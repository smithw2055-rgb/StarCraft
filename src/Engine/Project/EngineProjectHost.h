#pragma once

#include <cstdint>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Utils/JsonLite.h"
#include "Engine/EngineApp.h"

namespace Engine::Project
{

// ============================================================================
// ProjectManifest & IGameModule (Phase 59):
// Referenced from Godot 4 (project.godot + MainLoop / GDExtension) &
// Unity (ProjectSettings + Package/Assembly Definition Modular Architecture):
// - Formalizes the separation between the reusable "Engine Foundation"
//   (Core + Engine Server Matrix + Render + Resource) and any concrete
//   "Experimental Game Project Module" (such as StarCraft: Remastered or a
//   new 2D/RTS game built on top of the engine).
// ============================================================================
struct ProjectManifest
{
    std::string ProjectName = "Generic2DEngineProject";
    std::string ProjectVersion = "1.0.0";
    std::string EngineVersion = "2.0.0-ModernEngine";
    float FixedTickHz = 24.0f;
    int MaxSubStepsPerFrame = 4;
    std::string AssetMountRoot = "assets-R";
    std::string StartupSceneUri = "res://04_Terrain_Environment_And_Shaders/Byways_Chk_Full_Scene.json";
    bool AuthoritativeRollback = true;
    bool StrictHDOnly = true;
    uint32_t TotalHDPBRBundles = 0;
    uint32_t TotalTilesetGpuLuts = 0;

    bool LoadFromMasterManifestFile(const std::string& FilePath)
    {
        std::ifstream In(FilePath);
        if (!In.is_open())
            return false;
        std::ostringstream SS;
        SS << In.rdbuf();
        return LoadFromMasterManifestJson(SS.str());
    }

    bool LoadFromMasterManifestJson(std::string_view JsonText)
    {
        const auto Root = Core::Utils::JsonParser::ParseString(std::string(JsonText));
        if (!Root.IsObject())
            return false;
        const auto& Summary = Root["hd_batch_export_summary"];
        if (Summary.IsObject())
        {
            StrictHDOnly = Summary["strict_hd_only"].AsBool(true);
            TotalHDPBRBundles = static_cast<uint32_t>(Summary["total_hd_pbr_unit_bundles"].AsInt(247));
            TotalTilesetGpuLuts = static_cast<uint32_t>(Summary["hd_tileset_gpu_luts"].AsInt(8));
            return true;
        }
        return false;
    }
};

// ============================================================================
// IGameModule: First-Class Game Project Module Interface
// Extends Engine::IPlugin with structured project configuration, VFS mounting,
// and initial scene bootstrapping so any game project can be hosted on EngineApp.
// ============================================================================
class IGameModule : public Engine::IPlugin
{
public:
    ~IGameModule() override = default;

    virtual std::string_view GetModuleVersion() const { return "1.0.0"; }
    virtual void ConfigureProject(ProjectManifest& /*Manifest*/) {}
    virtual void OnMountVFS(Engine::VFS::VirtualFileSystemServer& /*VFS*/,
                            const ProjectManifest& /*Manifest*/) {}
    virtual bool OnLoadInitialScene(Engine::EngineApp& /*App*/,
                                    const ProjectManifest& /*Manifest*/)
    {
        return true;
    }
};

// ============================================================================
// EngineProjectHost: Turnkey Host Combining EngineApp + ProjectManifest + IGameModule
// ============================================================================
class EngineProjectHost
{
public:
    explicit EngineProjectHost(ProjectManifest Manifest = {})
        : mManifest(std::move(Manifest))
        , mApp(1.0f / std::max(1.0f, mManifest.FixedTickHz), mManifest.MaxSubStepsPerFrame)
    {
    }

    template <typename GameModuleT, typename... Args>
    GameModuleT& MountGameModule(Args&&... InArgs)
    {
        auto& ModuleRef = mApp.AddPlugin<GameModuleT>(std::forward<Args>(InArgs)...);
        ModuleRef.ConfigureProject(mManifest);
        mApp.ConfigureFixedStep(1.0f / std::max(1.0f, mManifest.FixedTickHz), mManifest.MaxSubStepsPerFrame);
        mApp.SetAutoRecordRollbackSnapshots(mManifest.AuthoritativeRollback);

        if (!mManifest.AssetMountRoot.empty())
        {
            mApp.Context().VFS().MountDirectory("res://", mManifest.AssetMountRoot, /*Priority=*/0, /*ReadOnly=*/true);
            const std::string MasterManifestPath =
                mApp.Context().VFS().ResolvePhysicalPath("res://00_Schema_And_Pipeline/Master_Refactor_Manifest.json");
            mManifest.LoadFromMasterManifestFile(MasterManifestPath);
        }

        ModuleRef.OnMountVFS(mApp.Context().VFS(), mManifest);
        mMountedModules.push_back(&ModuleRef);
        return ModuleRef;
    }

    bool BootProject(bool LoadInitialScene = true)
    {
        mApp.Initialize();
        if (LoadInitialScene)
        {
            for (auto* Mod : mMountedModules)
            {
                if (Mod && !Mod->OnLoadInitialScene(mApp, mManifest))
                    return false;
            }
        }
        return true;
    }

    int StepFrame(float WallDeltaSeconds)
    {
        return mApp.StepFrame(WallDeltaSeconds);
    }

    void ShutdownProject()
    {
        mApp.Shutdown();
    }

    Engine::EngineApp& App() noexcept { return mApp; }
    const Engine::EngineApp& App() const noexcept { return mApp; }

    Engine::EngineContext& Context() noexcept { return mApp.Context(); }
    const Engine::EngineContext& Context() const noexcept { return mApp.Context(); }

    ProjectManifest& Manifest() noexcept { return mManifest; }
    const ProjectManifest& Manifest() const noexcept { return mManifest; }

    size_t GetMountedModuleCount() const noexcept { return mMountedModules.size(); }

private:
    ProjectManifest mManifest;
    Engine::EngineApp mApp;
    std::vector<IGameModule*> mMountedModules;
};

} // namespace Engine::Project
