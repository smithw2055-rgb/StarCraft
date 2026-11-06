#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace Core::Concurrency
{

struct JobSystemStats
{
    uint32_t WorkerThreadCount = 0;
    uint64_t TotalParallelForCalls = 0;
    uint64_t TotalChunksExecuted = 0;
    uint64_t TotalItemsProcessed = 0;
};

// ============================================================================
// JobSystem: Modern data-oriented parallel task runner
// Referenced from Unity C# Job System (IJobParallelFor) & Godot WorkerThreadPool:
// - Zero-overhead main-thread fallback when item count < MinBatchSize
// - Atomic work-stealing chunk counter across worker threads + calling thread
// - Deterministic index-ordered Map-Reduce helper for lockstep RTS simulations
// ============================================================================
class JobSystem
{
public:
    static JobSystem& Instance()
    {
        static JobSystem sInstance;
        return sInstance;
    }

    explicit JobSystem(uint32_t RequestedWorkers = 0)
    {
        Initialize(RequestedWorkers);
    }

    ~JobSystem()
    {
        Shutdown();
    }

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    void Initialize(uint32_t RequestedWorkers = 0)
    {
        Shutdown();

        uint32_t HwThreads = std::thread::hardware_concurrency();
        if (HwThreads == 0)
            HwThreads = 4;

        const uint32_t NumWorkers = (RequestedWorkers > 0)
            ? RequestedWorkers
            : (HwThreads > 1 ? std::min(HwThreads - 1, 7u) : 0u);

        mStopRequested.store(false, std::memory_order_relaxed);
        mActiveBatchId.store(0, std::memory_order_relaxed);
        mWorkers.reserve(NumWorkers);

        for (uint32_t W = 0; W < NumWorkers; ++W)
        {
            mWorkers.emplace_back([this, W]() { WorkerLoop(W + 1); });
        }
    }

    void Shutdown()
    {
        if (mWorkers.empty())
            return;

        {
            std::lock_guard<std::mutex> Lock(mWakeMutex);
            mStopRequested.store(true, std::memory_order_release);
        }
        mWakeCv.notify_all();

        for (auto& Thread : mWorkers)
        {
            if (Thread.joinable())
                Thread.join();
        }
        mWorkers.clear();
    }

    uint32_t GetWorkerCount() const
    {
        return static_cast<uint32_t>(mWorkers.size());
    }

    uint32_t GetTotalThreadCount() const
    {
        return static_cast<uint32_t>(mWorkers.size()) + 1u;
    }

    // Kernel signature: void(uint32_t BeginIndex, uint32_t EndIndex, uint32_t ThreadIndex)
    template <typename RangeKernel>
    void ParallelForRange(uint32_t ItemCount, uint32_t MinBatchSize, RangeKernel&& Kernel)
    {
        if (ItemCount == 0)
            return;

        const uint32_t EffectiveBatchSize = std::max(1u, MinBatchSize);
        const uint32_t NumChunks = (ItemCount + EffectiveBatchSize - 1u) / EffectiveBatchSize;

        mTotalParallelForCalls.fetch_add(1, std::memory_order_relaxed);
        mTotalItemsProcessed.fetch_add(ItemCount, std::memory_order_relaxed);

        // Fast single-threaded path if only 1 chunk, no workers, or nested parallel call
        if (NumChunks <= 1u || mWorkers.empty() || mInParallelRegion.exchange(true, std::memory_order_acquire))
        {
            Kernel(0u, ItemCount, 0u);
            mTotalChunksExecuted.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        mCurrentItemCount = ItemCount;
        mCurrentBatchSize = EffectiveBatchSize;
        mCurrentChunkCount = NumChunks;
        mNextChunkIndex.store(0u, std::memory_order_relaxed);
        mCompletedChunks.store(0u, std::memory_order_relaxed);
        mCurrentKernel = [&](uint32_t Begin, uint32_t End, uint32_t ThreadIdx)
        {
            Kernel(Begin, End, ThreadIdx);
        };

        {
            std::lock_guard<std::mutex> Lock(mWakeMutex);
            mActiveBatchId.fetch_add(1, std::memory_order_release);
        }
        mWakeCv.notify_all();

        // Calling thread participates as ThreadIndex 0 (work-stealing chunks)
        ExecuteAvailableChunks(0u);

        // Wait until all chunks have completed
        while (mCompletedChunks.load(std::memory_order_acquire) < NumChunks)
        {
            std::this_thread::yield();
        }

        mCurrentKernel = nullptr;
        mTotalChunksExecuted.fetch_add(NumChunks, std::memory_order_relaxed);
        mInParallelRegion.store(false, std::memory_order_release);
    }

    // Per-item convenience overload: void(uint32_t Index)
    template <typename ItemKernel>
    void ParallelFor(uint32_t ItemCount, uint32_t MinBatchSize, ItemKernel&& Kernel)
    {
        ParallelForRange(
            ItemCount,
            MinBatchSize,
            [&](uint32_t Begin, uint32_t End, uint32_t /*ThreadIdx*/)
            {
                for (uint32_t I = Begin; I < End; ++I)
                {
                    Kernel(I);
                }
            });
    }

    // Deterministic parallel Map-Reduce:
    // 1. Executes Map(uint32_t Begin, uint32_t End) -> PartialResult for each chunk in parallel
    // 2. Reduces PartialResults in strict ascending chunk index order [0 .. NumChunks-1]
    // Guarantees identical bit-for-bit results regardless of OS thread scheduling!
    template <typename PartialResultT, typename MapFunc, typename ReduceFunc>
    PartialResultT ParallelDeterministicReduce(uint32_t ItemCount,
                                               uint32_t MinBatchSize,
                                               PartialResultT InitialValue,
                                               MapFunc&& Map,
                                               ReduceFunc&& Reduce)
    {
        if (ItemCount == 0)
            return InitialValue;

        const uint32_t EffectiveBatchSize = std::max(1u, MinBatchSize);
        const uint32_t NumChunks = (ItemCount + EffectiveBatchSize - 1u) / EffectiveBatchSize;
        std::vector<PartialResultT> ChunkResults(NumChunks, InitialValue);

        ParallelFor(
            NumChunks,
            1u,
            [&](uint32_t ChunkIdx)
            {
                const uint32_t Begin = ChunkIdx * EffectiveBatchSize;
                const uint32_t End = std::min(ItemCount, Begin + EffectiveBatchSize);
                ChunkResults[ChunkIdx] = Map(Begin, End);
            });

        PartialResultT FinalAcc = InitialValue;
        for (uint32_t ChunkIdx = 0; ChunkIdx < NumChunks; ++ChunkIdx)
        {
            FinalAcc = Reduce(FinalAcc, ChunkResults[ChunkIdx]);
        }
        return FinalAcc;
    }

    JobSystemStats GetStats() const
    {
        JobSystemStats S;
        S.WorkerThreadCount = GetWorkerCount();
        S.TotalParallelForCalls = mTotalParallelForCalls.load(std::memory_order_relaxed);
        S.TotalChunksExecuted = mTotalChunksExecuted.load(std::memory_order_relaxed);
        S.TotalItemsProcessed = mTotalItemsProcessed.load(std::memory_order_relaxed);
        return S;
    }

private:
    void ExecuteAvailableChunks(uint32_t ThreadIdx)
    {
        while (true)
        {
            const uint32_t ChunkIdx = mNextChunkIndex.fetch_add(1u, std::memory_order_relaxed);
            if (ChunkIdx >= mCurrentChunkCount)
                break;

            const uint32_t Begin = ChunkIdx * mCurrentBatchSize;
            const uint32_t End = std::min(mCurrentItemCount, Begin + mCurrentBatchSize);
            if (mCurrentKernel)
            {
                mCurrentKernel(Begin, End, ThreadIdx);
            }
            mCompletedChunks.fetch_add(1u, std::memory_order_release);
        }
    }

    void WorkerLoop(uint32_t ThreadIdx)
    {
        uint64_t LastSeenBatchId = 0;
        while (true)
        {
            {
                std::unique_lock<std::mutex> Lock(mWakeMutex);
                mWakeCv.wait(Lock, [&]()
                {
                    return mStopRequested.load(std::memory_order_acquire) ||
                           mActiveBatchId.load(std::memory_order_acquire) != LastSeenBatchId;
                });

                if (mStopRequested.load(std::memory_order_acquire))
                    return;

                LastSeenBatchId = mActiveBatchId.load(std::memory_order_acquire);
            }

            ExecuteAvailableChunks(ThreadIdx);
        }
    }

    std::vector<std::thread> mWorkers;
    std::mutex mWakeMutex;
    std::condition_variable mWakeCv;
    std::atomic<bool> mStopRequested{false};
    std::atomic<bool> mInParallelRegion{false};
    std::atomic<uint64_t> mActiveBatchId{0};

    uint32_t mCurrentItemCount = 0;
    uint32_t mCurrentBatchSize = 1;
    uint32_t mCurrentChunkCount = 0;
    std::atomic<uint32_t> mNextChunkIndex{0};
    std::atomic<uint32_t> mCompletedChunks{0};
    std::function<void(uint32_t, uint32_t, uint32_t)> mCurrentKernel;

    std::atomic<uint64_t> mTotalParallelForCalls{0};
    std::atomic<uint64_t> mTotalChunksExecuted{0};
    std::atomic<uint64_t> mTotalItemsProcessed{0};
};

} // namespace Core::Concurrency
