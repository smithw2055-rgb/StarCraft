#pragma once

#include <charconv>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>

#include "Game/Systems/AnimationSystem.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/BulletSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/NukeAndSpiderMineSystem.h"
#include "Game/Systems/ProductionSystem.h"
#include "Game/Systems/SpellSystem.h"
#include "Game/Systems/ZergSystem.h"
#include "Resource/ModernAssetRegistry.h"

namespace Game::Systems
{

// ============================================================================
// SystemAssetBridge: Unified Cloud Asset (ModernAssetRegistry) Resolver Bridge
// Eliminates redundant manual resolver wiring across 8 gameplay systems by
// binding all unit/building/turret/projectile/socket resolvers directly to
// the cloud-converted ModernAssetRegistry (StarCraft/assets-R) in a single call.
// ============================================================================
class SystemAssetBridge
{
public:
    // Parses "anim/main_239.anim" -> 239
    static uint32_t ParseHDAnimImageID(std::string_view AnimPath) noexcept
    {
        if (AnimPath.empty())
            return UINT32_MAX;
        const size_t Underscore = AnimPath.rfind('_');
        const size_t Dot = AnimPath.rfind('.');
        if (Underscore == std::string_view::npos || Dot == std::string_view::npos || Dot <= Underscore + 1)
            return UINT32_MAX;

        std::string_view Digits = AnimPath.substr(Underscore + 1, Dot - (Underscore + 1));
        uint32_t Parsed = UINT32_MAX;
        auto [Ptr, Ec] = std::from_chars(Digits.data(), Digits.data() + Digits.size(), Parsed);
        if (Ec == std::errc{} && Ptr == Digits.data() + Digits.size())
            return Parsed;
        return UINT32_MAX;
    }

    static void BindModernAssetRegistry(const Resource::ModernAssetRegistry& Registry)
    {
        if (!Registry.IsLoaded())
            return;

        // Share an immutable snapshot of the registry so static system resolvers
        // never dangle even if the caller's ModernAssetRegistry is stack-scoped.
        auto RegistryPtr = std::make_shared<const Resource::ModernAssetRegistry>(Registry);

        auto ImageResolver = [RegistryPtr](int ChkUnitID) -> uint32_t
        {
            if (ChkUnitID < 0)
                return 0u;
            if (const auto* Unit = RegistryPtr->FindUnitByID(static_cast<uint32_t>(ChkUnitID)))
                return Unit->ImageID;
            return static_cast<uint32_t>(ChkUnitID);
        };

        auto ConstructionResolver = [RegistryPtr](int ChkUnitID) -> uint32_t
        {
            if (ChkUnitID < 0)
                return UINT32_MAX;
            if (const auto* Unit = RegistryPtr->FindUnitByID(static_cast<uint32_t>(ChkUnitID)))
                return ParseHDAnimImageID(Unit->ConstructionHDAnim);
            return UINT32_MAX;
        };

        auto SubunitResolver = [RegistryPtr](int ChkUnitID) -> uint32_t
        {
            if (ChkUnitID < 0)
                return UINT32_MAX;
            const auto* Unit = RegistryPtr->FindUnitByID(static_cast<uint32_t>(ChkUnitID));
            if (!Unit || !Unit->HasTurretAttachment || Unit->TurretUnitID >= 228)
                return UINT32_MAX;
            if (const auto* Subunit = RegistryPtr->FindUnitByID(Unit->TurretUnitID))
                return Subunit->ImageID;
            return UINT32_MAX;
        };

        ProductionSystem::SetImageResolver(ImageResolver);
        BuildingSystem::SetImageResolver(ImageResolver);
        CommandSystem::SetImageResolver(ImageResolver);
        NukeAndSpiderMineSystem::SetImageResolver(ImageResolver);
        SpellSystem::SetImageResolver(ImageResolver);
        ZergSystem::SetImageResolver(ImageResolver);

        BuildingSystem::SetConstructionImageResolver(ConstructionResolver);

        ProductionSystem::SetSubunitImageResolver(SubunitResolver);
        BuildingSystem::SetSubunitImageResolver(SubunitResolver);
        CommandSystem::SetSubunitImageResolver(SubunitResolver);

        AnimationSystem::SetWeaponImageResolver([RegistryPtr](uint32_t WeaponID) -> uint32_t
        {
            if (const auto* Weap = RegistryPtr->FindWeaponByID(WeaponID))
                return ParseHDAnimImageID(Weap->ProjectileHDAnim);
            return UINT32_MAX;
        });

        AnimationSystem::SetImageAttachmentPersistenceResolver([RegistryPtr](uint32_t ImageID) -> bool
        {
            if (const auto* Img = RegistryPtr->FindHDImageByID(ImageID))
                return (Img->RoleFlags & (8 | 16)) != 0;
            return false;
        });

        BulletSystem::SetVisualResolver(
            [RegistryPtr](int ChkUnitID, bool PreferAirWeapon) -> std::optional<BulletSystem::VisualSpec>
            {
                if (ChkUnitID < 0)
                    return std::nullopt;
                const auto* Unit = RegistryPtr->FindUnitByID(static_cast<uint32_t>(ChkUnitID));
                if (!Unit)
                    return std::nullopt;

                uint32_t WeaponID = 130;
                if (PreferAirWeapon && Unit->HasAirWeapon)
                    WeaponID = Unit->AirWeaponID;
                else if (Unit->HasGroundWeapon)
                    WeaponID = Unit->GroundWeaponID;
                else if (Unit->HasAirWeapon)
                    WeaponID = Unit->AirWeaponID;

                if (WeaponID >= 130)
                    return std::nullopt;

                const auto* Weap = RegistryPtr->FindWeaponByID(WeaponID);
                if (!Weap)
                    return std::nullopt;

                BulletSystem::VisualSpec Out{};
                Out.ImageID = ParseHDAnimImageID(Weap->ProjectileHDAnim);
                if (Out.ImageID == UINT32_MAX)
                    return std::nullopt;
                Out.ForwardOffset = static_cast<float>(Weap->MuzzleSpawnOffsetPx[0]);
                Out.UpwardOffset = static_cast<float>(Weap->MuzzleSpawnOffsetPx[1]);
                Out.HasInitAnimation = true;
                Out.HasImpactAnimation = true;
                return Out;
            });
    }

    static void UnbindAll()
    {
        ProductionSystem::SetImageResolver({});
        BuildingSystem::SetImageResolver({});
        CommandSystem::SetImageResolver({});
        NukeAndSpiderMineSystem::SetImageResolver({});
        SpellSystem::SetImageResolver({});
        ZergSystem::SetImageResolver({});

        BuildingSystem::SetConstructionImageResolver({});

        ProductionSystem::SetSubunitImageResolver({});
        BuildingSystem::SetSubunitImageResolver({});
        CommandSystem::SetSubunitImageResolver({});

        AnimationSystem::SetWeaponImageResolver({});
        AnimationSystem::SetImageAttachmentPersistenceResolver({});
        BulletSystem::SetVisualResolver({});
    }
};

} // namespace Game::Systems
