#pragma once

#include <cstdint>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Core/Utils/JsonLite.h"
#include "Engine/EngineApp.h"

namespace Engine::Project
{

// ============================================================================
// ProjectManifest & IGameModule (Phases 59–77):
// Referenced from Godot 4 (project.godot + MainLoop / GDExtension) &
// Unity (ProjectSettings + Package/Assembly Definition Modular Architecture):
// - Formalizes the separation between the reusable "Engine Foundation"
//   (Core + 19-Server Matrix + Render + Resource) and any concrete
//   "Experimental Game Project Module" (such as StarCraft: Remastered or a
//   new 2D/RTS game built on top of the engine).
// ============================================================================
struct ProjectManifest
{
    std::string ProjectName = "Generic2DEngineProject";
    std::string ProjectVersion = "1.0.0";
    std::string EngineVersion = "3.0.0-ModernEngine-Phase77";
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
        ConfigureAndMountModule(ModuleRef);
        return ModuleRef;
    }

    void ConfigureAndMountModule(IGameModule& ModuleRef)
    {
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

// ============================================================================
// EngineProjectRegistry (Phase 77):
// Referenced from Godot 4 ProjectManager & Unity Hub / Multi-Project Workspace:
// - Allows registering multiple independent game projects (e.g., "starcraft-remastered",
//   "sandbox-tower-defense", "orbital-skirmish") and launching any registered
//   game project onto a clean EngineProjectHost instance.
// ============================================================================
struct RegisteredProjectEntry
{
    std::string ProjectID;
    std::string DisplayTitle;
    std::string GenreTag;
    std::function<std::unique_ptr<EngineProjectHost>(bool /*LoadInitialScene*/)> HostBuilder;
};

class EngineProjectRegistry
{
public:
    EngineProjectRegistry() = default;

    template <typename GameModuleT, typename... Args>
    void RegisterProject(std::string ProjectID,
                         std::string DisplayTitle,
                         std::string GenreTag,
                         Args... DefaultArgs)
    {
        RegisteredProjectEntry Entry;
        Entry.ProjectID = ProjectID;
        Entry.DisplayTitle = std::move(DisplayTitle);
        Entry.GenreTag = std::move(GenreTag);
        Entry.HostBuilder = [DefaultArgs...](bool LoadScene) {
            auto Host = std::make_unique<EngineProjectHost>();
            Host->template MountGameModule<GameModuleT>(DefaultArgs...);
            Host->BootProject(LoadScene);
            return Host;
        };
        mProjects[std::move(ProjectID)] = std::move(Entry);
    }

    std::unique_ptr<EngineProjectHost> LaunchProject(const std::string& ProjectID,
                                                     bool LoadInitialScene = true) const
    {
        auto It = mProjects.find(ProjectID);
        if (It == mProjects.end() || !It->second.HostBuilder)
            return nullptr;
        return It->second.HostBuilder(LoadInitialScene);
    }

    bool HasProject(const std::string& ProjectID) const
    {
        return mProjects.find(ProjectID) != mProjects.end();
    }

    size_t GetRegisteredProjectCount() const noexcept { return mProjects.size(); }

    std::vector<std::string> ListProjectIDs() const
    {
        std::vector<std::string> Out;
        Out.reserve(mProjects.size());
        for (const auto& [Id, _] : mProjects)
            Out.push_back(Id);
        return Out;
    }

private:
    std::unordered_map<std::string, RegisteredProjectEntry> mProjects;
};

} // namespace Engine::Project
