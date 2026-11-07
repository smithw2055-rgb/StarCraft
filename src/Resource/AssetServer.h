#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Core/Concurrency/JobSystem.h"
#include "Resource/IArchiveReader.h"
#include "Resource/ModernAssetRegistry.h"

namespace Resource
{

enum class AssetLoadState : uint8_t
{
    Unloaded = 0,
    Queued,
    Loading,
    Ready,
    Failed
};

struct AssetServerTelemetry
{
    uint64_t TotalHandlesAllocated = 0;
    uint64_t ActiveLoadedAssets = 0;
    uint64_t AsyncDecodeJobsDispatched = 0;
    uint64_t MainThreadGpuUploadsFlushed = 0;
    uint64_t MainThreadBytesUploaded = 0;
    uint64_t CacheHits = 0;
};

struct PendingGpuUploadTask
{
    std::string LogicalUri;
    uint32_t ByteSize = 0;
    std::function<void()> UploadCallback;
};

// ============================================================================
// AssetServer: Unified Addressable & Generational Handle Asset Coordinator
// Referenced from Godot 4 ResourceLoader/RenderingServer & Unity Addressables:
// - Bridges ModernAssetRegistry (HD PBR / JSON catalogs) and IArchiveReader
//   (MPQ / CASC / FileSystem) under a single priority-mounted virtual VFS.
// - Issues generational AssetHandle<T> references with reference counting and
//   generation validation to prevent dangling asset pointers.
// - Offloads CPU-heavy decoding to Core::Concurrency::JobSystem and queues
//   time-sliced GPU uploads via FlushPendingGpuUploads(BudgetBytes).
// ============================================================================
class AssetServer
{
public:
    static AssetServer& Instance()
    {
        static AssetServer sServer;
        return sServer;
    }

    AssetServer() = default;
    ~AssetServer() = default;

    void BindModernRegistry(const ModernAssetRegistry* Registry)
    {
        mModernRegistry = Registry;
    }

    const ModernAssetRegistry* GetModernRegistry() const
    {
        return mModernRegistry;
    }

    void MountArchive(std::shared_ptr<IArchiveReader> Archive, int Priority = 0)
    {
        if (!Archive)
            return;
        mMountedArchives.push_back({Priority, std::move(Archive)});
        std::stable_sort(
            mMountedArchives.begin(),
            mMountedArchives.end(),
            [](const MountedArchiveEntry& A, const MountedArchiveEntry& B) {
                return A.Priority > B.Priority;
            });
    }

    void ClearArchives()
    {
        mMountedArchives.clear();
    }

    // Registers or retrieves a cached logical asset with a generational AssetHandle<T>
    template <typename AssetT>
    AssetHandle<AssetT> RegisterOrAcquire(std::string_view LogicalUri, AssetT Value)
    {
        const std::string Key(LogicalUri);
        auto& Pool = GetTypedPool<AssetT>();

        auto It = Pool.UriToIndex.find(Key);
        if (It != Pool.UriToIndex.end())
        {
            const uint32_t Idx = It->second;
            auto& Slot = Pool.Slots[Idx];
            if (Slot.Alive)
            {
                ++Slot.RefCount;
                ++mTelemetry.CacheHits;
                return AssetHandle<AssetT>{Idx, Slot.Generation};
            }
        }

        uint32_t Idx = UINT32_MAX;
        if (!Pool.FreeIndices.empty())
        {
            Idx = Pool.FreeIndices.back();
            Pool.FreeIndices.pop_back();
        }
        else
        {
            Idx = static_cast<uint32_t>(Pool.Slots.size());
            Pool.Slots.emplace_back();
        }

        auto& Slot = Pool.Slots[Idx];
        ++Slot.Generation;
        if (Slot.Generation == 0)
            Slot.Generation = 1;
        Slot.Alive = true;
        Slot.RefCount = 1;
        Slot.State = AssetLoadState::Ready;
        Slot.Uri = Key;
        Slot.Payload = std::move(Value);

        Pool.UriToIndex[Key] = Idx;
        ++mTelemetry.TotalHandlesAllocated;
        ++mTelemetry.ActiveLoadedAssets;
        return AssetHandle<AssetT>{Idx, Slot.Generation};
    }

    template <typename AssetT>
    const AssetT* Resolve(AssetHandle<AssetT> Handle) const
    {
        const auto& Pool = GetTypedPool<AssetT>();
        if (Handle.Index >= Pool.Slots.size())
            return nullptr;
        const auto& Slot = Pool.Slots[Handle.Index];
        if (!Slot.Alive || Slot.Generation != Handle.Generation || Slot.State != AssetLoadState::Ready)
            return nullptr;
        return &Slot.Payload;
    }

    template <typename AssetT>
    bool Release(AssetHandle<AssetT> Handle)
    {
        auto& Pool = GetTypedPool<AssetT>();
        if (Handle.Index >= Pool.Slots.size())
            return false;
        auto& Slot = Pool.Slots[Handle.Index];
        if (!Slot.Alive || Slot.Generation != Handle.Generation)
            return false;

        if (Slot.RefCount > 0)
            --Slot.RefCount;

        if (Slot.RefCount == 0)
        {
            Slot.Alive = false;
            Slot.State = AssetLoadState::Unloaded;
            Pool.UriToIndex.erase(Slot.Uri);
            Slot.Uri.clear();
            Slot.Payload = AssetT{};
            Pool.FreeIndices.push_back(Handle.Index);
            if (mTelemetry.ActiveLoadedAssets > 0)
                --mTelemetry.ActiveLoadedAssets;
        }
        return true;
    }

    // Dispatches a batch of CPU asset decode tasks across Core::Concurrency::JobSystem
    // and queues their completion GPU upload callbacks for budgeted main-thread execution.
    template <typename DecodeBatchItemT, typename DecodeFnT>
    void DispatchAsyncBatchDecode(const std::vector<DecodeBatchItemT>& Items, DecodeFnT&& DecodeFn)
    {
        if (Items.empty())
            return;

        const uint32_t Count = static_cast<uint32_t>(Items.size());
        std::vector<PendingGpuUploadTask> ProducedTasks(Count);

        Core::Concurrency::JobSystem::Instance().ParallelFor(
            Count,
            1u,
            [&](uint32_t Index) {
                ProducedTasks[Index] = DecodeFn(Items[Index]);
            });

        mTelemetry.AsyncDecodeJobsDispatched += Count;
        for (auto& Task : ProducedTasks)
        {
            if (Task.UploadCallback)
                mPendingGpuUploads.push_back(std::move(Task));
        }
    }

    void EnqueueGpuUpload(std::string LogicalUri, uint32_t ByteSize, std::function<void()> UploadCallback)
    {
        if (!UploadCallback)
            return;
        mPendingGpuUploads.push_back({std::move(LogicalUri), ByteSize, std::move(UploadCallback)});
    }

    // Flushes queued GPU uploads within the frame byte budget (prevents frame-time spikes)
    size_t FlushPendingGpuUploads(uint32_t MaxUploadBudgetBytesPerFrame = 8u * 1024u * 1024u)
    {
        if (mPendingGpuUploads.empty())
            return 0;

        uint32_t BytesUploadedThisFrame = 0;
        size_t FlushedCount = 0;

        while (!mPendingGpuUploads.empty())
        {
            const auto& Next = mPendingGpuUploads.front();
            // Always allow at least 1 upload per frame even if a single texture exceeds budget
            if (FlushedCount > 0 && BytesUploadedThisFrame + Next.ByteSize > MaxUploadBudgetBytesPerFrame)
                break;

            PendingGpuUploadTask Task = std::move(mPendingGpuUploads.front());
            mPendingGpuUploads.erase(mPendingGpuUploads.begin());

            if (Task.UploadCallback)
                Task.UploadCallback();

            BytesUploadedThisFrame += Task.ByteSize;
            ++FlushedCount;
            ++mTelemetry.MainThreadGpuUploadsFlushed;
            mTelemetry.MainThreadBytesUploaded += Task.ByteSize;
        }
        return FlushedCount;
    }

    size_t GetPendingGpuUploadCount() const { return mPendingGpuUploads.size(); }
    const AssetServerTelemetry& GetTelemetry() const { return mTelemetry; }
    void ResetTelemetry() { mTelemetry = {}; }

private:
    struct MountedArchiveEntry
    {
        int Priority = 0;
        std::shared_ptr<IArchiveReader> Archive;
    };

    template <typename AssetT>
    struct AssetSlot
    {
        uint32_t Generation = 0;
        uint32_t RefCount = 0;
        bool Alive = false;
        AssetLoadState State = AssetLoadState::Unloaded;
        std::string Uri;
        AssetT Payload{};
    };

    template <typename AssetT>
    struct TypedAssetPool
    {
        std::vector<AssetSlot<AssetT>> Slots;
        std::vector<uint32_t> FreeIndices;
        std::unordered_map<std::string, uint32_t> UriToIndex;
    };

    template <typename AssetT>
    TypedAssetPool<AssetT>& GetTypedPool() const
    {
        static TypedAssetPool<AssetT> sPool;
        return sPool;
    }

    const ModernAssetRegistry* mModernRegistry = nullptr;
    std::vector<MountedArchiveEntry> mMountedArchives;
    std::vector<PendingGpuUploadTask> mPendingGpuUploads;
    AssetServerTelemetry mTelemetry{};
};

} // namespace Resource
