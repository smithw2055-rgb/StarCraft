#include "Game/Systems/AnimationSystem.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace
{
    void Require(bool Value, const char* Message)
    {
        if (!Value)
        {
            std::fprintf(stderr, "FAIL: %s\n", Message);
            std::exit(1);
        }
    }

    auto HasOnly(std::initializer_list<int> AnimIds)
    {
        std::array<bool, Game::IScriptAnims::kCount> Present{};
        for (int AnimId : AnimIds)
        {
            if (AnimId >= 0 && AnimId < (int)Present.size())
                Present[(size_t)AnimId] = true;
        }
        return [Present](int AnimId) {
            return AnimId >= 0 &&
                   AnimId < (int)Present.size() &&
                   Present[(size_t)AnimId];
        };
    }
}

int main()
{
    using Game::IScriptAnims::Anim_AirAttkInit;
    using Game::IScriptAnims::Anim_AlmostBuilt;
    using Game::IScriptAnims::Anim_Built;
    using Game::IScriptAnims::Anim_GndAttkInit;
    using Game::IScriptAnims::Anim_Init;
    using Game::IScriptAnims::Anim_IsWorking;
    using Game::IScriptAnims::Anim_Walking;
    using Game::IScriptAnims::Anim_WalkingToIdle;
    using Game::IScriptAnims::Anim_WorkingToIdle;
    using Game::Systems::AnimState;
    using Game::Systems::AnimationSystem;

    Require(AnimationSystem::SelectIScriptAnimationForState(
                AnimState::Constructing, true, true, false,
                false, false, false, false,
                HasOnly({Anim_Init, Anim_AlmostBuilt, Anim_Built})) == Anim_AlmostBuilt,
            "building under construction uses AlmostBuilt iscript when available");

    Require(AnimationSystem::SelectIScriptAnimationForState(
                AnimState::Constructing, true, true, false,
                false, false, false, false,
                HasOnly({Anim_Init, Anim_Built})) == Anim_Init,
            "building under construction falls back to Init when AlmostBuilt is unavailable");

    Require(AnimationSystem::SelectIScriptAnimationForState(
                AnimState::Idle, true, false, true,
                false, false, false, false,
                HasOnly({Anim_Init, Anim_Built, Anim_IsWorking})) == Anim_Built,
            "completed building plays Built iscript before returning to idle or working");

    Require(AnimationSystem::SelectIScriptAnimationForState(
                AnimState::Idle, true, false, false,
                true, false, false, false,
                HasOnly({Anim_Init, Anim_IsWorking, Anim_WorkingToIdle})) == Anim_IsWorking,
            "working building uses IsWorking iscript");

    Require(AnimationSystem::SelectIScriptAnimationForState(
                AnimState::Idle, true, false, false,
                false, true, false, false,
                HasOnly({Anim_Init, Anim_WorkingToIdle})) == Anim_WorkingToIdle,
            "building transitions from working to idle");

    Require(AnimationSystem::SelectIScriptAnimationForState(
                AnimState::Walking, false, false, false,
                false, false, false, false,
                HasOnly({Anim_Init, Anim_Walking})) == Anim_Walking,
            "mobile unit uses Walking iscript while moving");

    Require(AnimationSystem::SelectIScriptAnimationForState(
                AnimState::Idle, false, false, false,
                false, false, true, false,
                HasOnly({Anim_Init, Anim_WalkingToIdle})) == Anim_WalkingToIdle,
            "mobile unit uses WalkingToIdle after movement stops");

    Require(AnimationSystem::SelectIScriptAnimationForState(
                AnimState::Attacking, false, false, false,
                false, false, false, true,
                HasOnly({Anim_Init, Anim_GndAttkInit, Anim_AirAttkInit})) == Anim_GndAttkInit,
            "pending attack prefers ground attack init when available");

    Game::Components::CommandComponent MoveCmd;
    MoveCmd.CurrentCommand = Game::Components::CommandType::Move;
    Game::Components::PathfindingComponent EmptyPath;
    Game::Components::UnitComponent Unit;
    Require(AnimationSystem::GetAnimStateFromComponents(
                &MoveCmd, nullptr, &EmptyPath, &Unit, nullptr, nullptr) == AnimState::Idle,
            "move command with no active path does not keep walking animation alive");

    EmptyPath.Path.push_back({64.0f, 0.0f});
    Require(AnimationSystem::GetAnimStateFromComponents(
                &MoveCmd, nullptr, &EmptyPath, &Unit, nullptr, nullptr) == AnimState::Walking,
            "active path drives walking animation");

    Game::Components::CommandComponent AttackMoveCmd;
    AttackMoveCmd.CurrentCommand = Game::Components::CommandType::AttackMove;
    Require(AnimationSystem::GetAnimStateFromComponents(
                &AttackMoveCmd, nullptr, &EmptyPath, &Unit, nullptr, nullptr) == AnimState::Walking,
            "attack-move units use walking animation while physically moving");

    EmptyPath.Path.clear();
    EmptyPath.CurrentSpeed = 12.0f;
    Require(AnimationSystem::GetAnimStateFromComponents(
                &MoveCmd, nullptr, &EmptyPath, &Unit, nullptr, nullptr) == AnimState::Walking,
            "residual velocity keeps walking only while the unit is still physically moving");

    EmptyPath.CurrentSpeed = 0.0f;
    EmptyPath.CurrentVelocity = {0.0f, 0.0f};
    Require(AnimationSystem::GetAnimStateFromComponents(
                &AttackMoveCmd, nullptr, &EmptyPath, &Unit, nullptr, nullptr) == AnimState::Idle,
            "attack-move command without movement does not force attack animation");

    std::printf("AnimationStateTest passed\n");
    return 0;
}
