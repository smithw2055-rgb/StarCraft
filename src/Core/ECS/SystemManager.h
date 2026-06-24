#pragma once

#include <vector>
#include <memory>
#include <string_view>

#include "Core/ECS/EntityManager.h"

namespace Core::ECS
{

class ISystem
{
public:
    virtual ~ISystem() = default;
    virtual void Update(float DeltaTime) = 0;
    virtual std::string_view GetName() const = 0;
};

class SystemManager
{
public:
    void RegisterSystem(std::unique_ptr<ISystem> System);
    void UpdateAll(float DeltaTime);

    EntityManager& GetEntityManager() { return mEntityMgr; }

private:
    EntityManager mEntityMgr;
    std::vector<std::unique_ptr<ISystem>> mSystems;
};

} // namespace Core::ECS
