#pragma once

#include "Core/ECS/SystemManager.h"
#include "Core/Math/Vec2.h"
#include "Engine/Gameplay/GarrisonAndTransportServer2D.h"
#include <vector>

namespace Game::Map { class GameMap; }

namespace Game::Systems
{

class TransportSystem : public Core::ECS::ISystem
{
public:
    TransportSystem(Core::ECS::SystemManager& SysMgr) : mSystemMgr(SysMgr) {}
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "TransportSystem"; }

    static void SetMap(const Game::Map::GameMap* Map);

    static bool CanLoad(Core::ECS::EntityManager& EM, int TransportID, int PassengerID,
                         std::string* OutReason = nullptr);
    static bool IssueLoad(Core::ECS::EntityManager& EM, int TransportID, int PassengerID,
                          std::string* OutReason = nullptr);
    static bool IsTransport(Core::ECS::EntityManager& EM, int EntityID);
    static bool IssueBoard(Core::ECS::EntityManager& EM,
                           const std::vector<int>& PassengerIDs,
                           int TransportID);
    static bool LoadUnit(Core::ECS::EntityManager& EM, int TransportID, int PassengerID);
    static bool CanUnload(Core::ECS::EntityManager& EM, int TransportID,
                          int PassengerIndex, std::string* OutReason = nullptr);
    static bool UnloadUnit(Core::ECS::EntityManager& EM, int TransportID, int PassengerIndex,
                           const Core::Math::Vec2& Position = {});
    static bool UnloadAll(Core::ECS::EntityManager& EM, int TransportID);
    static void UpdateOrders(Core::ECS::EntityManager& EM, float DeltaTime);
    static bool IsNydusCanal(Core::ECS::EntityManager& EM, int EntityID);
    static bool EnterNydusCanal(Core::ECS::EntityManager& EM, int UnitID, int NydusID);

    static constexpr int kMaxTransportCapacity = 8;
    static constexpr int kMaxBunkerCapacity = 4;

    static int GetUnitSpaceRequired(int ChkUnitID);
    static bool CanEnterBunker(int ChkUnitID);
    static int GetLoadedSpace(Core::ECS::EntityManager& EM, int TransportID);

    static void BindEngineGarrisonServer(Engine::Gameplay::GarrisonAndTransportServer2D* Server) noexcept { sEngineGarrisonServer = Server; }
    static Engine::Gameplay::GarrisonAndTransportServer2D* GetBoundEngineGarrisonServer() noexcept { return sEngineGarrisonServer; }

private:
    Core::ECS::SystemManager& mSystemMgr;
    inline static Engine::Gameplay::GarrisonAndTransportServer2D* sEngineGarrisonServer = nullptr;
};

} // namespace Game::Systems
