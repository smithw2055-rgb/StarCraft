#include "Game/Systems/TransportSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Components.h"
#include "Game/Races/RaceDef.h"
#include "Core/Utils/Logger.h"

namespace Game::Systems
{

using namespace Game::Components;
using Core::Utils::Logger;

bool TransportSystem::CanLoad(Core::ECS::EntityManager& EM, int TransportID, int PassengerID,
                               std::string* OutReason)
{
    auto* TU = EM.GetComponent<UnitComponent>(TransportID);
    auto* TP = EM.GetComponent<PlayerComponent>(TransportID);
    auto* TT = EM.GetComponent<TransformComponent>(TransportID);
    auto* PU = EM.GetComponent<UnitComponent>(PassengerID);
    auto* PT = EM.GetComponent<TransformComponent>(PassengerID);
    if (!TU || !TP || !TT || !PU || !PT) return false;

    if (TP->PlayerID != EM.GetComponent<PlayerComponent>(PassengerID)->PlayerID)
    {
        if (OutReason) *OutReason = "cannot load enemy unit";
        return false;
    }

    float Dist = TT->Position.DistanceTo(PT->Position);
    if (Dist > 64.0f)
    {
        if (OutReason) *OutReason = "too far from transport";
        return false;
    }

    auto* Loaded = EM.GetComponent<LoadedUnitsComponent>(TransportID);
    if (Loaded && Loaded->PassengerIDs.size() >= (size_t)kMaxTransportCapacity)
    {
        if (OutReason) *OutReason = "transport full";
        return false;
    }

    return true;
}

bool TransportSystem::LoadUnit(Core::ECS::EntityManager& EM, int TransportID, int PassengerID)
{
    auto& Loaded = EM.AddComponent<LoadedUnitsComponent>(TransportID);
    Loaded.PassengerIDs.push_back(PassengerID);

    auto* CT = EM.GetComponent<TransformComponent>(TransportID);
    auto* PT = EM.GetComponent<TransformComponent>(PassengerID);
    if (PT && CT) PT->Position = CT->Position;

    auto* Render = EM.GetComponent<RenderComponent>(PassengerID);
    if (Render) Render->Visible = false;

    auto* Unit = EM.GetComponent<UnitComponent>(PassengerID);
    if (Unit) Unit->Speed = 0.0f;

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

    Logger::Info("TransportSystem",
        "Unit " + std::to_string(PassengerID) + " unloaded from transport " +
        std::to_string(TransportID));
    return true;
}

bool TransportSystem::UnloadAll(Core::ECS::EntityManager& EM, int TransportID)
{
    auto* Loaded = EM.GetComponent<LoadedUnitsComponent>(TransportID);
    if (!Loaded) return false;

    auto TT = EM.GetComponent<TransformComponent>(TransportID);
    Core::Math::Vec2 Pos = TT ? TT->Position : Core::Math::Vec2();

    while (!Loaded->PassengerIDs.empty())
    {
        int Pid = Loaded->PassengerIDs.back();
        Loaded->PassengerIDs.pop_back();
        auto* PT = EM.GetComponent<TransformComponent>(Pid);
        if (PT) PT->Position = Core::Math::Vec2(Pos.X + 48.0f, Pos.Y + (float)Loaded->PassengerIDs.size() * 24.0f);
        auto* Render = EM.GetComponent<RenderComponent>(Pid);
        if (Render) Render->Visible = true;
    }

    Logger::Info("TransportSystem", "All units unloaded from transport " + std::to_string(TransportID));
    return true;
}

bool TransportSystem::IsNydusCanal(Core::ECS::EntityManager& EM, int EntityID)
{
    auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
    auto* Bldg = EM.GetComponent<BuildingComponent>(EntityID);
    return Unit && Bldg && Unit->ChkUnitID == 0x78; // Nydus Canal
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
            if (BU->ChkUnitID == 0x78 && !B->UnderConstruction)
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
    (void)DeltaTime;
}

} // namespace Game::Systems
