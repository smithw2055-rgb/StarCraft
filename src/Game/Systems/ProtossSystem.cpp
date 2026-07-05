#include "Game/Systems/ProtossSystem.h"
#include "Game/Components.h"
#include "Game/Map/Map.h"
#include "Game/Races/RaceDef.h"
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
    CheckPowerStatus(EM);
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

    // Pylon provides power in a 4-tile radius = 8x8 area around its center
    // OpenBW psi_field_mask: 5x8 shape, simplified here as radius 4
    auto Units = EM.GetEntitiesWith<TransformComponent, UnitComponent>();
    for (int Eid : Units)
    {
        auto* U = EM.GetComponent<UnitComponent>(Eid);
        auto* T = EM.GetComponent<TransformComponent>(Eid);
        if (!U || !T) continue;
        if (U->ChkUnitID != 0x6A) continue; // Pylon = 0x6A
        if (U->HitPoints <= 0) continue;

        int TX = (int)(T->Position.X / 32.0f);
        int TY = (int)(T->Position.Y / 32.0f);

    // Pylon psi_field: ~5 tiles wide × 7 tiles tall, matching SC/BW shape
    // Centre tile + 4 left/right, 3 up/down (forming a wide diamond-ish area)
    for (int DY = -3; DY <= 3; DY++)
    {
        int MaxDX = 4 - (std::abs(DY) > 2 ? std::abs(DY) - 2 : 0);
        for (int DX = -MaxDX; DX <= MaxDX; DX++)
        {
            int X = TX + DX, Y = TY + DY;
            if (X >= 0 && X < W && Y >= 0 && Y < H)
                mPowerGrid[Y * W + X] = true;
        }
    }
    }

    // Update building power status
    auto Buildings = EM.GetEntitiesWith<TransformComponent, BuildingComponent>();
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
    for (int Bid : Batteries)
    {
        auto* BU = EM.GetComponent<UnitComponent>(Bid);
        auto* BT = EM.GetComponent<TransformComponent>(Bid);
        auto* BB = EM.GetComponent<BuildingComponent>(Bid);
        if (!BU || !BT || !BB) continue;
        if (BU->ChkUnitID != 0x6D) continue; // Shield Battery = 0x6D
        if (BU->EnergyPoints <= 0) continue;
        if (BB->UnderConstruction) continue;

        // Find nearby friendly units that need shield recharge
        float RechargeRate = 26.6f * DeltaTime; // ~26.6 shield per second
        float EnergyDrain = 10.0f * DeltaTime;  // energy cost per second

        auto PlayerComp = EM.GetComponent<PlayerComponent>(Bid);
        int BatteryOwner = PlayerComp ? PlayerComp->PlayerID : -1;
        if (BatteryOwner < 0) continue;

        auto AllUnits = EM.GetEntitiesWith<TransformComponent, UnitComponent>();
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

bool ProtossSystem::MorphArchon(Core::ECS::EntityManager& EM,
                                  int TemplarAID, int TemplarBID,
                                  bool IsDarkArchon)
{
    auto* UA = EM.GetComponent<UnitComponent>(TemplarAID);
    auto* UB = EM.GetComponent<UnitComponent>(TemplarBID);
    auto* PA = EM.GetComponent<PlayerComponent>(TemplarAID);
    auto* PB = EM.GetComponent<PlayerComponent>(TemplarBID);
    auto* TA = EM.GetComponent<TransformComponent>(TemplarAID);
    auto* TB = EM.GetComponent<TransformComponent>(TemplarBID);
    if (!UA || !UB || !PA || !PB || !TA || !TB) return false;
    if (UA->HitPoints <= 0 || UB->HitPoints <= 0) return false;
    if (PA->PlayerID != PB->PlayerID) return false;

    const int ArchonDefID = Game::Races::GetUnitDefID(IsDarkArchon ? "Dark Archon" : "Archon");
    const auto ArchonDef = Game::Races::GetUnitDefByID(ArchonDefID);
    if (ArchonDef.Name == "Unknown")
        return false;

    Core::Math::Vec2 CenterPos = {(TA->Position.X + TB->Position.X) * 0.5f,
                                   (TA->Position.Y + TB->Position.Y) * 0.5f};

    // Consume both templars
    EM.DestroyEntity(TemplarAID);
    EM.DestroyEntity(TemplarBID);

    // Spawn Archon
    int ArchonID = EM.CreateEntity();
    auto& ATrans = EM.AddComponent<TransformComponent>(ArchonID);
    auto& AUnit = EM.AddComponent<UnitComponent>(ArchonID);
    auto& APlayer = EM.AddComponent<PlayerComponent>(ArchonID);
    auto& ACombat = EM.AddComponent<CombatComponent>(ArchonID);
    auto& ACmd = EM.AddComponent<CommandComponent>(ArchonID);
    auto& APath = EM.AddComponent<PathfindingComponent>(ArchonID);
    auto& ARender = EM.AddComponent<RenderComponent>(ArchonID);

    ATrans.Position = CenterPos;
    APlayer = *PA;

    AUnit.UnitRace = ArchonDef.RaceType;
    AUnit.ChkUnitID = ArchonDef.ChkUnitID;
    AUnit.HitPoints = ArchonDef.HitPoints;
    AUnit.MaxHitPoints = ArchonDef.HitPoints;
    AUnit.ShieldPoints = ArchonDef.ShieldPoints;
    AUnit.MaxShieldPoints = ArchonDef.ShieldPoints;
    AUnit.EnergyPoints = ArchonDef.EnergyPoints;
    AUnit.MaxEnergyPoints = ArchonDef.EnergyPoints;
    AUnit.Armor = ArchonDef.Armor;
    AUnit.SupplyCost = ArchonDef.SupplyCost;
    AUnit.Speed = ArchonDef.Speed;
    AUnit.SightRange = ArchonDef.SightRange;
    AUnit.SizeType = ArchonDef.SizeType;

    ACombat.GroundDamage = ArchonDef.GroundDamage;
    ACombat.AirDamage = ArchonDef.AirDamage;
    ACombat.GroundDmgType = ArchonDef.GroundDmgType;
    ACombat.AirDmgType = ArchonDef.AirDmgType;
    ACombat.AttackRange = ArchonDef.AttackRange;
    ACombat.AttackCooldown = ArchonDef.AttackCooldown;
    ACombat.WeaponBeh = WeaponBehavior::AppearOnTarget;
    ACombat.CanTargetGround = ArchonDef.GroundDamage > 0;
    ACombat.CanTargetAir = ArchonDef.AirDamage > 0;
    ACombat.IsRanged = ArchonDef.AttackRange > 1.5f;

    ACmd.CurrentCommand = CommandType::Stop;
    APath.NodeTolerance = 8.0f;
    APath.IsAir = false;

    ARender.ImageID = (uint32_t)ArchonDef.ChkUnitID;
    ARender.FrameIndex = 0;
    ARender.TotalFrames = 1;
    ARender.Tint = Core::Colors::White();
    ARender.Layer = RenderLayer::Units;

    Core::Utils::Logger::Info("ProtossSystem",
        std::string(IsDarkArchon ? "Dark " : "") + "Archon spawned at (" +
        std::to_string((int)CenterPos.X) + "," + std::to_string((int)CenterPos.Y) + ")");
    return true;
}

} // namespace Game::Systems
