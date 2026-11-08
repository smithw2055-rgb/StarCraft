#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include "Core/Math/Vec2.h"
#include "Resource/ModernAssetRegistry.h"

namespace Engine::ECS
{

// ============================================================================
// CommonComponents2D & CapabilityTraitRegistry (Phase 66):
// General-Purpose 2D / RTS Engine Component Primitives & Trait Bitflags
// Inspired by Godot 4 Node2D / Sprite2D & Unity DOTS Entities:
// - Zero dependency on Game/ headers.
// - Provides standard, trivially-copyable (rollback-safe) 2D ECS components
//   that any 2D / RTS / Top-Down ARPG / Tower Defense project can use directly
//   with Engine::ECS::GenericWorldSnapshot and Engine::Project::EngineProjectHost.
// - Provides CapabilityTraitRegistry to map unit/entity archetype IDs to
//   composable capability bitflags (Organic, Mechanical, Robotic, Detector,
//   Cloakable, Flyer, Worker, Hero, Spellcaster, Regenerates).
// ============================================================================

struct Transform2DComponent
{
    Core::Math::Vec2 Position{0.0f, 0.0f};
    Core::Math::Vec2 Scale{1.0f, 1.0f};
    float RotationRadians = 0.0f;
    uint8_t Direction32 = 0;
    int32_t ElevationLevel = 0;
};

struct Kinematics2DComponent
{
    Core::Math::Vec2 Velocity{0.0f, 0.0f};
    float MaxSpeedPxPerSec = 120.0f;
    float AccelerationPxPerSec2 = 400.0f;
    uint8_t TurnRadiusUnits = 20;
    bool Flying = false;
};

struct Sprite2DComponent
{
    uint32_t FlingyID = 0;
    uint32_t SpriteID = 0;
    uint32_t ImageID = 0;
    uint32_t IScriptControllerID = 0;
    int32_t CurrentFrameSet = 0;
    int32_t VerticalOffsetPx = 0;
    bool FlipHorizontal = false;
    bool Visible = true;
};

struct HealthAndShieldComponent
{
    int32_t HitPoints = 100;
    int32_t MaxHitPoints = 100;
    int32_t ShieldPoints = 0;
    int32_t MaxShieldPoints = 0;
    int32_t ArmorLevel = 0;
    bool ShieldsEnabled = false;
    bool Invulnerable = false;
};

enum class CapabilityFlags2D : uint32_t
{
    None              = 0u,
    Organic           = 1u << 0,
    Mechanical        = 1u << 1,
    Robotic           = 1u << 2,
    Flyer             = 1u << 3,
    Worker            = 1u << 4,
    Building          = 1u << 5,
    Addon             = 1u << 6,
    Detector          = 1u << 7,
    Cloakable         = 1u << 8,
    Spellcaster       = 1u << 9,
    Regenerates       = 1u << 10,
    Hero              = 1u << 11,
    ResourceContainer = 1u << 12
};

constexpr inline CapabilityFlags2D operator|(CapabilityFlags2D A, CapabilityFlags2D B) noexcept
{
    return static_cast<CapabilityFlags2D>(static_cast<uint32_t>(A) | static_cast<uint32_t>(B));
}

constexpr inline CapabilityFlags2D operator&(CapabilityFlags2D A, CapabilityFlags2D B) noexcept
{
    return static_cast<CapabilityFlags2D>(static_cast<uint32_t>(A) & static_cast<uint32_t>(B));
}

constexpr inline bool HasCapabilityFlag(CapabilityFlags2D Mask, CapabilityFlags2D Flag) noexcept
{
    return (static_cast<uint32_t>(Mask) & static_cast<uint32_t>(Flag)) != 0u;
}

class CapabilityTraitRegistry
{
public:
    void Clear() noexcept
    {
        mTraitsByTypeId.clear();
    }

    void RegisterTraits(uint32_t ArchetypeTypeID, CapabilityFlags2D Flags)
    {
        mTraitsByTypeId[ArchetypeTypeID] = Flags;
    }

    CapabilityFlags2D GetTraits(uint32_t ArchetypeTypeID) const noexcept
    {
        auto It = mTraitsByTypeId.find(ArchetypeTypeID);
        return It != mTraitsByTypeId.end() ? It->second : CapabilityFlags2D::None;
    }

    bool HasTrait(uint32_t ArchetypeTypeID, CapabilityFlags2D Flag) const noexcept
    {
        return HasCapabilityFlag(GetTraits(ArchetypeTypeID), Flag);
    }

    size_t ImportFromModernRegistry(const Resource::ModernAssetRegistry& Registry)
    {
        size_t Imported = 0;
        for (const auto& Unit : Registry.GetUnits())
        {
            CapabilityFlags2D Flags = CapabilityFlags2D::None;
            const uint32_t Mask = Unit.SpecialAbilityBitmask;
            if ((Mask & 0x00000001u) != 0u) Flags = Flags | CapabilityFlags2D::Building;
            if ((Mask & 0x00000002u) != 0u) Flags = Flags | CapabilityFlags2D::Addon;
            if ((Mask & 0x00000004u) != 0u || Unit.LocomotionMode == "Flyer")
                Flags = Flags | CapabilityFlags2D::Flyer;
            if ((Mask & 0x00000008u) != 0u) Flags = Flags | CapabilityFlags2D::Worker;
            if ((Mask & 0x00000040u) != 0u) Flags = Flags | CapabilityFlags2D::Hero;
            if ((Mask & 0x00000080u) != 0u) Flags = Flags | CapabilityFlags2D::Regenerates;
            if ((Mask & (0x00000200u | 0x00200000u)) != 0u)
                Flags = Flags | CapabilityFlags2D::Cloakable;
            if ((Mask & 0x00002000u) != 0u) Flags = Flags | CapabilityFlags2D::ResourceContainer;
            if ((Mask & 0x00010000u) != 0u) Flags = Flags | CapabilityFlags2D::Organic;
            if ((Mask & (0x00100000u | 0x00200000u)) != 0u) Flags = Flags | CapabilityFlags2D::Spellcaster;
            if ((Mask & (0x00008000u | 0x00800000u)) != 0u) Flags = Flags | CapabilityFlags2D::Detector;
            if ((Mask & (0x00004000u | 0x02000000u | 0x20000000u)) != 0u)
                Flags = Flags | CapabilityFlags2D::Robotic;
            if ((Mask & 0x40000000u) != 0u) Flags = Flags | CapabilityFlags2D::Mechanical;

            RegisterTraits(Unit.UnitID, Flags);
            ++Imported;
        }
        return Imported;
    }

    size_t Size() const noexcept { return mTraitsByTypeId.size(); }

private:
    std::unordered_map<uint32_t, CapabilityFlags2D> mTraitsByTypeId;
};

} // namespace Engine::ECS
