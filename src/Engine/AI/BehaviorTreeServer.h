#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Engine/DataAssetRegistry.h"
#include "Game/AI/BehaviorTreeRuntime.h"

namespace Engine::AI
{

enum class BTStatus : uint8_t
{
    Success = 0,
    Failure = 1,
    Running = 2
};

// ============================================================================
// Blackboard: General-Purpose Typed AI Context Store
// ============================================================================
class Blackboard
{
public:
    void SetInt(std::string Key, int64_t Value) { mInts[std::move(Key)] = Value; }
    int64_t GetInt(const std::string& Key, int64_t Default = 0) const
    {
        auto It = mInts.find(Key);
        return It != mInts.end() ? It->second : Default;
    }

    void SetFloat(std::string Key, float Value) { mFloats[std::move(Key)] = Value; }
    float GetFloat(const std::string& Key, float Default = 0.0f) const
    {
        auto It = mFloats.find(Key);
        return It != mFloats.end() ? It->second : Default;
    }

    void SetBool(std::string Key, bool Value) { mBools[std::move(Key)] = Value; }
    bool GetBool(const std::string& Key, bool Default = false) const
    {
        auto It = mBools.find(Key);
        return It != mBools.end() ? It->second : Default;
    }

    void SetString(std::string Key, std::string Value) { mStrings[std::move(Key)] = std::move(Value); }
    const std::string& GetString(const std::string& Key) const
    {
        static const std::string kEmpty;
        auto It = mStrings.find(Key);
        return It != mStrings.end() ? It->second : kEmpty;
    }

    void Clear()
    {
        mInts.clear();
        mFloats.clear();
        mBools.clear();
        mStrings.clear();
    }

private:
    std::unordered_map<std::string, int64_t> mInts;
    std::unordered_map<std::string, float> mFloats;
    std::unordered_map<std::string, bool> mBools;
    std::unordered_map<std::string, std::string> mStrings;
};

// ============================================================================
// IBTNode & Composite / Leaf Behavior Tree Nodes
// ============================================================================
class IBTNode
{
public:
    virtual ~IBTNode() = default;
    virtual BTStatus Tick(Blackboard& Board) = 0;
};

class BTSequenceNode final : public IBTNode
{
public:
    void AddChild(std::unique_ptr<IBTNode> Child)
    {
        if (Child)
            mChildren.push_back(std::move(Child));
    }

    BTStatus Tick(Blackboard& Board) override
    {
        for (auto& Child : mChildren)
        {
            const BTStatus Status = Child->Tick(Board);
            if (Status != BTStatus::Success)
                return Status;
        }
        return BTStatus::Success;
    }

private:
    std::vector<std::unique_ptr<IBTNode>> mChildren;
};

class BTSelectorNode final : public IBTNode
{
public:
    void AddChild(std::unique_ptr<IBTNode> Child)
    {
        if (Child)
            mChildren.push_back(std::move(Child));
    }

    BTStatus Tick(Blackboard& Board) override
    {
        for (auto& Child : mChildren)
        {
            const BTStatus Status = Child->Tick(Board);
            if (Status != BTStatus::Failure)
                return Status;
        }
        return BTStatus::Failure;
    }

private:
    std::vector<std::unique_ptr<IBTNode>> mChildren;
};

class BTConditionNode final : public IBTNode
{
public:
    explicit BTConditionNode(std::function<bool(const Blackboard&)> Predicate)
        : mPredicate(std::move(Predicate))
    {
    }

    BTStatus Tick(Blackboard& Board) override
    {
        return (mPredicate && mPredicate(Board)) ? BTStatus::Success : BTStatus::Failure;
    }

private:
    std::function<bool(const Blackboard&)> mPredicate;
};

class BTActionNode final : public IBTNode
{
public:
    explicit BTActionNode(std::function<BTStatus(Blackboard&)> Action)
        : mAction(std::move(Action))
    {
    }

    BTStatus Tick(Blackboard& Board) override
    {
        return mAction ? mAction(Board) : BTStatus::Failure;
    }

private:
    std::function<BTStatus(Blackboard&)> mAction;
};

// ============================================================================
// BehaviorTreeAsset & BehaviorTreeServer (Phase 40):
// - Stores data-driven BehaviorTreeAssets in an Engine::DataAssetTable<BehaviorTreeAsset>.
// - Supports direct import of cloud assets-R AIScriptBin_All_293_BehaviorTrees.json
//   via Game::AI::BehaviorTreeCatalog and runtime execution against Blackboards.
// ============================================================================
struct BehaviorTreeAsset
{
    uint32_t TreeID = 0;
    std::string ScriptID;
    std::string DisplayName;
    bool HasStarCraftBWExpansion = false;
    uint32_t TotalBehaviorBlocks = 0;
    std::vector<std::string> HighLevelNodes;
    std::shared_ptr<IBTNode> RootNode;
};

class BehaviorTreeServer
{
public:
    using NodeActionHandler = std::function<BTStatus(const std::string& ActionOp, Blackboard&)>;

    void RegisterNodeHandler(std::string ActionOp, NodeActionHandler Handler)
    {
        mNodeHandlers[std::move(ActionOp)] = std::move(Handler);
    }

    Engine::AssetHandle<BehaviorTreeAsset> RegisterTree(
        uint32_t TreeID,
        std::string ScriptID,
        BehaviorTreeAsset Asset)
    {
        Asset.TreeID = TreeID;
        Asset.ScriptID = ScriptID;
        return mTreeTable.Register(TreeID, std::move(ScriptID), std::move(Asset));
    }

    // Imports AI Behavior Trees from cloud assets-R BehaviorTreeCatalog
    size_t ImportFromCatalog(const Game::AI::BehaviorTreeCatalog& Catalog)
    {
        size_t Imported = 0;
        uint32_t NextID = 1;

        auto ImportMap = [&](const std::unordered_map<std::string, Game::AI::BehaviorTreeAsset>& Map,
                             bool IsBW) {
            for (const auto& [FourCC, Src] : Map)
            {
                BehaviorTreeAsset Asset;
                Asset.TreeID = NextID++;
                Asset.ScriptID = IsBW ? ("BW_" + FourCC) : FourCC;
                Asset.DisplayName = Src.AssetName;
                Asset.HasStarCraftBWExpansion = Src.BroodWarOnly || IsBW;
                Asset.TotalBehaviorBlocks = static_cast<uint32_t>(Src.BehaviorBlocks.size());

                auto RootSeq = std::make_unique<BTSequenceNode>();
                auto EntryIt = Src.BehaviorBlocks.find(Src.EntryBlockID);
                if (EntryIt == Src.BehaviorBlocks.end() && !Src.BehaviorBlocks.empty())
                    EntryIt = Src.BehaviorBlocks.begin();

                if (EntryIt != Src.BehaviorBlocks.end())
                {
                    std::unordered_set<std::string> VisitedBlocks;
                    auto AppendBlockRecursive = [&](auto& Self,
                                                    BTSequenceNode& ParentSeq,
                                                    const std::string& BlockID) -> void {
                        if (BlockID.empty() || !VisitedBlocks.insert(BlockID).second)
                            return;
                        auto BlockIt = Src.BehaviorBlocks.find(BlockID);
                        if (BlockIt == Src.BehaviorBlocks.end())
                            return;

                        for (const auto& Child : BlockIt->second.Children)
                        {
                            const std::string ActionOp = Child.ActionOp.empty() ? Child.NodeType : Child.ActionOp;
                            if (!ActionOp.empty())
                            {
                                Asset.HighLevelNodes.push_back(ActionOp);
                                ParentSeq.AddChild(std::make_unique<BTActionNode>(
                                    [this, ActionOp](Blackboard& Board) -> BTStatus {
                                        Board.SetInt("ExecutedNodeCount", Board.GetInt("ExecutedNodeCount", 0) + 1);
                                        Board.SetString("LastActionOp", ActionOp);
                                        auto It = mNodeHandlers.find(ActionOp);
                                        if (It != mNodeHandlers.end() && It->second)
                                            return It->second(ActionOp, Board);
                                        return BTStatus::Success;
                                    }));
                            }
                            if (!Child.TargetBlock.empty())
                            {
                                Self(Self, ParentSeq, Child.TargetBlock);
                            }
                        }
                    };

                    AppendBlockRecursive(AppendBlockRecursive, *RootSeq, EntryIt->first);
                }
                Asset.RootNode = std::move(RootSeq);

                const uint32_t Id = Asset.TreeID; const std::string Name = Asset.ScriptID; mTreeTable.Register(Id, Name, std::move(Asset));
                ++Imported;
            }
        };

        ImportMap(Catalog.GetAIScriptTrees(), false);
        ImportMap(Catalog.GetBWScriptTrees(), true);
        return Imported;
    }

    size_t LoadFromAssetsRDirectory(const std::string& AssetsRRootPath)
    {
        Game::AI::BehaviorTreeCatalog Catalog;
        if (!Catalog.LoadFromAssetsR(AssetsRRootPath))
            return 0;
        return ImportFromCatalog(Catalog);
    }

    BTStatus TickTreeByName(std::string_view ScriptID, Blackboard& Board) const
    {
        const BehaviorTreeAsset* Asset = mTreeTable.FindByName(ScriptID);
        if (!Asset || !Asset->RootNode)
            return BTStatus::Failure;
        ++mTotalTreeTicks;
        return Asset->RootNode->Tick(Board);
    }

    BTStatus TickTree(Engine::AssetHandle<BehaviorTreeAsset> Handle, Blackboard& Board) const
    {
        const BehaviorTreeAsset* Asset = mTreeTable.Resolve(Handle);
        if (!Asset || !Asset->RootNode)
            return BTStatus::Failure;
        ++mTotalTreeTicks;
        return Asset->RootNode->Tick(Board);
    }

    const Engine::DataAssetTable<BehaviorTreeAsset>& GetTreeTable() const noexcept { return mTreeTable; }
    Engine::DataAssetTable<BehaviorTreeAsset>& GetTreeTable() noexcept { return mTreeTable; }
    uint64_t GetTotalTreeTicks() const noexcept { return mTotalTreeTicks; }

private:
    Engine::DataAssetTable<BehaviorTreeAsset> mTreeTable;
    std::unordered_map<std::string, NodeActionHandler> mNodeHandlers;
    mutable uint64_t mTotalTreeTicks = 0;
};

} // namespace Engine::AI
