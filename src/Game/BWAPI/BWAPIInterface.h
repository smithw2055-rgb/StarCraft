#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <cmath>

namespace Game::BWAPI
{

enum class BW_UnitType : int
{
    Terran_Marine = 0,
    Terran_Ghost,
    Terran_Vulture,
    Terran_Goliath,
    Terran_Siege_Tank_Tank_Mode = 5,
    Terran_SCV = 7,
    Terran_Wraith,
    Terran_Science_Vessel,
    Terran_Dropship = 11,
    Terran_Battlecruiser,
    Terran_Firebat = 32,
    Terran_Medic = 34,
    Terran_Command_Center = 106,
    Terran_Comsat_Station,
    Terran_Nuclear_Silo,
    Terran_Supply_Depot,
    Terran_Refinery,
    Terran_Barracks,
    Terran_Academy,
    Terran_Factory,
    Terran_Starport,
    Terran_Control_Tower,
    Terran_Science_Facility,
    Terran_Covert_Ops,
    Terran_Physics_Lab,
    Terran_Machine_Shop = 122,
    Terran_Engineering_Bay = 124,
    Terran_Armory,
    Terran_Missile_Turret,
    Terran_Bunker,
    Zerg_Larva = 35,
    Zerg_Egg,
    Zerg_Zergling,
    Zerg_Hydralisk,
    Zerg_Ultralisk,
    Zerg_Broodling,
    Zerg_Drone,
    Zerg_Overlord,
    Zerg_Mutalisk,
    Zerg_Guardian,
    Zerg_Queen,
    Zerg_Defiler,
    Zerg_Scourge,
    Zerg_Infested_Terran = 50,
    Zerg_Cocoon = 59,
    Zerg_Devourer = 62,
    Zerg_Lurker = 103,
    Zerg_Hatchery = 131,
    Zerg_Lair,
    Zerg_Hive,
    Zerg_Nydus_Canal,
    Zerg_Hydralisk_Den,
    Zerg_Defiler_Mound,
    Zerg_Greater_Spire,
    Zerg_Queens_Nest,
    Zerg_Evolution_Chamber,
    Zerg_Ultralisk_Cavern,
    Zerg_Spire,
    Zerg_Spawning_Pool,
    Zerg_Creep_Colony,
    Zerg_Spore_Colony,
    Zerg_Sunken_Colony = 146,
    Zerg_Extractor = 149,
    Protoss_Corsair = 60,
    Protoss_Dark_Templar,
    Protoss_Dark_Archon = 63,
    Protoss_Probe = 64,
    Protoss_Zealot,
    Protoss_Dragoon,
    Protoss_High_Templar,
    Protoss_Archon,
    Protoss_Shuttle,
    Protoss_Scout,
    Protoss_Arbiter,
    Protoss_Carrier,
    Protoss_Interceptor,
    Protoss_Reaver = 83,
    Protoss_Observer,
    Protoss_Scarab,
    Protoss_Nexus = 154,
    Protoss_Robotics_Facility,
    Protoss_Pylon,
    Protoss_Assimilator,
    Protoss_Observatory = 159,
    Protoss_Gateway = 160,
    Protoss_Photon_Cannon = 162,
    Protoss_Citadel_of_Adun,
    Protoss_Cybernetics_Core,
    Protoss_Templar_Archives,
    Protoss_Forge,
    Protoss_Stargate,
    Protoss_Fleet_Beacon = 169,
    Protoss_Arbiter_Tribunal,
    Protoss_Robotics_Support_Bay,
    Protoss_Shield_Battery,
    Resource_Mineral_Field = 176,
    Resource_Vespene_Geyser = 188,
    None = 228
};

enum class BW_Player
{
    Neutral = 0,
    Player1, Player2, Player3, Player4,
    Player5, Player6, Player7, Player8,
    Player9, Player10, Player11, Player12
};

enum class BW_GameType
{
    Melee,
    FreeForAll,
    OneVsOne,
    CaptureTheFlag,
    Greed,
    Slaughter,
    SuddenDeath,
    Ladder,
    UseMapSettings,
    TeamMelee,
    TeamFreeForAll,
    TeamCaptureTheFlag,
    TopVsBottom,
    None
};

enum class BW_EventType
{
    MatchStart,
    MatchEnd,
    MatchFrame,
    UnitCreate,
    UnitDestroy,
    UnitMorph,
    UnitRenegade,
    UnitComplete,
    UnitDiscover,
    UnitEvade,
    UnitShow,
    UnitHide,
    SaveGame,
    MenuFrame,
    None
};

struct BW_Position
{
    int X = 0;
    int Y = 0;

    BW_Position() = default;
    BW_Position(int X, int Y) : X(X), Y(Y) {}

    bool operator==(const BW_Position& Other) const { return X == Other.X && Y == Other.Y; }
    int GetApproxDistance(const BW_Position& Other) const
    {
        int dx = std::abs(X - Other.X);
        int dy = std::abs(Y - Other.Y);
        return (dx > dy) ? dx : dy;
    }
};

struct BW_Unit
{
    int ID = -1;
    BW_UnitType Type = BW_UnitType::None;
    BW_Position Position;
    int HitPoints = 0;
    int Shields = 0;
    int Energy = 0;
    int PlayerID = 0;
    bool IsCompleted = false;
    bool IsVisible = false;
    bool IsSelected = false;
};

struct BW_GameInfo
{
    int MapWidth = 0;
    int MapHeight = 0;
    int FrameCount = 0;
    int ElapsedTime = 0;
    bool IsPaused = false;
    BW_GameType GameType = BW_GameType::None;
};

using BW_FrameCallback = std::function<void(BW_GameInfo&)>;
using BW_UnitCallback = std::function<void(const BW_Unit&)>;
using BW_EventCallback = std::function<void(BW_EventType, const BW_GameInfo&)>;

class BWAPIInterface
{
public:
    BWAPIInterface() = default;

    void SetFrameCallback(BW_FrameCallback Callback) { mOnFrame = std::move(Callback); }
    void SetUnitCreatedCallback(BW_UnitCallback Callback) { mOnUnitCreate = std::move(Callback); }
    void SetUnitDestroyedCallback(BW_UnitCallback Callback) { mOnUnitDestroy = std::move(Callback); }
    void SetEventCallback(BW_EventCallback Callback) { mOnEvent = std::move(Callback); }

    void OnGameStart() { mGameInfo.FrameCount = 0; mGameInfo.ElapsedTime = 0; }
    void OnGameEnd() { if (mOnEvent) mOnEvent(BW_EventType::MatchEnd, mGameInfo); }
    void OnFrame()
    {
        ++mGameInfo.FrameCount;
        mGameInfo.ElapsedTime = mGameInfo.FrameCount / 24;
        if (mOnFrame) mOnFrame(mGameInfo);
    }

    void RegisterUnit(const BW_Unit& Unit)
    {
        mUnits[Unit.ID] = Unit;
        if (mOnUnitCreate) mOnUnitCreate(Unit);
    }

    void UnregisterUnit(int UnitID)
    {
        auto it = mUnits.find(UnitID);
        if (it != mUnits.end())
        {
            if (mOnUnitDestroy) mOnUnitDestroy(it->second);
            mUnits.erase(it);
        }
    }

    void UpdateUnit(const BW_Unit& Unit)
    {
        mUnits[Unit.ID] = Unit;
    }

    const BW_Unit* GetUnit(int UnitID) const
    {
        auto it = mUnits.find(UnitID);
        return (it != mUnits.end()) ? &it->second : nullptr;
    }

    std::vector<const BW_Unit*> GetUnitsInRectangle(int Left, int Top, int Right, int Bottom) const
    {
        std::vector<const BW_Unit*> result;
        for (const auto& [id, unit] : mUnits)
            if (unit.Position.X >= Left && unit.Position.X <= Right &&
                unit.Position.Y >= Top && unit.Position.Y <= Bottom)
                result.push_back(&unit);
        return result;
    }

    BW_GameInfo& GetGameInfo() { return mGameInfo; }
    const BW_GameInfo& GetGameInfo() const { return mGameInfo; }

    // --- AI Module Interface ---
    struct AIModule
    {
        virtual ~AIModule() = default;
        virtual void OnStart() {}
        virtual void OnEnd(bool IsWinner) { (void)IsWinner; }
        virtual void OnFrame() {}
        virtual void OnUnitCreate(const BW_Unit&) {}
        virtual void OnUnitDestroy(const BW_Unit&) {}
        virtual void OnSendText(const std::string&) {}
        virtual void OnReceiveText(int, const std::string&) {}
    };

    void SetAIModule(AIModule* Module) { mAI = Module; }
    AIModule* GetAIModule() { return mAI; }

private:
    BW_GameInfo mGameInfo;
    std::unordered_map<int, BW_Unit> mUnits;
    BW_FrameCallback mOnFrame;
    BW_UnitCallback mOnUnitCreate;
    BW_UnitCallback mOnUnitDestroy;
    BW_EventCallback mOnEvent;
    AIModule* mAI = nullptr;
};

} // namespace Game::BWAPI
