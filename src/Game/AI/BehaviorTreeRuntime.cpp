#include "Game/AI/BehaviorTreeRuntime.h"
#include "Core/Utils/JsonLite.h"

#include <cstring>
#include <fstream>

namespace Game::AI {

using Core::Utils::JsonParser;
using Core::Utils::JsonValue;

BehaviorTreeInstance::BehaviorTreeInstance(const BehaviorTreeAsset* Asset)
{
    BindAsset(Asset);
}

void BehaviorTreeInstance::BindAsset(const BehaviorTreeAsset* Asset)
{
    mAsset = Asset;
    mBlackboard = AIBlackboardState{};
    mCurrentChildIndex = 0;
    mWaitRemainingTicks = 0;
    if (mAsset) {
        mBlackboard.DefaultBuildOff = mAsset->DefaultBuildOff;
        mBlackboard.DefaultMin = mAsset->DefaultMin;
        mBlackboard.MaxForce = mAsset->MaxForce;
        mCurrentBlockID = mAsset->EntryBlockID;
    } else {
        mCurrentBlockID.clear();
    }
}

BTNodeStatus BehaviorTreeInstance::Tick(uint32_t ElapsedTicks)
{
    if (!mAsset || mCurrentBlockID.empty()) {
        return BTNodeStatus::Failure;
    }

    uint32_t RemainingBudget = ElapsedTicks;
    size_t SafetySteps = 0;

    while (SafetySteps++ < 256) {
        if (mWaitRemainingTicks > 0) {
            if (RemainingBudget < mWaitRemainingTicks) {
                mWaitRemainingTicks -= RemainingBudget;
                return BTNodeStatus::Running;
            }
            RemainingBudget -= mWaitRemainingTicks;
            mWaitRemainingTicks = 0;
        }

        auto BlockIt = mAsset->BehaviorBlocks.find(mCurrentBlockID);
        if (BlockIt == mAsset->BehaviorBlocks.end()) {
            return BTNodeStatus::Success;
        }

        const BTBasicBlock& Block = BlockIt->second;
        if (mCurrentChildIndex >= Block.Children.size()) {
            if (Block.IsSelfLooping) {
                mCurrentChildIndex = 0;
                if (RemainingBudget == 0) return BTNodeStatus::Running;
                continue;
            }
            return BTNodeStatus::Success;
        }

        const BTNode& Node = Block.Children[mCurrentChildIndex++];

        if (Node.NodeType == "BT_Wait") {
            mWaitRemainingTicks = Node.WaitTicks;
            continue;
        }
        if (Node.NodeType == "BT_Action_TownInit") {
            if (Node.ActionOp == "start_town" || Node.ActionOp == "init_area_town") {
                mBlackboard.TownStarted = true;
            } else if (Node.ActionOp == "start_campaign") {
                mBlackboard.CampaignMode = true;
            } else if (Node.ActionOp == "capt_expand" || Node.ActionOp == "expand") {
                ++mBlackboard.ExpansionsTriggered;
            }
        } else if (Node.NodeType == "BT_Sequence" && Node.NodeName == "BuildAndTechOrderPhase") {
            for (const auto& Step : Node.BuildOrders) {
                mBlackboard.QueuedBuildOrders.push_back(Step);
            }
        } else if (Node.NodeType == "BT_Action_UpdateDefenseForce") {
            for (const auto& Rule : Node.DefenseRules) {
                mBlackboard.ActiveDefensePool.push_back(Rule);
            }
        } else if (Node.NodeType == "BT_Sequence" && Node.NodeName == "ExecuteAttackWave") {
            mBlackboard.LastLaunchedWave = Node.WaveRoster;
            ++mBlackboard.AttackWavesLaunched;
            if (!Node.TargetBlock.empty()) {
                mCurrentBlockID = Node.TargetBlock;
                mCurrentChildIndex = 0;
            }
            if (Node.PreparationDelayTicks > 0) {
                mWaitRemainingTicks = Node.PreparationDelayTicks;
            }
        } else if (Node.NodeType == "BT_TransitionOrLoop") {
            if (!Node.TargetBlock.empty()) {
                mCurrentBlockID = Node.TargetBlock;
                mCurrentChildIndex = 0;
                if (RemainingBudget == 0) return BTNodeStatus::Running;
            }
        }
    }

    return BTNodeStatus::Running;
}

bool BehaviorTreeCatalog::LoadFromAssetsR(const std::string& AssetsRRootPath)
{
    mAIScriptTrees.clear();
    mBWScriptTrees.clear();
    mAIScriptCount = 0;
    mBWScriptCount = 0;

    if (!LoadTreeFile(AssetsRRootPath + "/AIScriptBin_All_293_BehaviorTrees.json", mAIScriptTrees, mAIScriptCount)) {
        return false;
    }
    if (!LoadTreeFile(AssetsRRootPath + "/BWScriptBin_All_156_BehaviorTrees.json", mBWScriptTrees, mBWScriptCount)) {
        return false;
    }
    return !mAIScriptTrees.empty() && !mBWScriptTrees.empty();
}

bool BehaviorTreeCatalog::LoadTreeFile(const std::string& JsonPath, std::unordered_map<std::string, BehaviorTreeAsset>& OutMap, size_t& OutLoadedCount)
{
    JsonValue Root;
    if (!JsonParser::ParseFile(JsonPath, Root)) return false;

    const auto& TreesObj = Root["behavior_trees"].AsObject();
    OutLoadedCount = TreesObj.size();

    for (const auto& [FourCC, TreeVal] : TreesObj) {
        BehaviorTreeAsset Asset;
        Asset.AssetName = TreeVal["bt_asset_name"].AsString();
        Asset.ScriptIDFourCC = TreeVal["script_id_fourcc"].AsString(FourCC);
        Asset.RoleCategory = TreeVal["role_category"].AsString();

        const auto& Meta = TreeVal["metadata"];
        Asset.EntryOffset = Meta["entry_offset"].AsUInt32();
        Asset.StatTxtID = Meta["stat_txt_id"].AsUInt32();
        Asset.Flags = Meta["flags"].AsUInt32();
        Asset.RequiresLocationRect = Meta["requires_location_rect"].AsBool();
        Asset.BroodWarOnly = Meta["brood_war_only"].AsBool();

        const auto& BB = TreeVal["blackboard"]["settings"];
        Asset.DefaultBuildOff = BB["defaultbuild_off"].AsBool();
        Asset.DefaultMin = BB["default_min"].AsInt();
        Asset.MaxForce = BB["max_force"].AsInt();

        for (const auto& [UnitName, CapVal] : TreeVal["blackboard"]["max_unit_caps"].AsObject()) {
            Asset.MaxUnitCaps[UnitName] = CapVal.AsUInt32();
        }

        const auto& RootNode = TreeVal["root_node"];
        Asset.RootNodeType = RootNode["node_type"].AsString();
        Asset.EntryBlockID = RootNode["entry_block"].AsString();

        for (const auto& DVal : RootNode["concurrent_background_daemons"].AsArray()) {
            BTDaemonSpec D;
            D.DaemonType = DVal["daemon_type"].AsString();
            D.TargetBlock = DVal["target_block"].AsString();
            Asset.ConcurrentDaemons.push_back(std::move(D));
        }

        for (const auto& [BlockID, BlockVal] : RootNode["behavior_blocks"].AsObject()) {
            BTBasicBlock Block;
            Block.BlockID = BlockID;
            Block.StartOffset = BlockVal["start_offset"].AsUInt32();
            Block.IsSelfLooping = BlockVal["is_self_looping"].AsBool();
            Block.CompositeType = BlockVal["bt_composite_type"].AsString();

            for (const auto& ChildVal : BlockVal["children"].AsArray()) {
                BTNode Node;
                Node.NodeType = ChildVal["node_type"].AsString();
                Node.NodeName = ChildVal["node_name"].AsString();
                Node.WaitTicks = ChildVal["ticks"].AsUInt32();
                Node.TargetBlock = ChildVal["target_block"].AsString();

                if (ChildVal.Has("action")) {
                    Node.ActionOp = ChildVal["action"]["op"].AsString();
                }

                for (const auto& OrdVal : ChildVal["orders"].AsArray()) {
                    BTBuildOrderStep Step;
                    Step.Op = OrdVal["op"].AsString();
                    Step.UnitOrTech = OrdVal.Has("unit") ? OrdVal["unit"].AsString() : OrdVal["tech"].AsString();
                    Step.Count = OrdVal["count"].AsUInt32(1);
                    Step.Priority = OrdVal["priority"].AsUInt32(80);
                    Node.BuildOrders.push_back(std::move(Step));
                }

                for (const auto& RuleVal : ChildVal["rules"].AsArray()) {
                    BTDefenseRule Rule;
                    Rule.Op = RuleVal["op"].AsString();
                    Rule.UnitName = RuleVal["unit"].AsString();
                    Rule.Count = RuleVal["count"].AsUInt32(1);
                    Node.DefenseRules.push_back(std::move(Rule));
                }

                for (const auto& WaveVal : ChildVal["wave_roster"].AsArray()) {
                    BTAttackWaveUnit WUnit;
                    WUnit.UnitName = WaveVal["unit"].AsString();
                    WUnit.Count = WaveVal["count"].AsUInt32(1);
                    Node.WaveRoster.push_back(std::move(WUnit));
                }
                Node.PreparationDelayTicks = ChildVal["preparation_delay_ticks"].AsUInt32();

                const auto& SubActions = ChildVal["sub_actions"].AsArray();
                if (!SubActions.empty() && SubActions.back().Has("fallthrough_block")) {
                    Node.TargetBlock = SubActions.back()["fallthrough_block"].AsString();
                }

                Block.Children.push_back(std::move(Node));
            }

            Asset.BehaviorBlocks.emplace(BlockID, std::move(Block));
        }

        OutMap[Asset.ScriptIDFourCC] = std::move(Asset);
    }
    return true;
}

const BehaviorTreeAsset* BehaviorTreeCatalog::FindTree(const std::string& FourCC, bool PreferBWScript) const
{
    if (PreferBWScript) {
        auto BWIt = mBWScriptTrees.find(FourCC);
        if (BWIt != mBWScriptTrees.end()) return &BWIt->second;
    }
    auto AIIt = mAIScriptTrees.find(FourCC);
    if (AIIt != mAIScriptTrees.end()) return &AIIt->second;
    auto BWIt = mBWScriptTrees.find(FourCC);
    return BWIt != mBWScriptTrees.end() ? &BWIt->second : nullptr;
}

static bool ReadBinaryTableEntries(
    const std::string& BinPath,
    std::unordered_map<std::string, uint32_t>& OutOffsets)
{
    std::ifstream File(BinPath, std::ios::binary);
    if (!File.is_open()) return false;
    std::vector<uint8_t> Data((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
    if (Data.size() < 4) return false;

    uint32_t TableOffset = 0;
    std::memcpy(&TableOffset, Data.data(), sizeof(uint32_t));
    if (TableOffset >= Data.size()) return false;

    const size_t Stride = (BinPath.find("bwscript") != std::string::npos) ? 8 : 16;
    size_t Pos = TableOffset;
    while (Pos + Stride <= Data.size()) {
        uint32_t IdRaw = 0;
        std::memcpy(&IdRaw, Data.data() + Pos, 4);
        if (IdRaw == 0) break;

        char FourCC[5] = {0};
        std::memcpy(FourCC, Data.data() + Pos, 4);

        uint32_t EntryOffset = 0;
        std::memcpy(&EntryOffset, Data.data() + Pos + 4, 4);

        OutOffsets[std::string(FourCC)] = EntryOffset;
        Pos += Stride;
    }
    return !OutOffsets.empty();
}

bool BehaviorTreeCatalog::CrossVerifyBinaryScripts(
    const std::string& RawAIScriptBinPath,
    const std::string& RawBWScriptBinPath,
    size_t& OutVerifiedCount,
    size_t& OutMismatches) const
{
    OutVerifiedCount = 0;
    OutMismatches = 0;

    std::unordered_map<std::string, uint32_t> AIOffsets;
    std::unordered_map<std::string, uint32_t> BWOffsets;
    if (!ReadBinaryTableEntries(RawAIScriptBinPath, AIOffsets)) return false;
    if (!ReadBinaryTableEntries(RawBWScriptBinPath, BWOffsets)) return false;

    for (const auto& [FourCC, Tree] : mAIScriptTrees) {
        auto It = AIOffsets.find(FourCC);
        if (It == AIOffsets.end() || It->second != Tree.EntryOffset) {
            ++OutMismatches;
            continue;
        }
        ++OutVerifiedCount;
    }
    for (const auto& [FourCC, Tree] : mBWScriptTrees) {
        auto It = BWOffsets.find(FourCC);
        if (It == BWOffsets.end() || It->second != Tree.EntryOffset) {
            ++OutMismatches;
            continue;
        }
        ++OutVerifiedCount;
    }
    return OutMismatches == 0 && OutVerifiedCount == (mAIScriptTrees.size() + mBWScriptTrees.size());
}

} // namespace Game::AI
