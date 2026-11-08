#pragma once

#include "Client/ClientRuntime.h"
#include "Main/GameGuiPresentation.h"
#include "Main/AppRuntime.h"
#include "Main/CampaignRuntime.h"
// === Consolidated from src/Main/GameInteractionSmokeRuntime.h ===
#include "Client/ClientRuntime.h"
#include "Client/Selection.h"
#include "Resource/GameDataLoader.h"
#include "Core/Utils/Logger.h"
#include "Game/Components.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/AnimationSystem.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/ProductionSystem.h"
#include "Main/CampaignRuntime.h"
#include "Main/GameCommandDispatchRuntime.h"
#include "Main/GameSession.h"
#include "Main/GameGuiPresentation.h"

#if ENABLE_GUI_UI
#include "third_party/sokol/sokol_app.h"
#endif

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Game::Client::GameLoop
{

struct GameInteractionSmokeState
{
    bool Attempted = false;
};

struct GameInteractionDemoState
{
    enum class Stage
    {
        Init,
        HarvestLoop,
        ProductionQueue,
        Construction,
        GasConstruction,
        GasHarvest,
        BarracksConstruction,
        MarineProduction,
        Research,
        FullRoster,
        Observe,
        Done,
        Failed
    };

    bool Enabled = false;
    bool FrameProbeLogged = false;
    bool Started = false;
    Stage CurrentStage = Stage::Init;
    float StageSeconds = 0.0f;
    int WorkerID = -1;
    int ResourceID = -1;
    int DepotID = -1;
    int TrainedUnitStartCount = 0;
    int BuildSiteID = -1;
    int RefineryID = -1;
    int GasWorkerID = -1;
    int BarracksID = -1;
    int MarineStartCount = 0;
    int EngineeringBayID = -1;
    int ResearchStartLevel = 0;
    int FocusEntityID = -1;
    std::vector<int> FullRosterEntityIDs;
    int FullRosterBuildingCount = 0;
    int FullRosterUnitCount = 0;
};


#if !defined(STARCRAFT_DISABLE_SMOKE_HARNESS)
#include "../../tests/Smoke/GameInteractionSmokeHarness.inl"
#else
inline void RunInteractionSmokeIfRequested(GameInteractionSmokeState& State,
                                           GameSession& Session,
                                           CampaignRuntime& Campaign,
                                           ClientRuntime& Client,
                                           GuiRuntime& Gui,
                                           UI::Hud& Hud)
{
    (void)State; (void)Session; (void)Campaign; (void)Client; (void)Gui; (void)Hud;
}

inline float RunInteractionDemoIfRequested(GameInteractionDemoState& State,
                                           GameSession& Session,
                                           ClientRuntime& Client,
                                           UI::Hud& Hud,
                                           float Dt)
{
    (void)State; (void)Session; (void)Client; (void)Hud; (void)Dt;
    return 1.0f;
}
#endif

} // namespace Game::Client::GameLoop

#include "Main/GameMenuPresentation.h"
#include "Main/GameSession.h"
#include "UI/Hud.h"

namespace Game::Client::GameLoop
{

struct GameAppRuntimeState
{
    AppRuntime Runtime;
    GameAudioRuntime Audio;
    GameMenuRuntime Menu;
    GuiRuntime Gui;
};

struct GameClientRuntimeState
{
    ClientRuntime Runtime;
    UI::Hud Hud;
};

struct GameSessionRuntimeState
{
    GameSession Game;
    CampaignRuntime Campaign;
};

struct GameRuntimeState
{
    GameAppRuntimeState App;
    GameClientRuntimeState Client;
    GameSessionRuntimeState Session;
    GameInteractionSmokeState InteractionSmoke;
    GameInteractionDemoState InteractionDemo;
};

} // namespace Game::Client::GameLoop
