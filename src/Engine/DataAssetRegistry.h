#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Resource/ModernAssetRegistry.h"

namespace Engine
{

// ============================================================================
// DataAssetTable<AssetT>: Generic Data-Driven Asset Registry Table
// Referenced from Unity ScriptableObject / Addressables & Godot ResourceDB:
// - Stores immutable or hot-reloadable data assets in a contiguous dense vector.
// - Provides O(1) lookup by numeric ID, O(1) lookup by semantic name, and
//   generational Resource::AssetHandle<AssetT> validation.
// ============================================================================
template <typename AssetT>
class DataAssetTable
{
public:
    using HandleType = Resource::AssetHandle<AssetT>;

    void Clear()
    {
        mDenseAssets.clear();
        mGenerations.clear();
        mDenseNames.clear();
        mIdToDenseIndex.clear();
        mNameToDenseIndex.clear();
    }

    void Reserve(size_t Capacity)
    {
        mDenseAssets.reserve(Capacity);
        mGenerations.reserve(Capacity);
        mDenseNames.reserve(Capacity);
        mIdToDenseIndex.reserve(Capacity);
        mNameToDenseIndex.reserve(Capacity);
    }

    HandleType Register(uint32_t NumericId, std::string SemanticName, AssetT AssetValue)
    {
        auto ExistingIt = mIdToDenseIndex.find(NumericId);
        if (ExistingIt != mIdToDenseIndex.end())
        {
            const uint32_t DenseIdx = ExistingIt->second;
            mDenseAssets[DenseIdx] = std::move(AssetValue);
            ++mGenerations[DenseIdx];
            if (!mDenseNames[DenseIdx].empty() && mDenseNames[DenseIdx] != SemanticName)
            {
                mNameToDenseIndex.erase(mDenseNames[DenseIdx]);
            }
            if (!SemanticName.empty())
            {
                mNameToDenseIndex[SemanticName] = DenseIdx;
                mDenseNames[DenseIdx] = std::move(SemanticName);
            }
            return HandleType{DenseIdx, mGenerations[DenseIdx]};
        }

        const uint32_t DenseIdx = static_cast<uint32_t>(mDenseAssets.size());
        mDenseAssets.push_back(std::move(AssetValue));
        mGenerations.push_back(1u);
        mDenseNames.push_back(SemanticName);
        mIdToDenseIndex[NumericId] = DenseIdx;
        if (!SemanticName.empty())
            mNameToDenseIndex[std::move(SemanticName)] = DenseIdx;

        return HandleType{DenseIdx, 1u};
    }

    HandleType BumpGeneration(uint32_t NumericId) noexcept
    {
        auto It = mIdToDenseIndex.find(NumericId);
        if (It == mIdToDenseIndex.end())
            return HandleType{};
        const uint32_t DenseIdx = It->second;
        ++mGenerations[DenseIdx];
        return HandleType{DenseIdx, mGenerations[DenseIdx]};
    }

    const AssetT* Resolve(HandleType Handle) const noexcept
    {
        if (!Handle.IsValid() || Handle.Index >= mDenseAssets.size())
            return nullptr;
        if (mGenerations[Handle.Index] != Handle.Generation)
            return nullptr;
        return &mDenseAssets[Handle.Index];
    }

    const AssetT* FindById(uint32_t NumericId) const noexcept
    {
        auto It = mIdToDenseIndex.find(NumericId);
        return It != mIdToDenseIndex.end() ? &mDenseAssets[It->second] : nullptr;
    }

    const AssetT* FindByName(std::string_view SemanticName) const
    {
        auto It = mNameToDenseIndex.find(std::string(SemanticName));
        return It != mNameToDenseIndex.end() ? &mDenseAssets[It->second] : nullptr;
    }

    HandleType GetHandleById(uint32_t NumericId) const noexcept
    {
        auto It = mIdToDenseIndex.find(NumericId);
        if (It == mIdToDenseIndex.end())
            return HandleType{};
        const uint32_t DenseIdx = It->second;
        return HandleType{DenseIdx, mGenerations[DenseIdx]};
    }

    size_t Size() const noexcept { return mDenseAssets.size(); }
    bool Empty() const noexcept { return mDenseAssets.empty(); }
    const std::vector<AssetT>& GetAll() const noexcept { return mDenseAssets; }

private:
    std::vector<AssetT> mDenseAssets;
    std::vector<uint32_t> mGenerations;
    std::vector<std::string> mDenseNames;
    std::unordered_map<uint32_t, uint32_t> mIdToDenseIndex;
    std::unordered_map<std::string, uint32_t> mNameToDenseIndex;
};

} // namespace Engine
