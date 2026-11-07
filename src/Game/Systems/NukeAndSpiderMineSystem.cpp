#include "Game/Systems/NukeAndSpiderMineSystem.h"
#include "Game/Pathfinding/WorldSpatialIndexSystem.h"
#include "Game/Systems/BulletSystem.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Game/Map/Map.h"
#include "Game/IScriptVM.h"
#include "Game/DataEnums.h"

#include <cmath>
#include <limits>
#include <utility>

namespace Game::Systems
{

namespace
{
    NukeAndSpiderMineSystem::ImageIDResolver gImageResolver;
    NukeAndSpiderMineSystem* gActiveNukeAndSpiderMineSystem = nullptr;
    constexpr int kTerranSpiderMineChkUnitID = 13;
    constexpr int kTerranVultureChkUnitID = 2;
    constexpr int kTerranHeroVultureChkUnitID = 19;
    constexpr float kSpiderMineExplosionRadius = 64.0f;
    constexpr int kSpiderMineDamage = 125;

    bool IsValidSpiderMineTriggerTarget(Core::ECS::EntityManager& EM,
                                        int EntityID,
                                        int MinePlayerID)
    {
        if (!EM.IsAlive(EntityID))
            return false;

        auto* Unit = EM.GetComponent<Components::UnitComponent>(EntityID);
        auto* Trans = EM.GetComponent<Components::TransformComponent>(EntityID);
        auto* Player = EM.GetComponent<Components::PlayerComponent>(EntityID);
        if (!Unit || !Trans || !Player || Unit->HitPoints <= 0)
            return false;

        // Enemy check
        if (Player->PlayerID == MinePlayerID)
            return false;

        // Cannot target units inside transports
        if (Unit->LoadedInTransportID >= 0)
            return false;

        // Cannot target air units
        auto* Path = EM.GetComponent<Components::PathfindingComponent>(EntityID);
        if ((Path && Path->IsAir) || Game::Data::IsAirUnitType(Unit->ChkUnitID))
            return false;

        // Cannot target buildings
        if (EM.GetComponent<Components::BuildingComponent>(EntityID))
            return false;

        // Cannot target resources
        if (EM.GetComponent<Components::ResourceComponent>(EntityID))
            return false;

        // Hover units do not trigger spider mines:
        // Terran Vulture (2, 19), Protoss Archon (68, 76), Dark Archon (63),
        // SCV (7), Drone (41), Probe (64)
        if (Unit->ChkUnitID == 2 || Unit->ChkUnitID == 19 ||
            Unit->ChkUnitID == 68 || Unit->ChkUnitID == 76 ||
            Unit->ChkUnitID == 63 ||
            Unit->ChkUnitID == 7 || Unit->ChkUnitID == 41 || Unit->ChkUnitID == 64)
        {
            return false;
        }

        // Spider mines do not trigger on other spider mines
        if (Unit->ChkUnitID == kTerranSpiderMineChkUnitID)
            return false;

        // Larva (35), Egg (36), Lurker Egg (97) do not trigger mines
        if (Unit->ChkUnitID == 35 || Unit->ChkUnitID == 36 || Unit->ChkUnitID == 97)
            return false;

        // Undetected cloaked or burrowed units do not trigger mines
        auto* Cloak = EM.GetComponent<Components::CloakComponent>(EntityID);
        if (Cloak && (Cloak->IsCloaked || Cloak->IsBurrowed))
            return false;

        return true;
    }
}

NukeAndSpiderMineSystem::NukeAndSpiderMineSystem(
    Core::ECS::SystemManager& SysMgr,
    BulletSystem& BulletSys)
    : mSystemMgr(SysMgr), mBulletSystem(BulletSys)
{
    gActiveNukeAndSpiderMineSystem = this;
}

NukeAndSpiderMineSystem::~NukeAndSpiderMineSystem()
{
    if (gActiveNukeAndSpiderMineSystem == this)
        gActiveNukeAndSpiderMineSystem = nullptr;
}

void NukeAndSpiderMineSystem::SetImageResolver(ImageIDResolver Resolver)
{
    gImageResolver = std::move(Resolver);
}

bool NukeAndSpiderMineSystem::PlaceSpiderMineForActiveSession(
    int VultureEntityID)
{
    return gActiveNukeAndSpiderMineSystem &&
           gActiveNukeAndSpiderMineSystem->PlaceSpiderMine(VultureEntityID);
}

void NukeAndSpiderMineSystem::Update(float DeltaTime)
{
    UpdateNuke(DeltaTime);
    UpdateSpiderMines(DeltaTime);
}

void NukeAndSpiderMineSystem::ArmNuke(int GhostEntityID, int SiloEntityID, const Core::Math::Vec2& TargetPos)
{
    mNukeGhostEntityID = GhostEntityID;
    mNukeSiloEntityID = SiloEntityID;
    mNukeTargetPosition = TargetPos;
    mNukeArmTimer = 2.0f;
    mNukeFlightTimer = 0.0f;
    mNukeArmed = false;
    mNukeLaunched = false;
    mNukeDetonated = false;
    mNukeHasNukeDot = false;
    mNukeDotEntityID = -1;
}

void NukeAndSpiderMineSystem::CancelNuke(int EntityID)
{
    (void)EntityID;
    mNukeArmed = false;
    mNukeLaunched = false;
    mNukeDetonated = false;
    mNukeHasNukeDot = false;
    if (mNukeDotEntityID >= 0)
    {
        auto& EM = mSystemMgr.GetEntityManager();
        EM.DestroyEntity(mNukeDotEntityID);
        mNukeDotEntityID = -1;
    }
}

bool NukeAndSpiderMineSystem::PlaceSpiderMine(int VultureEntityID)
{
    auto& EM = mSystemMgr.GetEntityManager();
    auto* Trans =
        EM.GetComponent<Components::TransformComponent>(VultureEntityID);
    auto* Unit = EM.GetComponent<Components::UnitComponent>(VultureEntityID);
    auto* Player =
        EM.GetComponent<Components::PlayerComponent>(VultureEntityID);
    if (!Trans || !Unit || !Player ||
        (Unit->ChkUnitID != kTerranVultureChkUnitID &&
         Unit->ChkUnitID != kTerranHeroVultureChkUnitID) ||
        !UpgradeSystem::HasResearch(
            Player->PlayerID, ResearchId::Terran_SpiderMines))
    {
        return false;
    }

    auto* Inventory =
        EM.GetComponent<Components::VultureMineInventoryComponent>(
            VultureEntityID);
    if (!Inventory)
    {
        Inventory =
            &EM.AddComponent<Components::VultureMineInventoryComponent>(
                VultureEntityID);
    }
    if (Inventory->RemainingMines <= 0)
        return false;
    --Inventory->RemainingMines;

    if (auto* Path =
            EM.GetComponent<Components::PathfindingComponent>(VultureEntityID))
    {
        Path->Path.clear();
        Path->PathIndex = 0;
        Path->CurrentSpeed = 0.0f;
        Path->CurrentVelocity = {0.0f, 0.0f};
    }

    SpiderMineState mine;
    mine.VultureEntityID = VultureEntityID;
    mine.PlacementPosition = Trans->Position;
    mine.BurrowTimer = 1.0f;
    mine.Burrowed = false;
    mMines.push_back(mine);
    return true;
}

bool NukeAndSpiderMineSystem::IsSpiderMineTriggered(int MineEntityID) const
{
    for (const auto& mine : mMines)
        if (mine.MineEntityID == MineEntityID)
            return mine.Burrowed;
    return false;
}

void NukeAndSpiderMineSystem::TriggerSpiderMine(int MineEntityID, int TargetEntityID)
{
    auto& EM = mSystemMgr.GetEntityManager();
    auto* mineComp = EM.GetComponent<Components::SpiderMineComponent>(MineEntityID);
    if (!mineComp) return;

    mineComp->IsTriggered = true;
    mineComp->ChasingEntityID = TargetEntityID;
}

void NukeAndSpiderMineSystem::UpdateNuke(float DeltaTime)
{
    if (mNukeGhostEntityID >= 0)
    {
        auto& EM = mSystemMgr.GetEntityManager();
        auto* GhostUnit = EM.GetComponent<Components::UnitComponent>(mNukeGhostEntityID);
        if (!GhostUnit || GhostUnit->HitPoints <= 0)
        {
            CancelNuke(mNukeGhostEntityID);
            return;
        }
    }

    if (!mNukeArmed)
    {
        mNukeArmTimer -= DeltaTime;
        if (mNukeArmTimer <= 0.0f)
        {
            mNukeArmed = true;
            mNukeLaunched = true;
            mNukeFlightTimer = 3.0f;
        }
        return;
    }

    if (mNukeLaunched && !mNukeDetonated)
    {
        mNukeFlightTimer -= DeltaTime;

        if (mNukeFlightTimer < 2.0f && !mNukeHasNukeDot)
        {
            mNukeHasNukeDot = true;
            auto& EM = mSystemMgr.GetEntityManager();
            int dotID = EM.CreateEntity();
            EM.AddComponent<Components::TransformComponent>(dotID).Position = mNukeTargetPosition;
            auto& dotRender = EM.AddComponent<Components::RenderComponent>(dotID);
            dotRender.Layer = Components::RenderLayer::Effects;
            dotRender.Visible = true;
            dotRender.Tint = Core::Color(255, 0, 0, 204);
            mNukeDotEntityID = dotID;
        }

        if (mNukeFlightTimer <= 0.0f)
        {
            mNukeDetonated = true;
            DetonateNuke();

            if (mNukeDotEntityID >= 0)
            {
                mSystemMgr.GetEntityManager().DestroyEntity(mNukeDotEntityID);
                mNukeDotEntityID = -1;
            }
        }
    }
}

void NukeAndSpiderMineSystem::DetonateNuke()
{
    auto& EM = mSystemMgr.GetEntityManager();
    const int nukeRadius = 128;
    const int nukeDamage = 500;

    Core::Math::Vec2 center = mNukeTargetPosition;

    auto entities = EM.GetEntitiesWith<
        Components::TransformComponent,
        Components::UnitComponent>();

    for (int entityID : entities)
    {
        auto* transform = EM.GetComponent<Components::TransformComponent>(entityID);
        auto* unit = EM.GetComponent<Components::UnitComponent>(entityID);
        if (!transform || !unit || unit->HitPoints <= 0) continue;

        float dx = transform->Position.X - center.X;
        float dy = transform->Position.Y - center.Y;
        float dist = std::sqrt(dx * dx + dy * dy);

        if (dist < (float)nukeRadius)
        {
            float factor = 1.0f - (dist / (float)nukeRadius);
            int damage = (int)((float)nukeDamage * factor);
            if (damage < nukeDamage / 4) damage = nukeDamage / 4;

            Components::CombatComponent dummyCombat;
            dummyCombat.GroundDamage = damage;
            dummyCombat.GroundDmgType = Components::DamageType::Normal;
            CombatSystem::ApplyDamage(EM, entityID, dummyCombat, mNukeGhostEntityID, damage);
        }
    }

    // Create explosion visual
    int explosionID = EM.CreateEntity();
    EM.AddComponent<Components::TransformComponent>(explosionID).Position = center;
    auto& expRender = EM.AddComponent<Components::RenderComponent>(explosionID);
    expRender.Layer = Components::RenderLayer::Effects;
    expRender.Visible = true;
    expRender.ZOrder = 1000;
}

void NukeAndSpiderMineSystem::UpdateSpiderMines(float DeltaTime)
{
    auto& EM = mSystemMgr.GetEntityManager();

    for (auto it = mMines.begin(); it != mMines.end(); )
    {
        if (!it->Burrowed)
        {
            it->BurrowTimer -= DeltaTime;
            if (it->BurrowTimer <= 0.0f)
            {
                it->Burrowed = true;
                it->MineEntityID = EM.CreateEntity();
                EM.AddComponent<Components::TransformComponent>(it->MineEntityID).Position = it->PlacementPosition;
                auto& render = EM.AddComponent<Components::RenderComponent>(it->MineEntityID);
                render.ImageID = gImageResolver
                    ? gImageResolver(kTerranSpiderMineChkUnitID)
                    : std::numeric_limits<uint32_t>::max();
                render.FrameIndex = 0;
                render.TotalFrames = 1;
                render.Layer = Components::RenderLayer::GroundDecor;
                render.Visible = false;

                // EntityRenderer deliberately accepts only game units. The old
                // mine entity had only a RenderComponent, so it became armed
                // but was never submitted to the sprite pass.
                auto& unit = EM.AddComponent<Components::UnitComponent>(it->MineEntityID);
                unit.ChkUnitID = kTerranSpiderMineChkUnitID;
                unit.UnitRace = Components::Race::Terran;
                unit.SizeType = Components::UnitSize::Small;
                unit.HitPoints = 20;
                unit.MaxHitPoints = 20;
                unit.Speed = 0.0f;

                auto* vulturePlayer = EM.GetComponent<Components::PlayerComponent>(it->VultureEntityID);
                if (vulturePlayer)
                    EM.AddComponent<Components::PlayerComponent>(it->MineEntityID).PlayerID = vulturePlayer->PlayerID;
                else
                    EM.AddComponent<Components::PlayerComponent>(it->MineEntityID).PlayerID = 0;

                auto& spider = EM.AddComponent<Components::SpiderMineComponent>(it->MineEntityID);
                spider.IsPlaced = true;
                spider.IsBurrowed = false;
                spider.BurrowTimer = 1.0f;
                spider.VultureEntityID = it->VultureEntityID;
            }
        }
        else if (it->MineEntityID >= 0)
        {
            if (!EM.IsAlive(it->MineEntityID))
            {
                it = mMines.erase(it);
                continue;
            }

            auto* mineUnit = EM.GetComponent<Components::UnitComponent>(it->MineEntityID);
            if (!mineUnit || mineUnit->HitPoints <= 0)
            {
                EM.DestroyEntity(it->MineEntityID);
                it = mMines.erase(it);
                continue;
            }

            auto* spiderComp = EM.GetComponent<Components::SpiderMineComponent>(it->MineEntityID);
            auto* render = EM.GetComponent<Components::RenderComponent>(it->MineEntityID);

            if (spiderComp && !spiderComp->IsBurrowed)
            {
                spiderComp->BurrowTimer -= DeltaTime;
                if (spiderComp->BurrowTimer <= 0.0f)
                {
                    spiderComp->IsBurrowed = true;
                    auto& cloak = EM.AddComponent<Components::CloakComponent>(it->MineEntityID);
                    cloak.IsBurrowed = true;
                    cloak.IsCloaked = true;
                    cloak.IsPassiveCloak = true;
                    if (render) render->Visible = true;
                }
            }

            if (spiderComp && spiderComp->IsBurrowed && !spiderComp->IsTriggered)
            {
                if (CheckSpiderMineTrigger(it->MineEntityID))
                {
                    spiderComp->IsTriggered = true;
                    if (auto* cloak = EM.GetComponent<Components::CloakComponent>(it->MineEntityID))
                    {
                        cloak->IsBurrowed = false;
                        cloak->IsCloaked = false;
                    }
                    if (render) render->Visible = true;
                }
            }

            if (spiderComp && spiderComp->IsTriggered)
            {
                auto* trans = EM.GetComponent<Components::TransformComponent>(it->MineEntityID);
                auto* targetTrans = EM.GetComponent<Components::TransformComponent>(spiderComp->ChasingEntityID);
                auto* targetUnit = EM.GetComponent<Components::UnitComponent>(spiderComp->ChasingEntityID);

                if (trans)
                {
                    bool TargetLost = !targetTrans || !targetUnit || targetUnit->HitPoints <= 0;
                    if (!TargetLost && targetUnit->LoadedInTransportID >= 0)
                    {
                        TargetLost = true;
                    }

                    float speed = spiderComp->ChaseSpeed * DeltaTime;
                    float dx = targetTrans ? (targetTrans->Position.X - trans->Position.X) : 0.0f;
                    float dy = targetTrans ? (targetTrans->Position.Y - trans->Position.Y) : 0.0f;
                    float dist = std::sqrt(dx * dx + dy * dy);

                    if (TargetLost || dist <= std::max(12.0f, speed * 1.2f))
                    {
                        Components::CombatComponent combat;
                        combat.GroundDamage = kSpiderMineDamage;
                        combat.GroundDmgType = Components::DamageType::Explosive;
                        combat.Splash = Components::SplashType::Radial;
                        combat.SplashRadius = kSpiderMineExplosionRadius;

                        const int MineEntityID = it->MineEntityID;
                        const Core::Math::Vec2 ExplosionPosition = trans->Position;
                        Components::PlayerComponent MinePlayerCopy;
                        bool HasMinePlayer = false;
                        if (const auto* MinePlayer =
                                EM.GetComponent<Components::PlayerComponent>(MineEntityID))
                        {
                            MinePlayerCopy = *MinePlayer;
                            HasMinePlayer = true;
                        }

                        const uint32_t MineImageID = gImageResolver
                            ? gImageResolver(kTerranSpiderMineChkUnitID)
                            : std::numeric_limits<uint32_t>::max();
                        if (MineImageID != std::numeric_limits<uint32_t>::max())
                        {
                            const int EffectID = EM.CreateEntity();
                            EM.AddComponent<Components::TransformComponent>(EffectID).Position = ExplosionPosition;
                            auto& EffectRender = EM.AddComponent<Components::RenderComponent>(EffectID);
                            EffectRender.ImageID = MineImageID;
                            EffectRender.FrameIndex = 0;
                            EffectRender.TotalFrames = 1;
                            EffectRender.Layer = Components::RenderLayer::Effects;
                            EffectRender.Visible = true;

                            auto& Effect = EM.AddComponent<Components::EffectComponent>(EffectID);
                            Effect.LifetimeRemaining = 8.0f;
                            Effect.InitialAnimation = Game::IScriptAnims::Anim_Death;
                            Effect.DestroyWhenAnimationCompletes = true;
                            Effect.DrawAbove = true;

                            auto& EffectUnit = EM.AddComponent<Components::UnitComponent>(EffectID);
                            EffectUnit.ChkUnitID = -1;
                            EffectUnit.HitPoints = 0;
                            EffectUnit.MaxHitPoints = 1;
                            EffectUnit.SupplyCost = 0;
                            if (HasMinePlayer)
                            {
                                EM.AddComponent<Components::PlayerComponent>(EffectID) = MinePlayerCopy;
                            }
                        }

                        const float RadiusSquared = kSpiderMineExplosionRadius * kSpiderMineExplosionRadius;
                        auto& SharedSpatial = Game::Pathfinding::SpatialIndexResource::AcquireUpToDate(EM);
                        static thread_local std::vector<int> sMineExplosionTargets;
                        SharedSpatial.AllUnitsHash.QueryInto(
                            ExplosionPosition.X - kSpiderMineExplosionRadius,
                            ExplosionPosition.Y - kSpiderMineExplosionRadius,
                            ExplosionPosition.X + kSpiderMineExplosionRadius,
                            ExplosionPosition.Y + kSpiderMineExplosionRadius,
                            sMineExplosionTargets);
                        for (int TargetID : sMineExplosionTargets)
                        {
                            if (TargetID == MineEntityID)
                                continue;
                            const auto* TUnit = EM.GetComponent<Components::UnitComponent>(TargetID);
                            const auto* TPos = EM.GetComponent<Components::TransformComponent>(TargetID);
                            const auto* TPath = EM.GetComponent<Components::PathfindingComponent>(TargetID);
                            if (!TUnit || TUnit->HitPoints <= 0 || !TPos ||
                                (TPath && TPath->IsAir) || Game::Data::IsAirUnitType(TUnit->ChkUnitID))
                            {
                                continue;
                            }
                            const Core::Math::Vec2 Delta = TPos->Position - ExplosionPosition;
                            if (Delta.X * Delta.X + Delta.Y * Delta.Y > RadiusSquared)
                            {
                                continue;
                            }
                            CombatSystem::ApplyDamage(EM, TargetID, combat, MineEntityID, kSpiderMineDamage);
                        }

                        EM.DestroyEntity(MineEntityID);
                        it = mMines.erase(it);
                        continue;
                    }

                    float nDist = (dist > 0.0f) ? dist : 1.0f;
                    trans->Position.X += (dx / nDist) * speed;
                    trans->Position.Y += (dy / nDist) * speed;
                }
            }
        }

        ++it;
    }
}

bool NukeAndSpiderMineSystem::CheckSpiderMineTrigger(int MineEntityID)
{
    auto& EM = mSystemMgr.GetEntityManager();
    auto* mineTrans = EM.GetComponent<Components::TransformComponent>(MineEntityID);
    auto* minePlayer = EM.GetComponent<Components::PlayerComponent>(MineEntityID);
    auto* spiderComp = EM.GetComponent<Components::SpiderMineComponent>(MineEntityID);

    if (!mineTrans || !spiderComp) return false;

    const int MinePlayerID = minePlayer ? minePlayer->PlayerID : -1;

    auto& SharedSpatial = Game::Pathfinding::SpatialIndexResource::AcquireUpToDate(EM);
    static thread_local std::vector<int> sTriggerCandidates;
    const float R = spiderComp->TriggerRange;
    SharedSpatial.AllUnitsHash.QueryInto(
        mineTrans->Position.X - R,
        mineTrans->Position.Y - R,
        mineTrans->Position.X + R,
        mineTrans->Position.Y + R,
        sTriggerCandidates);

    int BestTargetID = -1;
    float BestDist = spiderComp->TriggerRange;

    for (int entityID : sTriggerCandidates)
    {
        if (!IsValidSpiderMineTriggerTarget(EM, entityID, MinePlayerID))
            continue;

        auto* trans = EM.GetComponent<Components::TransformComponent>(entityID);
        float dx = trans->Position.X - mineTrans->Position.X;
        float dy = trans->Position.Y - mineTrans->Position.Y;
        float dist = std::sqrt(dx * dx + dy * dy);

        if (dist < BestDist)
        {
            BestDist = dist;
            BestTargetID = entityID;
        }
    }

    if (BestTargetID >= 0)
    {
        spiderComp->ChasingEntityID = BestTargetID;
        return true;
    }

    return false;
}

} // namespace Game::Systems
