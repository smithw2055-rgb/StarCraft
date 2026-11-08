#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Engine::Gameplay {

    struct TechDefinition {
        uint32_t TechID = 0;
        std::string Name;
        int MineralCost = 0;
        int GasCost = 0;
        uint32_t ResearchTicks = 0;
        uint32_t RequiredBuildingUnitID = 0;
        bool UnlockedByDefault = false;
    };

    struct UpgradeDefinition {
        uint32_t UpgradeID = 0;
        std::string Name;
        int MaxLevel = 3;
        int BaseMineralCost = 100;
        int MineralCostFactor = 75;
        int BaseGasCost = 100;
        int GasCostFactor = 75;
        uint32_t BaseResearchTicks = 4000;
        uint32_t ResearchTicksFactor = 480;
        uint32_t RequiredBuildingUnitID = 0;
        int BonusPerLevel = 1;
    };

    struct UpgradeCostResult {
        bool Valid = false;
        int MineralCost = 0;
        int GasCost = 0;
        uint32_t ResearchTicks = 0;
    };

    enum class ResearchTaskType : uint8_t {
        Tech = 0,
        Upgrade = 1
    };

    struct ResearchTask {
        int BuildingEntityID = -1;
        int PlayerID = 0;
        ResearchTaskType Type = ResearchTaskType::Tech;
        uint32_t TargetID = 0;
        int TargetLevel = 1;
        uint32_t RemainingTicks = 0;
        uint32_t TotalTicks = 0;
    };

    struct CompletedResearchEvent {
        int BuildingEntityID = -1;
        int PlayerID = 0;
        ResearchTaskType Type = ResearchTaskType::Tech;
        uint32_t TargetID = 0;
        int NewLevel = 1;
    };

    class TechAndUpgradeServer {
    public:
        static constexpr int kMaxPlayers = 8;
        static constexpr size_t kStandardTechCount = 44;
        static constexpr size_t kStandardUpgradeCount = 61;

        TechAndUpgradeServer() = default;
        ~TechAndUpgradeServer() = default;

        void Clear() {
            m_techCatalog.clear();
            m_upgradeCatalog.clear();
            for (auto& p : m_playerStates) {
                p.UnlockedTechs.clear();
                p.UpgradeLevels.clear();
            }
            m_activeQueues.clear();
        }

        void RegisterTech(const TechDefinition& def) {
            m_techCatalog[def.TechID] = def;
            if (def.UnlockedByDefault) {
                for (int p = 0; p < kMaxPlayers; ++p) {
                    m_playerStates[p].UnlockedTechs[def.TechID] = true;
                }
            }
        }

        void RegisterUpgrade(const UpgradeDefinition& def) {
            m_upgradeCatalog[def.UpgradeID] = def;
        }

        const TechDefinition* GetTechDefinition(uint32_t techID) const {
            auto it = m_techCatalog.find(techID);
            return (it != m_techCatalog.end()) ? &it->second : nullptr;
        }

        const UpgradeDefinition* GetUpgradeDefinition(uint32_t upgradeID) const {
            auto it = m_upgradeCatalog.find(upgradeID);
            return (it != m_upgradeCatalog.end()) ? &it->second : nullptr;
        }

        size_t GetTechCatalogSize() const {
            return m_techCatalog.size();
        }

        size_t GetUpgradeCatalogSize() const {
            return m_upgradeCatalog.size();
        }

        void LoadStandardRTSCatalog() {
            Clear();

            const std::array<TechDefinition, kStandardTechCount> standardTechs = {{
                {0,  "StimPacks",            100, 100, 1200, 112, false},
                {1,  "Lockdown",             200, 200, 1500, 117, false},
                {2,  "EMPShockwave",         200, 200, 1800, 116, false},
                {3,  "SpiderMines",          100, 100, 1200, 120, false},
                {4,  "ScannerSweep",           0,   0,    0, 115, true},
                {5,  "SiegeMode",            150, 150, 1200, 120, false},
                {6,  "DefensiveMatrix",        0,   0,    0, 116, true},
                {7,  "Irradiate",            200, 200, 1200, 116, false},
                {8,  "YamatoGun",            100, 100, 1800, 118, false},
                {9,  "CloakingField",        150, 150, 1500, 115, false},
                {10, "PersonnelCloaking",    100, 100, 1200, 117, false},
                {11, "Burrowing",            100, 100, 1200, 131, false},
                {12, "Infestation",            0,   0,    0, 138, true},
                {13, "SpawnBroodlings",      100, 100, 1200, 138, false},
                {14, "DarkSwarm",              0,   0,    0, 136, true},
                {15, "Plague",               200, 200, 1500, 136, false},
                {16, "Consume",              100, 100, 1500, 136, false},
                {17, "Ensnare",              100, 100, 1200, 138, false},
                {18, "Parasite",               0,   0,    0, 138, true},
                {19, "PsionicStorm",         200, 200, 1800, 165, false},
                {20, "Hallucination",        150, 150, 1200, 165, false},
                {21, "Recall",               150, 150, 1800, 170, false},
                {22, "StasisField",          150, 150, 1500, 170, false},
                {23, "ArchonWarp",             0,   0,    0, 165, true},
                {24, "Restoration",          100, 100, 1200, 112, false},
                {25, "DisruptionWeb",        200, 200, 1200, 169, false},
                {26, "UnusedTech26",           0,   0,    0,   0, false},
                {27, "MindControl",          200, 200, 1800, 165, false},
                {28, "DarkArchonMeld",         0,   0,    0, 165, true},
                {29, "Feedback",               0,   0,    0, 165, true},
                {30, "OpticalFlare",         100, 100, 1800, 112, false},
                {31, "Maelstrom",            100, 100, 1500, 165, false},
                {32, "LurkerAspect",         200, 200, 1800, 135, false},
                {33, "UnusedTech33",           0,   0,    0,   0, false},
                {34, "Healing",                0,   0,    0, 112, true},
                {35, "ReservedTech35",         0,   0,    0,   0, false},
                {36, "ReservedTech36",         0,   0,    0,   0, false},
                {37, "ReservedTech37",         0,   0,    0,   0, false},
                {38, "ReservedTech38",         0,   0,    0,   0, false},
                {39, "ReservedTech39",         0,   0,    0,   0, false},
                {40, "ReservedTech40",         0,   0,    0,   0, false},
                {41, "ReservedTech41",         0,   0,    0,   0, false},
                {42, "ReservedTech42",         0,   0,    0,   0, false},
                {43, "ReservedTech43",         0,   0,    0,   0, false}
            }};

            for (const auto& tech : standardTechs) {
                RegisterTech(tech);
            }

            const std::array<UpgradeDefinition, kStandardUpgradeCount> standardUpgrades = {{
                {0,  "TerranInfantryArmor",       3, 100,  75, 100,  75, 4000, 480, 122, 1},
                {1,  "TerranVehiclePlating",      3, 100,  75, 100,  75, 4000, 480, 123, 1},
                {2,  "TerranShipPlating",         3, 150,  75, 150,  75, 4000, 480, 123, 1},
                {3,  "ZergCarapace",              3, 150,  75, 150,  75, 4000, 480, 139, 1},
                {4,  "ZergFlyerCarapace",         3, 150,  75, 150,  75, 4000, 480, 141, 1},
                {5,  "ProtossGroundArmor",        3, 100,  75, 100,  75, 4000, 480, 166, 1},
                {6,  "ProtossAirArmor",           3, 150,  75, 150,  75, 4000, 480, 164, 1},
                {7,  "TerranInfantryWeapons",     3, 100,  75, 100,  75, 4000, 480, 122, 1},
                {8,  "TerranVehicleWeapons",      3, 100,  75, 100,  75, 4000, 480, 123, 1},
                {9,  "TerranShipWeapons",         3, 100,  50, 100,  50, 4000, 480, 123, 1},
                {10, "ZergMeleeAttacks",          3, 100,  50, 100,  50, 4000, 480, 139, 1},
                {11, "ZergMissileAttacks",        3, 100,  50, 100,  50, 4000, 480, 139, 1},
                {12, "ZergFlyerAttacks",          3, 100,  75, 100,  75, 4000, 480, 141, 1},
                {13, "ProtossGroundWeapons",      3, 100,  50, 100,  50, 4000, 480, 166, 1},
                {14, "ProtossAirWeapons",         3, 100,  75, 100,  75, 4000, 480, 164, 1},
                {15, "ProtossPlasmaShields",      3, 200, 100, 200, 100, 4000, 480, 166, 1},
                {16, "U238Shells",                1, 150,   0, 150,   0, 1500,   0, 112, 1},
                {17, "IonThrusters",              1, 100,   0, 100,   0, 1500,   0, 120, 1},
                {18, "BurstLasers",               1, 200,   0, 200,   0, 2500,   0, 115, 1},
                {19, "TitanReactor",              1, 150,   0, 150,   0, 2500,   0, 116, 1},
                {20, "OcularImplants",            1, 100,   0, 100,   0, 2500,   0, 117, 1},
                {21, "MoebiusReactor",            1, 150,   0, 150,   0, 2500,   0, 117, 1},
                {22, "ApolloReactor",             1, 200,   0, 200,   0, 2500,   0, 115, 1},
                {23, "ColossusReactor",           1, 150,   0, 150,   0, 2500,   0, 118, 1},
                {24, "VentralSacs",               1, 200,   0, 200,   0, 2400,   0, 132, 1},
                {25, "Antennae",                  1, 150,   0, 150,   0, 2000,   0, 132, 1},
                {26, "PneumatizedCarapace",       1, 150,   0, 150,   0, 2000,   0, 132, 1},
                {27, "MetabolicBoost",            1, 100,   0, 100,   0, 1500,   0, 142, 1},
                {28, "AdrenalGlands",             1, 200,   0, 200,   0, 1500,   0, 142, 1},
                {29, "MuscularAugments",          1, 150,   0, 150,   0, 1500,   0, 135, 1},
                {30, "GroovedSpines",             1, 150,   0, 150,   0, 1500,   0, 135, 1},
                {31, "GameteMeiosis",             1, 150,   0, 150,   0, 2500,   0, 138, 1},
                {32, "MetasynapticNode",          1, 150,   0, 150,   0, 2500,   0, 136, 1},
                {33, "SingularityCharge",         1, 150,   0, 150,   0, 2500,   0, 164, 1},
                {34, "LegEnhancements",           1, 150,   0, 150,   0, 2000,   0, 163, 1},
                {35, "ScarabDamage",              1, 200,   0, 200,   0, 2500,   0, 171, 1},
                {36, "ReaverCapacity",            1, 200,   0, 200,   0, 2500,   0, 171, 1},
                {37, "GraviticDrive",             1, 200,   0, 200,   0, 2500,   0, 171, 1},
                {38, "SensorArray",               1, 150,   0, 150,   0, 2000,   0, 159, 1},
                {39, "GraviticBoosters",          1, 150,   0, 150,   0, 2000,   0, 159, 1},
                {40, "KhaydarinAmulet",           1, 150,   0, 150,   0, 2500,   0, 165, 1},
                {41, "ApialSensors",              1, 100,   0, 100,   0, 2500,   0, 165, 1},
                {42, "GraviticThrusters",         1, 200,   0, 200,   0, 2500,   0, 169, 1},
                {43, "CarrierCapacity",           1, 100,   0, 100,   0, 1500,   0, 169, 1},
                {44, "KhaydarinCore",             1, 150,   0, 150,   0, 2500,   0, 170, 1},
                {45, "UnusedUpgrade45",           1,   0,   0,   0,   0,    0,   0,   0, 0},
                {46, "UnusedUpgrade46",           1,   0,   0,   0,   0,    0,   0,   0, 0},
                {47, "ArgusJewel",                1, 100,   0, 100,   0, 2500,   0, 169, 1},
                {48, "UnusedUpgrade48",           1,   0,   0,   0,   0,    0,   0,   0, 0},
                {49, "ArgusTalisman",             1, 150,   0, 150,   0, 2500,   0, 165, 1},
                {50, "UnusedUpgrade50",           1,   0,   0,   0,   0,    0,   0,   0, 0},
                {51, "CaduceusReactor",           1, 150,   0, 150,   0, 2500,   0, 112, 1},
                {52, "ChitinousPlating",          1, 150,   0, 150,   0, 2000,   0, 140, 2},
                {53, "AnabolicSynthesis",         1, 200,   0, 200,   0, 2000,   0, 140, 1},
                {54, "CharonBoosters",            1, 100,   0, 100,   0, 2000,   0, 120, 1},
                {55, "ReservedUpgrade55",         1,   0,   0,   0,   0,    0,   0,   0, 0},
                {56, "ReservedUpgrade56",         1,   0,   0,   0,   0,    0,   0,   0, 0},
                {57, "ReservedUpgrade57",         1,   0,   0,   0,   0,    0,   0,   0, 0},
                {58, "ReservedUpgrade58",         1,   0,   0,   0,   0,    0,   0,   0, 0},
                {59, "ReservedUpgrade59",         1,   0,   0,   0,   0,    0,   0,   0, 0},
                {60, "Upgrade60Boost",            1, 100,   0, 100,   0, 1500,   0,   0, 1}
            }};

            for (const auto& upg : standardUpgrades) {
                RegisterUpgrade(upg);
            }
        }

        bool HasTech(int playerID, uint32_t techID) const {
            if (!IsValidPlayer(playerID)) {
                return false;
            }
            const auto& unlocked = m_playerStates[playerID].UnlockedTechs;
            auto it = unlocked.find(techID);
            if (it != unlocked.end()) {
                return it->second;
            }
            const TechDefinition* def = GetTechDefinition(techID);
            return def ? def->UnlockedByDefault : false;
        }

        void GrantTech(int playerID, uint32_t techID) {
            if (!IsValidPlayer(playerID)) {
                return;
            }
            if (m_techCatalog.find(techID) != m_techCatalog.end()) {
                m_playerStates[playerID].UnlockedTechs[techID] = true;
            }
        }

        int GetUpgradeLevel(int playerID, uint32_t upgradeID) const {
            if (!IsValidPlayer(playerID)) {
                return 0;
            }
            const auto& levels = m_playerStates[playerID].UpgradeLevels;
            auto it = levels.find(upgradeID);
            return (it != levels.end()) ? it->second : 0;
        }

        void SetUpgradeLevel(int playerID, uint32_t upgradeID, int level) {
            if (!IsValidPlayer(playerID)) {
                return;
            }
            auto it = m_upgradeCatalog.find(upgradeID);
            if (it == m_upgradeCatalog.end()) {
                return;
            }
            const int clampedLevel = std::clamp(level, 0, it->second.MaxLevel);
            m_playerStates[playerID].UpgradeLevels[upgradeID] = clampedLevel;
        }

        int GetUpgradeBonus(int playerID, uint32_t upgradeID) const {
            const UpgradeDefinition* def = GetUpgradeDefinition(upgradeID);
            if (!def) {
                return 0;
            }
            return GetUpgradeLevel(playerID, upgradeID) * def->BonusPerLevel;
        }

        UpgradeCostResult EvaluateUpgradeCost(uint32_t upgradeID, int nextLevel) const {
            UpgradeCostResult result{};
            const UpgradeDefinition* def = GetUpgradeDefinition(upgradeID);
            if (!def || nextLevel < 1 || nextLevel > def->MaxLevel) {
                return result;
            }
            const int levelIndex = nextLevel - 1;
            result.Valid = true;
            result.MineralCost = def->BaseMineralCost + levelIndex * def->MineralCostFactor;
            result.GasCost = def->BaseGasCost + levelIndex * def->GasCostFactor;
            result.ResearchTicks = def->BaseResearchTicks + static_cast<uint32_t>(levelIndex) * def->ResearchTicksFactor;
            return result;
        }

        void ResetPlayerStates() {
            for (auto& p : m_playerStates) {
                p.UnlockedTechs.clear();
                p.UpgradeLevels.clear();
            }
            for (const auto& [techID, def] : m_techCatalog) {
                if (def.UnlockedByDefault) {
                    for (int p = 0; p < kMaxPlayers; ++p) {
                        m_playerStates[p].UnlockedTechs[techID] = true;
                    }
                }
            }
            m_activeQueues.clear();
        }

        bool CanResearchTech(int playerID,
                             uint32_t techID,
                             const std::vector<uint32_t>& activeBuildingUnitIDs = {}) const {
            if (!IsValidPlayer(playerID)) {
                return false;
            }
            const TechDefinition* def = GetTechDefinition(techID);
            if (!def || HasTech(playerID, techID) || IsTechBeingResearched(playerID, techID)) {
                return false;
            }
            if (!activeBuildingUnitIDs.empty() && def->RequiredBuildingUnitID != 0) {
                if (std::find(activeBuildingUnitIDs.begin(),
                              activeBuildingUnitIDs.end(),
                              def->RequiredBuildingUnitID) == activeBuildingUnitIDs.end()) {
                    return false;
                }
            }
            return true;
        }

        bool CanStartUpgrade(int playerID,
                             uint32_t upgradeID,
                             const std::vector<uint32_t>& activeBuildingUnitIDs = {}) const {
            if (!IsValidPlayer(playerID)) {
                return false;
            }
            const UpgradeDefinition* def = GetUpgradeDefinition(upgradeID);
            if (!def || IsUpgradeBeingResearched(playerID, upgradeID)) {
                return false;
            }
            const int nextLevel = GetUpgradeLevel(playerID, upgradeID) + 1;
            if (nextLevel > def->MaxLevel) {
                return false;
            }
            if (!activeBuildingUnitIDs.empty() && def->RequiredBuildingUnitID != 0) {
                if (std::find(activeBuildingUnitIDs.begin(),
                              activeBuildingUnitIDs.end(),
                              def->RequiredBuildingUnitID) == activeBuildingUnitIDs.end()) {
                    return false;
                }
            }
            return EvaluateUpgradeCost(upgradeID, nextLevel).Valid;
        }

        bool StartTechResearch(int buildingEntityID, int playerID, uint32_t techID) {
            if (buildingEntityID < 0 || !IsValidPlayer(playerID)) {
                return false;
            }
            if (m_activeQueues.find(buildingEntityID) != m_activeQueues.end()) {
                return false;
            }
            const TechDefinition* def = GetTechDefinition(techID);
            if (!def || HasTech(playerID, techID) || IsTechBeingResearched(playerID, techID)) {
                return false;
            }

            ResearchTask task{};
            task.BuildingEntityID = buildingEntityID;
            task.PlayerID = playerID;
            task.Type = ResearchTaskType::Tech;
            task.TargetID = techID;
            task.TargetLevel = 1;
            task.RemainingTicks = def->ResearchTicks;
            task.TotalTicks = def->ResearchTicks;

            m_activeQueues[buildingEntityID] = task;
            return true;
        }

        bool StartUpgradeResearch(int buildingEntityID, int playerID, uint32_t upgradeID) {
            if (buildingEntityID < 0 || !IsValidPlayer(playerID)) {
                return false;
            }
            if (m_activeQueues.find(buildingEntityID) != m_activeQueues.end()) {
                return false;
            }
            const UpgradeDefinition* def = GetUpgradeDefinition(upgradeID);
            if (!def || IsUpgradeBeingResearched(playerID, upgradeID)) {
                return false;
            }

            const int currentLevel = GetUpgradeLevel(playerID, upgradeID);
            const int nextLevel = currentLevel + 1;
            if (nextLevel > def->MaxLevel) {
                return false;
            }

            const UpgradeCostResult cost = EvaluateUpgradeCost(upgradeID, nextLevel);
            if (!cost.Valid) {
                return false;
            }

            ResearchTask task{};
            task.BuildingEntityID = buildingEntityID;
            task.PlayerID = playerID;
            task.Type = ResearchTaskType::Upgrade;
            task.TargetID = upgradeID;
            task.TargetLevel = nextLevel;
            task.RemainingTicks = cost.ResearchTicks;
            task.TotalTicks = cost.ResearchTicks;

            m_activeQueues[buildingEntityID] = task;
            return true;
        }

        bool CancelResearch(int buildingEntityID) {
            return m_activeQueues.erase(buildingEntityID) > 0;
        }

        const ResearchTask* GetActiveResearch(int buildingEntityID) const {
            auto it = m_activeQueues.find(buildingEntityID);
            return (it != m_activeQueues.end()) ? &it->second : nullptr;
        }

        std::vector<CompletedResearchEvent> StepResearchQueues(uint32_t deltaTicks) {
            std::vector<CompletedResearchEvent> completed;
            if (m_activeQueues.empty()) {
                return completed;
            }

            std::vector<int> finishedBuildings;
            for (auto& [buildingID, task] : m_activeQueues) {
                if (task.RemainingTicks <= deltaTicks) {
                    task.RemainingTicks = 0;
                    finishedBuildings.push_back(buildingID);
                } else {
                    task.RemainingTicks -= deltaTicks;
                }
            }

            std::sort(finishedBuildings.begin(), finishedBuildings.end());

            for (int buildingID : finishedBuildings) {
                auto it = m_activeQueues.find(buildingID);
                if (it == m_activeQueues.end()) {
                    continue;
                }
                const ResearchTask task = it->second;
                m_activeQueues.erase(it);

                if (task.Type == ResearchTaskType::Tech) {
                    GrantTech(task.PlayerID, task.TargetID);
                } else {
                    SetUpgradeLevel(task.PlayerID, task.TargetID, task.TargetLevel);
                }

                CompletedResearchEvent ev{};
                ev.BuildingEntityID = task.BuildingEntityID;
                ev.PlayerID = task.PlayerID;
                ev.Type = task.Type;
                ev.TargetID = task.TargetID;
                ev.NewLevel = task.TargetLevel;
                completed.push_back(ev);
            }

            return completed;
        }

    private:
        struct PlayerResearchState {
            std::unordered_map<uint32_t, bool> UnlockedTechs;
            std::unordered_map<uint32_t, int> UpgradeLevels;
        };

        static constexpr bool IsValidPlayer(int playerID) {
            return playerID >= 0 && playerID < kMaxPlayers;
        }

        bool IsTechBeingResearched(int playerID, uint32_t techID) const {
            for (const auto& [buildingID, task] : m_activeQueues) {
                if (task.PlayerID == playerID &&
                    task.Type == ResearchTaskType::Tech &&
                    task.TargetID == techID) {
                    return true;
                }
            }
            return false;
        }

        bool IsUpgradeBeingResearched(int playerID, uint32_t upgradeID) const {
            for (const auto& [buildingID, task] : m_activeQueues) {
                if (task.PlayerID == playerID &&
                    task.Type == ResearchTaskType::Upgrade &&
                    task.TargetID == upgradeID) {
                    return true;
                }
            }
            return false;
        }

        std::unordered_map<uint32_t, TechDefinition> m_techCatalog;
        std::unordered_map<uint32_t, UpgradeDefinition> m_upgradeCatalog;
        std::array<PlayerResearchState, kMaxPlayers> m_playerStates{};
        std::unordered_map<int, ResearchTask> m_activeQueues;
    };

} // namespace Engine::Gameplay
