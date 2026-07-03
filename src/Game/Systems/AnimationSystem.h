#pragma once

#include "Core/ECS/SystemManager.h"
#include "Game/Components.h"
#include "Game/IScriptVM.h"
#include <vector>
#include <unordered_map>

namespace Core::Audio { class AudioSystem; }

namespace Game::Systems
{

class BulletSystem;

enum class AnimState : uint8_t
{
    Idle,
    Walking,
    Attacking,
    Dying,
    Constructing,
    Morphing,
    Burrowed,
    Flying,
    Count
};

struct AnimClip
{
    AnimState State = AnimState::Idle;
    int StartFrame = 0;
    int FrameCount = 1;
    float FrameDuration = 0.15f;
    bool Loop = true;
    bool Flipped = false;
};

class AnimationSystem : public Core::ECS::ISystem
{
public:
    AnimationSystem(Core::ECS::SystemManager& SysMgr) : mSystemMgr(SysMgr) {}
    AnimationSystem(Core::ECS::SystemManager& SysMgr, Core::Audio::AudioSystem* Audio)
        : mSystemMgr(SysMgr), mAudio(Audio) {}
    AnimationSystem(Core::ECS::SystemManager& SysMgr, Core::Audio::AudioSystem* Audio, BulletSystem* BulletSys)
        : mSystemMgr(SysMgr), mAudio(Audio), mBulletSystem(BulletSys) {}
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "AnimationSystem"; }

    static void RegisterUnitAnim(int ChkUnitID, const std::vector<AnimClip>& Clips);
    static bool HasIScriptAnimationForImage(int ImageID, int AnimID);
    static AnimState GetAnimStateFromComponents(const Components::CommandComponent* Cmd,
                                                  const Components::CombatComponent* Combat,
                                                  const Components::PathfindingComponent* Path,
                                                  const Components::UnitComponent* Unit,
                                                  const Components::BuildingComponent* Bldg,
                                                  const Components::MorphComponent* Morph);
private:
    struct EntityScriptState
    {
        Game::IScriptVM VM;
        int ImageID = -1;
        int ScriptID = -1;
        int AnimID = -1;
        bool WasWorking = false;
        bool WasUnderConstruction = false;
        float StepAccumulator = 0.0f;
    };

    struct UnitAnimData
    {
        std::vector<AnimClip> Clips;
    };
    static std::unordered_map<int, UnitAnimData> sAnimDB;
    static Game::IScriptDatabase sIScriptDB;
    static bool sTriedLoadIScript;

    Core::ECS::SystemManager& mSystemMgr;
    Core::Audio::AudioSystem* mAudio = nullptr;
    BulletSystem* mBulletSystem = nullptr;
    std::unordered_map<int, EntityScriptState> mScriptStates;

    static void EnsureIScriptLoaded();
    void UpdateScriptEffects(float DeltaTime);
    void UpdateImageTrees(float DeltaTime);
    void ClearImageTree(int EntityID);
    void MarkImageSubtreeHidden(Components::ImageTreeComponent& Tree, int ParentIndex);
    void AddImageTreeNode(int EntityID,
                          int ImageID,
                          int OffsetX,
                          int OffsetY,
                          bool DrawAbove,
                          bool Flipped,
                          float Lifetime,
                          bool UseSpriteID = false);
    void AddImageTreeNode(int EntityID,
                          int ParentIndex,
                          int ImageID,
                          Core::Math::Vec2 Offset,
                          bool DrawAbove,
                          bool Flipped,
                          float Lifetime,
                          bool UseSpriteID = false);
    void StartImageNodeScript(Components::ImageSubnode& Node);
    void ConsumeImageNodeEvents(int EntityID,
                                int NodeIndex,
                                Core::Math::Vec2 NodeOffset,
                                float NodeLifetime,
                                const Components::TransformComponent& Trans,
                                const Components::PlayerComponent* Player);
    void SpawnIScriptOverlay(int ParentEntityID,
                             const Components::TransformComponent& ParentTrans,
                             int ImageID,
                             int OffsetX,
                             int OffsetY,
                             float Lifetime,
                             bool FollowParent);
    void ConsumeIScriptEvents(int EntityID,
                              EntityScriptState& State,
                              const Components::TransformComponent& Trans,
                              const Components::PlayerComponent* Player,
                              Components::CombatComponent* Combat);
    bool UpdateIScriptAnimation(int EntityID,
                                float DeltaTime,
                                Components::RenderComponent& Render,
                                const Components::TransformComponent& Trans,
                                const Components::PlayerComponent* Player,
                                Components::CombatComponent* Combat,
                                const Components::BuildingComponent* Bldg,
                                const Components::ProductionComponent* Prod);
};

} // namespace Game::Systems
