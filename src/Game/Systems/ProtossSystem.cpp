#include "Game/Systems/ProtossSystem.h"
#include "Game/Components.h"
#include "Game/Map/Map.h"
#include "Game/ProtossPower.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/ProductionSystem.h"
#include "Core/Utils/Logger.h"

namespace Game::Systems
{

using namespace Game::Components;
using Core::Utils::Logger;

Game::Map::GameMap* ProtossSystem::sMap = nullptr;

ProtossSystem::ProtossSystem(Core::ECS::SystemManager& SysMgr, Game::Map::GameMap* Map)
    : mSystemMgr(SysMgr)
{
    sMap = Map;
}

void ProtossSystem::SetMap(Game::Map::GameMap* Map)
{
    sMap = Map;
}

bool ProtossSystem::HasPower(int TileX, int TileY)
{
    if (!sMap) return false;
    if (TileX < 0 || TileX >= sMap->GetWidth() ||
        TileY < 0 || TileY >= sMap->GetHeight())
        return false;

    // Check the power grid cache
    if (TileX >= 0 && TileX < mPowerGridW && TileY >= 0 && TileY < mPowerGridH)
        return mPowerGrid[TileY * mPowerGridW + TileX];

    return false;
}

void ProtossSystem::Update(float DeltaTime)
{
    if (DeltaTime <= 0.0f) return;
    auto& EM = mSystemMgr.GetEntityManager();
    UpdatePowerGrid(EM);
    RechargeShields(EM, DeltaTime);
    UpdateArchonWarps(EM, DeltaTime);
}

void ProtossSystem::CheckPowerStatus(Core::ECS::EntityManager& EM)
{
    auto Buildings = EM.GetEntitiesWith<TransformComponent, BuildingComponent, UnitComponent>();
    for (int Eid : Buildings)
    {
        auto* B = EM.GetComponent<BuildingComponent>(Eid);
        auto* T = EM.GetComponent<TransformComponent>(Eid);
        auto* U = EM.GetComponent<UnitComponent>(Eid);
        if (!B || !T || !U) continue;
        if (U->UnitRace != Race::Protoss) continue;
        if (!B->RequiresPylon) continue;

        int TX = (int)(T->Position.X / 32.0f);
        int TY = (int)(T->Position.Y / 32.0f);
        B->HasPylonPower = HasPower(TX, TY);
    }
}

void ProtossSystem::UpdatePowerGrid(Core::ECS::EntityManager& EM)
{
    if (!sMap) return;
    int W = sMap->GetWidth();
    int H = sMap->GetHeight();

    // Initialize/reinit power grid cache
    if ((int)mPowerGrid.size() != W * H)
    {
        mPowerGrid.resize(W * H, false);
        mPowerGridW = W;
        mPowerGridH = H;
    }

    std::fill(mPowerGrid.begin(), mPowerGrid.end(), false);

    // Pylon power is a four-tile circle around its center.
    constexpr int kPylonChkID = 156;
    auto Buildings = EM.GetEntitiesWith<TransformComponent, BuildingComponent>();
    for (int Eid : Buildings)
    {
        auto* U = EM.GetComponent<UnitComponent>(Eid);
        auto* T = EM.GetComponent<TransformComponent>(Eid);
        auto* PB = EM.GetComponent<BuildingComponent>(Eid);
        if (!U || !T || !PB) continue;
        if (U->ChkUnitID != kPylonChkID) continue;
        if (U->HitPoints <= 0) continue;
        if (PB->UnderConstruction)
            continue;

        int TX = (int)(T->Position.X / 32.0f);
        int TY = (int)(T->Position.Y / 32.0f);

        for (int DY = -Game::ProtossPower::PylonRadiusTiles;
             DY <= Game::ProtossPower::PylonRadiusTiles; ++DY)
        {
            for (int DX = -Game::ProtossPower::PylonRadiusTiles;
                 DX <= Game::ProtossPower::PylonRadiusTiles; ++DX)
            {
                if (!Game::ProtossPower::ContainsTileOffset(DX, DY))
                    continue;
                int X = TX + DX, Y = TY + DY;
                if (X >= 0 && X < W && Y >= 0 && Y < H)
                    mPowerGrid[Y * W + X] = true;
            }
        }
    }

    // Update building power status
    for (int Eid : Buildings)
    {
        auto* B = EM.GetComponent<BuildingComponent>(Eid);
        auto* T = EM.GetComponent<TransformComponent>(Eid);
        if (!B || !T || !B->RequiresPylon) continue;

        int TX = (int)(T->Position.X / 32.0f);
        int TY = (int)(T->Position.Y / 32.0f);
        B->HasPylonPower = HasPower(TX, TY);
    }
}

void ProtossSystem::RechargeShields(Core::ECS::EntityManager& EM, float DeltaTime)
{
    // Shield Battery: consume energy to recharge nearby units
    auto Batteries = EM.GetEntitiesWith<TransformComponent, UnitComponent, BuildingComponent>();
    if (Batteries.empty())
        return;

    std::vector<int> AllUnits;
    bool QueriedUnits = false;

    for (int Bid : Batteries)
    {
        auto* BU = EM.GetComponent<UnitComponent>(Bid);
        auto* BT = EM.GetComponent<TransformComponent>(Bid);
        auto* BB = EM.GetComponent<BuildingComponent>(Bid);
        if (!BU || !BT || !BB) continue;
        if (BU->ChkUnitID != 172) continue; // Protoss Shield Battery
        if (BU->EnergyPoints <= 0) continue;
        if (BB->UnderConstruction || (BB->RequiresPylon && !BB->HasPylonPower)) continue;

        // Find nearby friendly units that need shield recharge
        float RechargeRate = 26.6f * DeltaTime; // ~26.6 shield per second
        float EnergyDrain = 10.0f * DeltaTime;  // energy cost per second

        auto PlayerComp = EM.GetComponent<PlayerComponent>(Bid);
        int BatteryOwner = PlayerComp ? PlayerComp->PlayerID : -1;
        if (BatteryOwner < 0) continue;

        if (!QueriedUnits)
        {
            AllUnits = EM.GetEntitiesWith<TransformComponent, UnitComponent>();
            QueriedUnits = true;
        }

        for (int Uid : AllUnits)
        {
            if (Uid == Bid) continue;
            if (BU->EnergyPoints <= 0) break;

            auto* TU = EM.GetComponent<UnitComponent>(Uid);
            auto* TT = EM.GetComponent<TransformComponent>(Uid);
            auto* TP = EM.GetComponent<PlayerComponent>(Uid);
            if (!TU || !TT || !TP) continue;
            if (TP->PlayerID != BatteryOwner) continue;
            if (TU->ShieldPoints >= TU->MaxShieldPoints) continue;
            if (TU->MaxShieldPoints <= 0) continue;

            float Dist = BT->Position.DistanceTo(TT->Position);
            if (Dist > 128.0f) continue; // ~4 tile range

            // Recharge shields
            int RechargeAmount = (int)std::min(RechargeRate, (float)(TU->MaxShieldPoints - TU->ShieldPoints));
            TU->ShieldPoints = std::min(TU->MaxShieldPoints, TU->ShieldPoints + RechargeAmount);
            BU->EnergyPoints = std::max(0, BU->EnergyPoints - (int)EnergyDrain);
        }
    }
}

bool ProtossSystem::CanRechargeShieldsTarget(Core::ECS::EntityManager& EM,
                                             int BatteryID,
                                             int TargetID)
{
    auto* BatteryUnit = EM.GetComponent<UnitComponent>(BatteryID);
    auto* BatteryBuilding = EM.GetComponent<BuildingComponent>(BatteryID);
    auto* BatteryPlayer = EM.GetComponent<PlayerComponent>(BatteryID);
    auto* BatteryTransform = EM.GetComponent<TransformComponent>(BatteryID);
    auto* TargetUnit = EM.GetComponent<UnitComponent>(TargetID);
    auto* TargetPlayer = EM.GetComponent<PlayerComponent>(TargetID);
    auto* TargetTransform = EM.GetComponent<TransformComponent>(TargetID);
    return BatteryUnit && BatteryBuilding && BatteryPlayer && BatteryTransform &&
           TargetUnit && TargetPlayer && TargetTransform &&
           BatteryUnit->ChkUnitID == 172 && !BatteryBuilding->UnderConstruction &&
           (!BatteryBuilding->RequiresPylon || BatteryBuilding->HasPylonPower) &&
           BatteryUnit->EnergyPoints > 0 &&
           BatteryPlayer->PlayerID == TargetPlayer->PlayerID &&
           TargetUnit->MaxShieldPoints > 0 &&
           TargetUnit->ShieldPoints < TargetUnit->MaxShieldPoints &&
           BatteryTransform->Position.DistanceTo(TargetTransform->Position) <= 128.0f;
}

bool ProtossSystem::RechargeShieldsTarget(Core::ECS::EntityManager& EM,
                                          int BatteryID,
                                          int TargetID)
{
    if (!CanRechargeShieldsTarget(EM, BatteryID, TargetID))
        return false;

    auto* BatteryUnit = EM.GetComponent<UnitComponent>(BatteryID);
    auto* TargetUnit = EM.GetComponent<UnitComponent>(TargetID);
    const int MissingShields = TargetUnit->MaxShieldPoints - TargetUnit->ShieldPoints;
    const int Restored = std::min(MissingShields, BatteryUnit->EnergyPoints * 2);
    const int EnergySpent = (Restored + 1) / 2;
    TargetUnit->ShieldPoints += Restored;
    BatteryUnit->EnergyPoints -= EnergySpent;
    return Restored > 0;
}

bool ProtossSystem::MorphArchon(Core::ECS::EntityManager& EM,
                                  int TemplarAID, int TemplarBID,
                                  bool IsDarkArchon,
                                  bool InitialShieldCharge)
{
    if (TemplarAID == TemplarBID)
        return false;

    auto* UA = EM.GetComponent<UnitComponent>(TemplarAID);
    auto* UB = EM.GetComponent<UnitComponent>(TemplarBID);
    auto* PA = EM.GetComponent<PlayerComponent>(TemplarAID);
    auto* PB = EM.GetComponent<PlayerComponent>(TemplarBID);
    auto* TA = EM.GetComponent<TransformComponent>(TemplarAID);
    auto* TB = EM.GetComponent<TransformComponent>(TemplarBID);
    if (!UA || !UB || !PA || !PB || !TA || !TB) return false;
    if (UA->HitPoints <= 0 || UB->HitPoints <= 0) return false;
    if (PA->PlayerID != PB->PlayerID) return false;

    const int RequiredTemplarDefID =
        Game::Races::GetUnitDefID(IsDarkArchon ? "Dark Templar" : "High Templar");
    const auto RequiredTemplarDef = Game::Races::GetUnitDefByID(RequiredTemplarDefID);
    if (RequiredTemplarDef.Name == "Unknown" ||
        UA->ChkUnitID != RequiredTemplarDef.ChkUnitID ||
        UB->ChkUnitID != RequiredTemplarDef.ChkUnitID)
    {
        return false;
    }

    const int ArchonDefID = Game::Races::GetUnitDefID(IsDarkArchon ? "Dark Archon" : "Archon");
    const auto ArchonDef = Game::Races::GetUnitDefByID(ArchonDefID);
    if (ArchonDef.Name == "Unknown")
        return false;

    Core::Math::Vec2 CenterPos = {(TA->Position.X + TB->Position.X) * 0.5f,
                                   (TA->Position.Y + TB->Position.Y) * 0.5f};
    const int PlayerID = PA->PlayerID;

    const int ArchonID = ProductionSystem::DebugSpawnUnit(
        EM, ArchonDefID, CenterPos, PlayerID);
    if (ArchonID < 0)
        return false;

    if (InitialShieldCharge)
    {
        if (auto* ArchonUnit = EM.GetComponent<UnitComponent>(ArchonID))
        {
            // In Brood War, newly morphed Archons start with low shields (10) and charge up
            ArchonUnit->ShieldPoints = 10;
            ArchonUnit->ShieldSubDamage = 0;
            ArchonUnit->ShieldRegenAccumulator = 0.0f;
        }
    }

    EM.RemoveComponent<ArchonWarpComponent>(TemplarAID);
    EM.RemoveComponent<ArchonWarpComponent>(TemplarBID);

    // Consume the source units only after the replacement entity is complete.
    EM.DestroyEntity(TemplarAID);
    EM.DestroyEntity(TemplarBID);

    Core::Utils::Logger::Info("ProtossSystem",
        std::string(IsDarkArchon ? "Dark " : "") + "Archon spawned at (" +
        std::to_string((int)CenterPos.X) + "," + std::to_string((int)CenterPos.Y) + ")");
    return true;
}

bool ProtossSystem::StartArchonWarp(Core::ECS::EntityManager& EM,
                                    int TemplarAID, int TemplarBID,
                                    bool IsDarkArchon,
                                    float Duration,
                                    bool InitialShieldCharge)
{
    if (TemplarAID == TemplarBID)
        return false;

    auto* UA = EM.GetComponent<UnitComponent>(TemplarAID);
    auto* UB = EM.GetComponent<UnitComponent>(TemplarBID);
    auto* PA = EM.GetComponent<PlayerComponent>(TemplarAID);
    auto* PB = EM.GetComponent<PlayerComponent>(TemplarBID);
    auto* TA = EM.GetComponent<TransformComponent>(TemplarAID);
    auto* TB = EM.GetComponent<TransformComponent>(TemplarBID);
    if (!UA || !UB || !PA || !PB || !TA || !TB) return false;
    if (UA->HitPoints <= 0 || UB->HitPoints <= 0) return false;
    if (PA->PlayerID != PB->PlayerID) return false;

    const int RequiredTemplarDefID =
        Game::Races::GetUnitDefID(IsDarkArchon ? "Dark Templar" : "High Templar");
    const auto RequiredTemplarDef = Game::Races::GetUnitDefByID(RequiredTemplarDefID);
    if (RequiredTemplarDef.Name == "Unknown" ||
        UA->ChkUnitID != RequiredTemplarDef.ChkUnitID ||
        UB->ChkUnitID != RequiredTemplarDef.ChkUnitID)
    {
        return false;
    }

    if (auto* CmdA = EM.GetComponent<CommandComponent>(TemplarAID))
    {
        CmdA->CancelAll();
        CmdA->CurrentCommand = CommandType::Stop;
    }
    if (auto* CmdB = EM.GetComponent<CommandComponent>(TemplarBID))
    {
        CmdB->CancelAll();
        CmdB->CurrentCommand = CommandType::Stop;
    }

    auto& WarpA = EM.AddComponent<ArchonWarpComponent>(TemplarAID);
    WarpA.PartnerEntityID = TemplarBID;
    WarpA.IsDarkArchon = IsDarkArchon;
    WarpA.WarpTimer = Duration;
    WarpA.WarpDuration = Duration;
    WarpA.IsWarping = true;
    WarpA.InitialShieldCharge = InitialShieldCharge;

    auto& WarpB = EM.AddComponent<ArchonWarpComponent>(TemplarBID);
    WarpB.PartnerEntityID = TemplarAID;
    WarpB.IsDarkArchon = IsDarkArchon;
    WarpB.WarpTimer = Duration;
    WarpB.WarpDuration = Duration;
    WarpB.IsWarping = true;
    WarpB.InitialShieldCharge = InitialShieldCharge;

    Core::Utils::Logger::Info("ProtossSystem",
        std::string(IsDarkArchon ? "Dark " : "") + "Archon warp channeling started between " +
        std::to_string(TemplarAID) + " and " + std::to_string(TemplarBID));
    return true;
}

void ProtossSystem::UpdateArchonWarps(Core::ECS::EntityManager& EM, float DeltaTime)
{
    auto WarpingEntities = EM.GetEntitiesWith<ArchonWarpComponent>();
    std::vector<int> Handled;
    for (int Eid : WarpingEntities)
    {
        if (std::find(Handled.begin(), Handled.end(), Eid) != Handled.end())
            continue;
        auto* Warp = EM.GetComponent<ArchonWarpComponent>(Eid);
        if (!Warp || !Warp->IsWarping)
            continue;

        const int PartnerID = Warp->PartnerEntityID;
        Handled.push_back(Eid);
        Handled.push_back(PartnerID);

        if (!EM.IsAlive(PartnerID))
        {
            EM.RemoveComponent<ArchonWarpComponent>(Eid);
            continue;
        }

        Warp->WarpTimer -= DeltaTime;
        if (auto* PartnerWarp = EM.GetComponent<ArchonWarpComponent>(PartnerID))
            PartnerWarp->WarpTimer = Warp->WarpTimer;

        if (Warp->WarpTimer <= 0.0f)
        {
            const bool IsDarkArchon = Warp->IsDarkArchon;
            const bool InitialShieldCharge = Warp->InitialShieldCharge;
            MorphArchon(EM, Eid, PartnerID, IsDarkArchon, InitialShieldCharge);
        }
    }
}

} // namespace Game::Systems
