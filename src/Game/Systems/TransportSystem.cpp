#include "Game/Systems/TransportSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Components.h"
#include "Game/DataEnums.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Core/Utils/Logger.h"

#include <algorithm>

namespace Game::Systems
{

using namespace Game::Components;
using Core::Utils::Logger;

namespace
{
    bool IsTransportUnit(Core::ECS::EntityManager& EM,
                         int EntityID,
                         const UnitComponent* Unit = nullptr,
                         const PlayerComponent* Player = nullptr)
    {
        if (!Unit)
            Unit = EM.GetComponent<UnitComponent>(EntityID);
        if (!Player)
            Player = EM.GetComponent<PlayerComponent>(EntityID);
        if (!Unit)
            return false;
        if (Unit->ChkUnitID == 11 ||  // Dropship
            Unit->ChkUnitID == 69 ||  // Shuttle
            Unit->ChkUnitID == 125)   // Bunker
        {
            return true;
        }
        return Unit->ChkUnitID == 42 && Player && // Overlord
            UpgradeSystem::HasResearch(
                Player->PlayerID, ResearchId::Zerg_VentralSacs);
    }

    bool ValidateLoadTarget(Core::ECS::EntityManager& EM, int TransportID,
                            int PassengerID, bool RequireRange,
                            std::string* OutReason)
    {
        auto Fail = [&](const char* Reason)
        {
            if (OutReason) *OutReason = Reason;
            return false;
        };

        auto* TU = EM.GetComponent<UnitComponent>(TransportID);
        auto* TP = EM.GetComponent<PlayerComponent>(TransportID);
        auto* TT = EM.GetComponent<TransformComponent>(TransportID);
        auto* PU = EM.GetComponent<UnitComponent>(PassengerID);
        auto* PP = EM.GetComponent<PlayerComponent>(PassengerID);
        auto* PT = EM.GetComponent<TransformComponent>(PassengerID);
        if (!TU || !TP || !TT || !PU || !PP || !PT || PassengerID == TransportID)
            return Fail("invalid transport or passenger");
        if (!IsTransportUnit(EM, TransportID, TU, TP))
            return Fail("unit cannot transport passengers");
        if (TP->PlayerID != PP->PlayerID)
            return Fail("cannot load enemy unit");
        if (PU->HitPoints <= 0 || EM.GetComponent<ResourceComponent>(PassengerID) ||
            EM.GetComponent<BuildingComponent>(PassengerID))
            return Fail("passenger cannot be loaded");
        if (Game::Data::IsAirUnitType(PU->ChkUnitID))
            return Fail("cannot load air unit");

        auto* Loaded = EM.GetComponent<LoadedUnitsComponent>(TransportID);
        if (Loaded && std::find(Loaded->PassengerIDs.begin(), Loaded->PassengerIDs.end(), PassengerID) !=
                          Loaded->PassengerIDs.end())
            return Fail("unit already loaded");
        const size_t Capacity = TU->ChkUnitID == 125
            ? static_cast<size_t>(TransportSystem::kMaxBunkerCapacity)
            : static_cast<size_t>(TransportSystem::kMaxTransportCapacity);
        if (Loaded && Loaded->PassengerIDs.size() >= Capacity)
            return Fail("transport full");
        if (RequireRange && TT->Position.DistanceTo(PT->Position) > 64.0f)
            return Fail("too far from transport");
        return true;
    }

    void ClearPassengerMotion(Core::ECS::EntityManager& EM, int PassengerID)
    {
        if (auto* Path = EM.GetComponent<PathfindingComponent>(PassengerID))
        {
            Path->Path.clear();
            Path->PathIndex = 0;
            Path->CurrentSpeed = 0.0f;
            Path->TargetSpeed = 0.0f;
            Path->CurrentVelocity = {};
        }
        if (auto* Command = EM.GetComponent<CommandComponent>(PassengerID))
        {
            Command->CancelAll();
            Command->CommandQueue.clear();
            Command->CurrentCommand = CommandType::Stop;
            Command->TargetEntityID = -1;
        }
    }

    void RestorePassenger(Core::ECS::EntityManager& EM, int PassengerID)
    {
        if (auto* Render = EM.GetComponent<RenderComponent>(PassengerID))
            Render->Visible = true;
        if (auto* Unit = EM.GetComponent<UnitComponent>(PassengerID))
        {
            const int DefID = Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID);
            if (DefID >= 0)
                Unit->Speed = Game::Races::GetUnitDefByID(DefID).Speed;
        }
    }

    Core::Math::Vec2 UnloadOffset(int SequenceIndex)
    {
        static const Core::Math::Vec2 Offsets[] = {
            {48.0f, 0.0f}, {34.0f, 34.0f}, {0.0f, 48.0f}, {-34.0f, 34.0f},
            {-48.0f, 0.0f}, {-34.0f, -34.0f}, {0.0f, -48.0f}, {34.0f, -34.0f}
        };
        return Offsets[SequenceIndex % (int)(sizeof(Offsets) / sizeof(Offsets[0]))];
    }

    bool IsPassengerLoaded(Core::ECS::EntityManager& EM, int TransportID, int PassengerID)
    {
        const auto* Loaded = EM.GetComponent<LoadedUnitsComponent>(TransportID);
        return Loaded && std::find(Loaded->PassengerIDs.begin(), Loaded->PassengerIDs.end(),
                                   PassengerID) != Loaded->PassengerIDs.end();
    }

    void IssueTransportApproach(Core::ECS::EntityManager& EM, int TransportID,
                                int PassengerID)
    {
        auto* PassengerTransform = EM.GetComponent<TransformComponent>(PassengerID);
        if (!PassengerTransform)
            return;

        auto* Pending = EM.GetComponent<PendingTransportLoadComponent>(TransportID);
        if (!Pending)
            Pending = &EM.AddComponent<PendingTransportLoadComponent>(TransportID);
        Pending->PassengerID = PassengerID;
        Pending->LastIssuedTarget = PassengerTransform->Position;
        Pending->RepathTimer = 0.25f;
        CommandSystem::IssueMoveAndHasPath(
            EM, TransportID, PassengerTransform->Position, PassengerID);
    }
}

bool TransportSystem::IsTransport(Core::ECS::EntityManager& EM, int EntityID)
{
    const auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
    const auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
    return IsTransportUnit(EM, EntityID, Unit, Player);
}

bool TransportSystem::CanLoad(Core::ECS::EntityManager& EM, int TransportID, int PassengerID,
                               std::string* OutReason)
{
    return ValidateLoadTarget(EM, TransportID, PassengerID, true, OutReason);
}

bool TransportSystem::IssueLoad(Core::ECS::EntityManager& EM, int TransportID,
                                int PassengerID, std::string* OutReason)
{
    if (!ValidateLoadTarget(EM, TransportID, PassengerID, false, OutReason))
        return false;
    if (CanLoad(EM, TransportID, PassengerID, nullptr))
        return LoadUnit(EM, TransportID, PassengerID);

    auto* TransportUnit = EM.GetComponent<UnitComponent>(TransportID);
    auto* PassengerTransform = EM.GetComponent<TransformComponent>(PassengerID);
    if (!TransportUnit || !PassengerTransform || TransportUnit->Speed <= 0.0f)
    {
        if (OutReason) *OutReason = "transport cannot approach passenger";
        return false;
    }

    IssueTransportApproach(EM, TransportID, PassengerID);
    return true;
}

bool TransportSystem::IssueBoard(Core::ECS::EntityManager& EM,
                                 const std::vector<int>& PassengerIDs,
                                 int TransportID)
{
    if (!IsTransport(EM, TransportID))
        return false;

    auto* TransportTransform = EM.GetComponent<TransformComponent>(TransportID);
    if (!TransportTransform)
        return false;

    const auto* TransportUnit = EM.GetComponent<UnitComponent>(TransportID);
    const bool CanRendezvous =
        TransportUnit && TransportUnit->ChkUnitID != 125;
    const size_t Capacity = TransportUnit && TransportUnit->ChkUnitID == 125
        ? static_cast<size_t>(kMaxBunkerCapacity)
        : static_cast<size_t>(kMaxTransportCapacity);
    const auto* Loaded = EM.GetComponent<LoadedUnitsComponent>(TransportID);
    size_t ReservedSlots = Loaded ? Loaded->PassengerIDs.size() : 0;

    if (auto* ExistingPlan = EM.GetComponent<PendingTransportBoardingComponent>(TransportID))
    {
        for (int PassengerID : ExistingPlan->PassengerIDs)
            if (auto* Pending = EM.GetComponent<PendingBoardTransportComponent>(PassengerID);
                Pending && Pending->TransportID == TransportID)
                Pending->TransportID = -1;
        ExistingPlan->PassengerIDs.clear();
        ExistingPlan->CurrentIndex = 0;
    }

    bool AcceptedAny = false;
    std::vector<int> RendezvousPassengers;
    for (int PassengerID : PassengerIDs)
    {
        if (ReservedSlots >= Capacity)
            break;
        if (!ValidateLoadTarget(EM, TransportID, PassengerID, false, nullptr))
            continue;
        if (CanLoad(EM, TransportID, PassengerID, nullptr))
        {
            if (LoadUnit(EM, TransportID, PassengerID))
            {
                ++ReservedSlots;
                AcceptedAny = true;
            }
            continue;
        }

        auto& Pending = EM.AddComponent<PendingBoardTransportComponent>(PassengerID);
        Pending.TransportID = TransportID;
        Pending.LastIssuedTarget = TransportTransform->Position;
        Pending.RepathTimer = 0.25f;
        const bool PassengerCanApproach = CommandSystem::IssueMoveAndHasPath(
            EM, PassengerID, TransportTransform->Position, TransportID);
        if (!PassengerCanApproach)
            Pending.TransportID = -1;

        if (CanRendezvous)
        {
            RendezvousPassengers.push_back(PassengerID);
            ++ReservedSlots;
            AcceptedAny = true;
        }
        else if (PassengerCanApproach)
        {
            ++ReservedSlots;
            AcceptedAny = true;
        }
    }

    if (!RendezvousPassengers.empty())
    {
        auto* Plan = EM.GetComponent<PendingTransportBoardingComponent>(TransportID);
        if (!Plan)
            Plan = &EM.AddComponent<PendingTransportBoardingComponent>(TransportID);
        Plan->PassengerIDs = std::move(RendezvousPassengers);
        Plan->CurrentIndex = 0;
        IssueTransportApproach(EM, TransportID, Plan->PassengerIDs.front());
    }
    return AcceptedAny;
}

bool TransportSystem::LoadUnit(Core::ECS::EntityManager& EM, int TransportID, int PassengerID)
{
    if (!CanLoad(EM, TransportID, PassengerID, nullptr))
        return false;
    auto* Loaded = EM.GetComponent<LoadedUnitsComponent>(TransportID);
    if (!Loaded)
        Loaded = &EM.AddComponent<LoadedUnitsComponent>(TransportID);
    Loaded->PassengerIDs.push_back(PassengerID);

    auto* CT = EM.GetComponent<TransformComponent>(TransportID);
    auto* PT = EM.GetComponent<TransformComponent>(PassengerID);
    if (PT && CT) PT->Position = CT->Position;

    auto* Render = EM.GetComponent<RenderComponent>(PassengerID);
    if (Render) Render->Visible = false;

    auto* Unit = EM.GetComponent<UnitComponent>(PassengerID);
    if (Unit) Unit->Speed = 0.0f;
    ClearPassengerMotion(EM, PassengerID);
    if (auto* Pending = EM.GetComponent<PendingTransportLoadComponent>(TransportID))
        Pending->PassengerID = -1;
    if (auto* PendingBoard = EM.GetComponent<PendingBoardTransportComponent>(PassengerID))
        PendingBoard->TransportID = -1;

    Logger::Info("TransportSystem",
        "Unit " + std::to_string(PassengerID) + " loaded into transport " +
        std::to_string(TransportID));
    return true;
}

bool TransportSystem::CanUnload(Core::ECS::EntityManager& EM, int TransportID,
                                 int PassengerIndex, std::string* OutReason)
{
    auto* Loaded = EM.GetComponent<LoadedUnitsComponent>(TransportID);
    if (!Loaded || PassengerIndex < 0 || PassengerIndex >= (int)Loaded->PassengerIDs.size())
    {
        if (OutReason) *OutReason = "no such passenger";
        return false;
    }
    return true;
}

bool TransportSystem::UnloadUnit(Core::ECS::EntityManager& EM, int TransportID,
                                  int PassengerIndex, const Core::Math::Vec2& Position)
{
    auto* Loaded = EM.GetComponent<LoadedUnitsComponent>(TransportID);
    if (!Loaded || PassengerIndex >= (int)Loaded->PassengerIDs.size()) return false;

    int PassengerID = Loaded->PassengerIDs[PassengerIndex];
    Loaded->PassengerIDs.erase(Loaded->PassengerIDs.begin() + PassengerIndex);

    auto* TT = EM.GetComponent<TransformComponent>(TransportID);
    auto* PT = EM.GetComponent<TransformComponent>(PassengerID);
    if (PT && TT)
    {
        PT->Position = Position.LengthSq() > 0 ? Position :
            Core::Math::Vec2(TT->Position.X + 48.0f, TT->Position.Y);
    }

    auto* Render = EM.GetComponent<RenderComponent>(PassengerID);
    if (Render) Render->Visible = true;
    RestorePassenger(EM, PassengerID);

    Logger::Info("TransportSystem",
        "Unit " + std::to_string(PassengerID) + " unloaded from transport " +
        std::to_string(TransportID));
    return true;
}

bool TransportSystem::UnloadAll(Core::ECS::EntityManager& EM, int TransportID)
{
    auto* Loaded = EM.GetComponent<LoadedUnitsComponent>(TransportID);
    if (!Loaded || Loaded->PassengerIDs.empty()) return false;

    auto* Pending = EM.GetComponent<PendingTransportUnloadComponent>(TransportID);
    if (!Pending)
        Pending = &EM.AddComponent<PendingTransportUnloadComponent>(TransportID);
    if (Pending->Active)
        return true;
    Pending->Active = true;
    Pending->SequenceIndex = 0;
    Pending->TimeUntilNextPassenger = 0.0f;
    Logger::Info("TransportSystem", "Sequential unload started for transport " +
        std::to_string(TransportID));
    return true;
}

bool TransportSystem::IsNydusCanal(Core::ECS::EntityManager& EM, int EntityID)
{
    auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
    auto* Bldg = EM.GetComponent<BuildingComponent>(EntityID);
    return Unit && Bldg && !Bldg->UnderConstruction &&
           Unit->ChkUnitID == 0x86; // Nydus Canal (CHK 134)
}

bool TransportSystem::EnterNydusCanal(Core::ECS::EntityManager& EM, int UnitID, int NydusID)
{
    if (!IsNydusCanal(EM, NydusID)) return false;

    auto* NPlayer = EM.GetComponent<PlayerComponent>(NydusID);
    auto* UPlayer = EM.GetComponent<PlayerComponent>(UnitID);
    if (!NPlayer || !UPlayer || NPlayer->PlayerID != UPlayer->PlayerID) return false;

    float Dist = 0.0f;
    if (auto* NT = EM.GetComponent<TransformComponent>(NydusID))
        if (auto* UT = EM.GetComponent<TransformComponent>(UnitID))
            Dist = NT->Position.DistanceTo(UT->Position);

    if (Dist > 64.0f) return false;

    // Teleport to nearest exit
    auto Buildings = EM.GetEntitiesWith<BuildingComponent, PlayerComponent, TransformComponent>();
    for (int Bid : Buildings)
    {
        auto* B = EM.GetComponent<BuildingComponent>(Bid);
        auto* P = EM.GetComponent<PlayerComponent>(Bid);
        auto* T = EM.GetComponent<TransformComponent>(Bid);
        if (!B || !P || !T || Bid == NydusID) continue;
        if (P->PlayerID != NPlayer->PlayerID) continue;
        if (auto* BU = EM.GetComponent<UnitComponent>(Bid))
            if (BU->ChkUnitID == 0x86 && !B->UnderConstruction)
            {
                auto* UT = EM.GetComponent<TransformComponent>(UnitID);
                if (UT) UT->Position = Core::Math::Vec2(T->Position.X + 24.0f, T->Position.Y);
                Logger::Info("TransportSystem",
                    "Unit " + std::to_string(UnitID) + " entered Nydus Canal");
                return true;
            }
    }
    return false;
}

void TransportSystem::Update(float DeltaTime)
{
    auto& EM = mSystemMgr.GetEntityManager();
    UpdateOrders(EM, DeltaTime);
}

void TransportSystem::UpdateOrders(Core::ECS::EntityManager& EM, float DeltaTime)
{
    for (int TransportID : EM.GetEntitiesWith<PendingTransportBoardingComponent>())
    {
        auto* Plan = EM.GetComponent<PendingTransportBoardingComponent>(TransportID);
        if (!Plan)
            continue;

        while (Plan->CurrentIndex < static_cast<int>(Plan->PassengerIDs.size()))
        {
            const int PassengerID = Plan->PassengerIDs[Plan->CurrentIndex];
            if (IsPassengerLoaded(EM, TransportID, PassengerID))
            {
                ++Plan->CurrentIndex;
                continue;
            }
            if (!ValidateLoadTarget(EM, TransportID, PassengerID, false, nullptr))
            {
                if (auto* Pending = EM.GetComponent<PendingBoardTransportComponent>(PassengerID);
                    Pending && Pending->TransportID == TransportID)
                    Pending->TransportID = -1;
                ++Plan->CurrentIndex;
                continue;
            }

            auto* PendingLoad = EM.GetComponent<PendingTransportLoadComponent>(TransportID);
            if (!PendingLoad || PendingLoad->PassengerID != PassengerID)
                IssueTransportApproach(EM, TransportID, PassengerID);
            break;
        }

        if (Plan->CurrentIndex >= static_cast<int>(Plan->PassengerIDs.size()))
        {
            Plan->PassengerIDs.clear();
            Plan->CurrentIndex = 0;
            if (auto* PendingLoad = EM.GetComponent<PendingTransportLoadComponent>(TransportID))
                PendingLoad->PassengerID = -1;
        }
    }

    for (int TransportID : EM.GetEntitiesWith<PendingTransportLoadComponent>())
    {
        auto* Pending = EM.GetComponent<PendingTransportLoadComponent>(TransportID);
        if (!Pending || Pending->PassengerID < 0)
            continue;

        const int PassengerID = Pending->PassengerID;
        if (!ValidateLoadTarget(EM, TransportID, PassengerID, false, nullptr))
        {
            Pending->PassengerID = -1;
            continue;
        }
        if (CanLoad(EM, TransportID, PassengerID, nullptr))
        {
            LoadUnit(EM, TransportID, PassengerID);
            continue;
        }

        auto* PassengerTransform = EM.GetComponent<TransformComponent>(PassengerID);
        if (!PassengerTransform)
        {
            Pending->PassengerID = -1;
            continue;
        }
        Pending->RepathTimer -= std::max(0.0f, DeltaTime);
        const bool TargetMoved =
            Pending->LastIssuedTarget.DistanceTo(PassengerTransform->Position) > 24.0f;
        const auto* Command = EM.GetComponent<CommandComponent>(TransportID);
        const bool TransportStopped = !Command || Command->CurrentCommand == CommandType::Stop;
        if (Pending->RepathTimer <= 0.0f && (TargetMoved || TransportStopped))
        {
            Pending->LastIssuedTarget = PassengerTransform->Position;
            Pending->RepathTimer = 0.25f;
            CommandSystem::IssueMoveAndHasPath(
                EM, TransportID, PassengerTransform->Position, PassengerID);
        }
    }

    for (int PassengerID : EM.GetEntitiesWith<PendingBoardTransportComponent>())
    {
        auto* Pending = EM.GetComponent<PendingBoardTransportComponent>(PassengerID);
        if (!Pending || Pending->TransportID < 0)
            continue;

        const int TransportID = Pending->TransportID;
        if (!ValidateLoadTarget(EM, TransportID, PassengerID, false, nullptr))
        {
            Pending->TransportID = -1;
            continue;
        }
        if (CanLoad(EM, TransportID, PassengerID, nullptr))
        {
            LoadUnit(EM, TransportID, PassengerID);
            continue;
        }

        auto* TransportTransform = EM.GetComponent<TransformComponent>(TransportID);
        if (!TransportTransform)
        {
            Pending->TransportID = -1;
            continue;
        }
        Pending->RepathTimer -= std::max(0.0f, DeltaTime);
        const bool TargetMoved =
            Pending->LastIssuedTarget.DistanceTo(TransportTransform->Position) > 24.0f;
        const auto* Command = EM.GetComponent<CommandComponent>(PassengerID);
        const bool PassengerStopped = !Command || Command->CurrentCommand == CommandType::Stop;
        if (Pending->RepathTimer <= 0.0f && (TargetMoved || PassengerStopped))
        {
            Pending->LastIssuedTarget = TransportTransform->Position;
            Pending->RepathTimer = 0.25f;
            if (!CommandSystem::IssueMoveAndHasPath(
                    EM, PassengerID, TransportTransform->Position, TransportID))
            {
                Pending->TransportID = -1;
            }
        }
    }

    for (int TransportID : EM.GetEntitiesWith<PendingTransportUnloadComponent>())
    {
        auto* Pending = EM.GetComponent<PendingTransportUnloadComponent>(TransportID);
        auto* Loaded = EM.GetComponent<LoadedUnitsComponent>(TransportID);
        auto* Transform = EM.GetComponent<TransformComponent>(TransportID);
        if (!Pending || !Pending->Active || !Loaded ||
            Loaded->PassengerIDs.empty() || !Transform)
            continue;

        Pending->TimeUntilNextPassenger -= std::max(0.0f, DeltaTime);
        if (Pending->TimeUntilNextPassenger > 0.0f)
            continue;

        const Core::Math::Vec2 Position =
            Transform->Position + UnloadOffset(Pending->SequenceIndex);
        if (UnloadUnit(EM, TransportID, 0, Position))
        {
            ++Pending->SequenceIndex;
            Pending->TimeUntilNextPassenger = 0.35f;
        }
        if (Loaded->PassengerIDs.empty())
        {
            Pending->SequenceIndex = 0;
            Pending->TimeUntilNextPassenger = 0.0f;
            Pending->Active = false;
            Logger::Info("TransportSystem", "Sequential unload completed for transport " +
                std::to_string(TransportID));
        }
    }
}

} // namespace Game::Systems
