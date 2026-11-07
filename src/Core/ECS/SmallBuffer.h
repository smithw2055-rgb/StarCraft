#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <type_traits>
#include <vector>

namespace Core::ECS
{

// ============================================================================
// SmallBuffer<T, N>: Inline Fixed-Capacity Component Buffer
// Referenced from Unity DOTS DynamicBuffer<T> InternalCapacity & LLVM SmallVector:
// - Stores up to N elements inline inside the component page with zero heap
//   allocations, preserving 100% cache locality and trivial copyability
//   (std::is_trivially_copyable_v) when T is trivially copyable.
// - Provides std::vector-compatible methods (push_back, clear, size, empty,
//   erase, begin, end, operator[]) and transparent std::vector<T> conversion
//   for backward compatibility with legacy system interfaces.
// ============================================================================
template <typename T, size_t N>
struct SmallBuffer
{
    static_assert(N > 0 && N <= 65535, "SmallBuffer capacity must fit in uint16_t");

    using value_type = T;
    using size_type = size_t;
    using iterator = T*;
    using const_iterator = const T*;

    std::array<T, N> mElements{};
    uint16_t mCount = 0;

    constexpr SmallBuffer() noexcept = default;

    constexpr SmallBuffer(std::initializer_list<T> Init) noexcept
    {
        for (const auto& Item : Init)
        {
            if (mCount >= N)
                break;
            mElements[mCount++] = Item;
        }
    }

    SmallBuffer(const std::vector<T>& Vec) noexcept
    {
        AssignFromVector(Vec);
    }

    SmallBuffer& operator=(const std::vector<T>& Vec) noexcept
    {
        AssignFromVector(Vec);
        return *this;
    }

    operator std::vector<T>() const
    {
        return ToVector();
    }

    std::vector<T> ToVector() const
    {
        return std::vector<T>(begin(), end());
    }

    void AssignFromVector(const std::vector<T>& Vec) noexcept
    {
        mCount = static_cast<uint16_t>(std::min(Vec.size(), N));
        for (uint16_t i = 0; i < mCount; ++i)
            mElements[i] = Vec[i];
    }

    constexpr bool empty() const noexcept { return mCount == 0; }
    constexpr bool Empty() const noexcept { return mCount == 0; }
    constexpr size_t size() const noexcept { return mCount; }
    constexpr size_t Size() const noexcept { return mCount; }
    static constexpr size_t capacity() noexcept { return N; }
    static constexpr size_t Capacity() noexcept { return N; }
    constexpr bool Full() const noexcept { return mCount >= N; }

    constexpr void clear() noexcept { mCount = 0; }
    constexpr void Clear() noexcept { mCount = 0; }

    constexpr bool push_back(const T& Value) noexcept
    {
        if (mCount >= N)
            return false;
        mElements[mCount++] = Value;
        return true;
    }

    constexpr bool PushBack(const T& Value) noexcept
    {
        return push_back(Value);
    }

    template <typename... Args>
    constexpr T& emplace_back(Args&&... InArgs) noexcept
    {
        if (mCount < N)
        {
            mElements[mCount] = T{std::forward<Args>(InArgs)...};
            return mElements[mCount++];
        }
        return mElements[N - 1];
    }

    constexpr void pop_back() noexcept
    {
        if (mCount > 0)
            --mCount;
    }

    constexpr void resize(size_t NewSize, const T& FillValue = T{}) noexcept
    {
        const uint16_t Clamped = static_cast<uint16_t>(std::min(NewSize, N));
        for (uint16_t i = mCount; i < Clamped; ++i)
            mElements[i] = FillValue;
        mCount = Clamped;
    }

    constexpr void reserve(size_t) noexcept {}

    constexpr iterator erase(const_iterator Pos) noexcept
    {
        const ptrdiff_t Idx = Pos - cbegin();
        if (Idx < 0 || static_cast<size_t>(Idx) >= mCount)
            return end();
        for (size_t i = static_cast<size_t>(Idx); i + 1 < mCount; ++i)
            mElements[i] = mElements[i + 1];
        --mCount;
        return begin() + Idx;
    }

    constexpr iterator erase(const_iterator First, const_iterator Last) noexcept
    {
        const ptrdiff_t StartIdx = First - cbegin();
        const ptrdiff_t EndIdx = Last - cbegin();
        if (StartIdx < 0 || EndIdx <= StartIdx || static_cast<size_t>(StartIdx) >= mCount)
            return end();
        const size_t ClampedEnd = std::min(static_cast<size_t>(EndIdx), static_cast<size_t>(mCount));
        const size_t CountToRemove = ClampedEnd - static_cast<size_t>(StartIdx);
        for (size_t i = static_cast<size_t>(StartIdx); i + CountToRemove < mCount; ++i)
            mElements[i] = mElements[i + CountToRemove];
        mCount = static_cast<uint16_t>(mCount - CountToRemove);
        return begin() + StartIdx;
    }

    constexpr T* data() noexcept { return mElements.data(); }
    constexpr const T* data() const noexcept { return mElements.data(); }

    constexpr iterator begin() noexcept { return mElements.data(); }
    constexpr iterator end() noexcept { return mElements.data() + mCount; }
    constexpr const_iterator begin() const noexcept { return mElements.data(); }
    constexpr const_iterator end() const noexcept { return mElements.data() + mCount; }
    constexpr const_iterator cbegin() const noexcept { return mElements.data(); }
    constexpr const_iterator cend() const noexcept { return mElements.data() + mCount; }

    constexpr T& front() noexcept { return mElements[0]; }
    constexpr const T& front() const noexcept { return mElements[0]; }
    constexpr T& back() noexcept { return mElements[mCount > 0 ? mCount - 1 : 0]; }
    constexpr const T& back() const noexcept { return mElements[mCount > 0 ? mCount - 1 : 0]; }

    constexpr T& operator[](size_t Idx) noexcept { return mElements[Idx]; }
    constexpr const T& operator[](size_t Idx) const noexcept { return mElements[Idx]; }

    constexpr bool operator==(const SmallBuffer& Other) const noexcept
    {
        if (mCount != Other.mCount)
            return false;
        for (uint16_t i = 0; i < mCount; ++i)
        {
            if (!(mElements[i] == Other.mElements[i]))
                return false;
        }
        return true;
    }

    constexpr bool operator!=(const SmallBuffer& Other) const noexcept
    {
        return !(*this == Other);
    }
};

} // namespace Core::ECS
