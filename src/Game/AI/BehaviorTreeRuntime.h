#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Game::AI {

enum class BTNodeStatus : uint8_t {
    Success = 0,
    Running = 1,
    Failure = 2
};

struct BTBuildOrderStep {
    std::string Op;       // "build", "tech", "upgrade", "train"
    std::string UnitOrTech;
    uint32_t Count = 1;
    uint32_t Priority = 80;
};

struct BTDefenseRule {
    std::string Op;       // "defensebuild_gg", "defenseuse_gg", etc.
    std::string UnitName;
    uint32_t Count = 1;
};

struct BTAttackWaveUnit {
    std::string UnitName;
    uint32_t Count = 1;
};

struct BTNode {
    std::string NodeType;
    std::string NodeName;
    std::string ActionOp;
    std::string TargetBlock;
    uint32_t WaitTicks = 0;
    std::vector<BTBuildOrderStep> BuildOrders;
    std::vector<BTDefenseRule> DefenseRules;
    std::vector<BTAttackWaveUnit> WaveRoster;
    uint32_t PreparationDelayTicks = 0;
};

struct BTBasicBlock {
    std::string BlockID;
    uint32_t StartOffset = 0;
    bool IsSelfLooping = false;
    std::string CompositeType;
    std::vector<BTNode> Children;
};

struct BTDaemonSpec {
    std::string DaemonType;
    std::string TargetBlock;
};

struct BehaviorTreeAsset {
    std::string AssetName;
    std::string ScriptIDFourCC;
    std::string RoleCategory;
    uint32_t EntryOffset = 0;
    uint32_t StatTxtID = 0;
    uint32_t Flags = 0;
    bool RequiresLocationRect = false;
    bool BroodWarOnly = false;

    // Blackboard defaults
    bool DefaultBuildOff = false;
    int32_t DefaultMin = 0;
    int32_t MaxForce = 0;
    std::unordered_map<std::string, uint32_t> MaxUnitCaps;

    std::string RootNodeType;
    std::string EntryBlockID;
    std::vector<BTDaemonSpec> ConcurrentDaemons;
    std::unordered_map<std::string, BTBasicBlock> BehaviorBlocks;
};

struct AIBlackboardState {
    bool TownStarted = false;
    bool CampaignMode = false;
    bool DefaultBuildOff = false;
    int32_t DefaultMin = 0;
    int32_t MaxForce = 0;
    uint32_t ExpansionsTriggered = 0;
    uint32_t AttackWavesLaunched = 0;
    std::vector<BTBuildOrderStep> QueuedBuildOrders;
    std::vector<BTDefenseRule> ActiveDefensePool;
    std::vector<BTAttackWaveUnit> LastLaunchedWave;
};

class BehaviorTreeInstance {
public:
    explicit BehaviorTreeInstance(const BehaviorTreeAsset* Asset = nullptr);

    void BindAsset(const BehaviorTreeAsset* Asset);
    BTNodeStatus Tick(uint32_t ElapsedTicks);

    const AIBlackboardState& GetBlackboard() const { return mBlackboard; }
    const std::string& GetCurrentBlockID() const { return mCurrentBlockID; }
    size_t GetCurrentChildIndex() const { return mCurrentChildIndex; }

private:
    const BehaviorTreeAsset* mAsset = nullptr;
    AIBlackboardState mBlackboard;
    std::string mCurrentBlockID;
    size_t mCurrentChildIndex = 0;
    uint32_t mWaitRemainingTicks = 0;
};

class BehaviorTreeCatalog {
public:
    BehaviorTreeCatalog() = default;
    ~BehaviorTreeCatalog() = default;

    bool LoadFromAssetsR(const std::string& AssetsRRootPath);

    size_t GetAIScriptTreeCount() const { return mAIScriptCount; }
    size_t GetBWScriptTreeCount() const { return mBWScriptCount; }
    size_t GetTotalTreeCount() const { return mAIScriptCount + mBWScriptCount; }

    const BehaviorTreeAsset* FindTree(const std::string& FourCC, bool PreferBWScript = false) const;
    const std::unordered_map<std::string, BehaviorTreeAsset>& GetAIScriptTrees() const { return mAIScriptTrees; }
    const std::unordered_map<std::string, BehaviorTreeAsset>& GetBWScriptTrees() const { return mBWScriptTrees; }

    // Cross-verifies entry offsets and FourCC headers against raw binary aiscript.bin / bwscript.bin
    bool CrossVerifyBinaryScripts(
        const std::string& RawAIScriptBinPath,
        const std::string& RawBWScriptBinPath,
        size_t& OutVerifiedCount,
        size_t& OutMismatches) const;

private:
    bool LoadTreeFile(const std::string& JsonPath, std::unordered_map<std::string, BehaviorTreeAsset>& OutMap, size_t& OutLoadedCount);

    size_t mAIScriptCount = 0;
    size_t mBWScriptCount = 0;
    std::unordered_map<std::string, BehaviorTreeAsset> mAIScriptTrees;
    std::unordered_map<std::string, BehaviorTreeAsset> mBWScriptTrees;
};

} // namespace Game::AI
