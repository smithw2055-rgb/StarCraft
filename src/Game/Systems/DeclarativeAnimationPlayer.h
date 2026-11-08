#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "Core/ECS/EntityManager.h"
#include "Core/ECS/SystemManager.h"
#include "Engine/Animation/AnimationServer2D.h"
#include "Game/Events/GameEvents.h"
#include "Resource/ModernAssetRegistry.h"

namespace Game::Systems {

struct DeclarativeAnimatorComponent {
    uint32_t IScriptID = 0;
    std::string CurrentClipName = "Init";
    uint32_t CurrentTick = 0;
    float TickAccumulator = 0.0f;
    int32_t CurrentFrameSet = 0;
    bool NoBreakSectionActive = false;
    bool Playing = true;
    bool DestroyOnClipEnd = false;
    uint32_t LoopsCompleted = 0;
};

// Godot AnimationTree / Unity Animator style 6-track declarative timeline player
// Phase 51: Connected directly to Engine::Animation::AnimationServer2D & ModernAssetRegistry
class DeclarativeAnimationPlayerSystem final : public Core::ECS::ISystem {
public:
    DeclarativeAnimationPlayerSystem(
        Core::ECS::SystemManager& SysMgr,
        const Resource::ModernAssetRegistry* Registry = nullptr,
        Engine::Animation::AnimationServer2D* AnimServer = nullptr)
        : mSystemMgr(SysMgr), mRegistry(Registry), mAnimServer(AnimServer) {}

    std::string_view GetName() const override { return "DeclarativeAnimationPlayerSystem"; }
    Core::ECS::SystemPhase GetPhase() const override { return Core::ECS::SystemPhase::Presentation; }

    void SetAssetRegistry(const Resource::ModernAssetRegistry* Registry) { mRegistry = Registry; }
    void SetAnimationServer(Engine::Animation::AnimationServer2D* Server) { mAnimServer = Server; }
    Engine::Animation::AnimationServer2D* GetAnimationServer() const noexcept { return mAnimServer; }

    bool TransitionToState(Core::ECS::EntityID Entity, const std::string& NewClipName, bool ForceInterrupt = false);
    void StepEntityTicks(Core::ECS::EntityID Entity, uint32_t TicksToAdvance);
    void Update(float DeltaTime) override;

private:
    const Resource::ModernAnimationClip* ResolveClip(uint32_t IScriptID, const std::string& ClipName) const
    {
        if (mAnimServer)
        {
            if (const auto* Clip = mAnimServer->FindClip(IScriptID, ClipName))
                return Clip;
        }
        if (mRegistry)
        {
            if (const auto* Ctrl = mRegistry->FindIScriptControllerByID(IScriptID))
            {
                auto It = Ctrl->Clips.find(ClipName);
                if (It != Ctrl->Clips.end())
                    return &It->second;
            }
        }
        return nullptr;
    }

    void EvaluateTickKeyframes(
        Core::ECS::EntityID Entity,
        DeclarativeAnimatorComponent& Anim,
        const Resource::ModernAnimationClip& Clip,
        uint32_t Tick);

    Core::ECS::SystemManager& mSystemMgr;
    const Resource::ModernAssetRegistry* mRegistry = nullptr;
    Engine::Animation::AnimationServer2D* mAnimServer = nullptr;
};

} // namespace Game::Systems
