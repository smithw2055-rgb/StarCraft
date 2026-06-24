#include "Core/ECS/SystemManager.h"

namespace Core::ECS
{

void SystemManager::RegisterSystem(std::unique_ptr<ISystem> System)
{
    mSystems.push_back(std::move(System));
}

void SystemManager::UpdateAll(float DeltaTime)
{
    for (auto& Sys : mSystems)
    {
        Sys->Update(DeltaTime);
    }
}

} // namespace Core::ECS
