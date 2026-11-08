#pragma once

#include <cstdint>
#include <string>

#include "Core/Audio/ModernAudioEventRouter.h"
#include "Core/ECS/EntityManager.h"
#include "Core/Math/Vec2.h"

namespace Game::Events {

// Track 2: audio_notify_track signal (aliased from Core::Audio::AnimAudioNotifyEvent)
using AnimAudioNotifyEvent = Core::Audio::AnimAudioNotifyEvent;

// Track 3: combat_notify_track signal
struct AnimCombatNotifyEvent {
    Core::ECS::EntityID SourceEntity = Core::ECS::InvalidEntity;
    uint32_t IScriptID = 0;
    std::string ClipName;
    uint32_t Tick = 0;
    std::string NotifyType; // "attackwith", "attackmelee", "domissiledmg", "useweapon", "gotorepeatattk", "castspell"
    int32_t WeaponSlotOrParam = 0;
    Core::Math::Vec2 WorldPos{0.0f, 0.0f};
};

// Track 4: socket_vfx_spawn_track signal
struct AnimSocketVfxSpawnEvent {
    Core::ECS::EntityID SourceEntity = Core::ECS::InvalidEntity;
    uint32_t IScriptID = 0;
    std::string ClipName;
    uint32_t Tick = 0;
    std::string SpawnMode;  // "imgol", "imgul", "sprol", "lowsprul", "imgoluselo"
    std::string TargetAsset;
    int16_t OffsetX = 0;
    int16_t OffsetY = 0;
};

// Track 5: root_motion_and_transform_track signal
struct AnimRootMotionEvent {
    Core::ECS::EntityID SourceEntity = Core::ECS::InvalidEntity;
    uint32_t IScriptID = 0;
    std::string ClipName;
    uint32_t Tick = 0;
    std::string Action;     // "move", "setvertpos", "sethorpos", "setfldirect", "setflspeed", "turncwise", "turnccwise"
    int32_t Param = 0;
};

// Track 6: fsm_signals_and_branches signal
struct AnimFsmSignalEvent {
    Core::ECS::EntityID SourceEntity = Core::ECS::InvalidEntity;
    uint32_t IScriptID = 0;
    std::string ClipName;
    uint32_t Tick = 0;
    std::string Signal;     // "nobrkcodestart", "nobrkcodeend", "sigorder", "orderdone", "randcondjmp"
    int32_t Param = 0;
};

} // namespace Game::Events
