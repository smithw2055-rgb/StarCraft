#pragma once

#include "Core/ECS/SystemManager.h"
#include "Core/Math/Vec2.h"
#include <vector>

namespace Game::Systems
{

class TransportSystem : public Core::ECS::ISystem
{
public:
    TransportSystem(Core::ECS::SystemManager& SysMgr) : mSystemMgr(SysMgr) {}
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "TransportSystem"; }

    static bool CanLoad(Core::ECS::EntityManager& EM, int TransportID, int PassengerID,
                        std::string* OutReason = nullptr);
    static bool LoadUnit(Core::ECS::EntityManager& EM, int TransportID, int PassengerID);
    static bool CanUnload(Core::ECS::EntityManager& EM, int TransportID,
                          int PassengerIndex, std::string* OutReason = nullptr);
    static bool UnloadUnit(Core::ECS::EntityManager& EM, int TransportID, int PassengerIndex,
                           const Core::Math::Vec2& Position = {});
    static bool UnloadAll(Core::ECS::EntityManager& EM, int TransportID);
    static bool IsNydusCanal(Core::ECS::EntityManager& EM, int EntityID);
    static bool EnterNydusCanal(Core::ECS::EntityManager& EM, int UnitID, int NydusID);

    static constexpr int kMaxTransportCapacity = 8;
    static constexpr int kMaxBunkerCapacity = 4;

private:
    Core::ECS::SystemManager& mSystemMgr;
};

} // namespace Game::Systems
