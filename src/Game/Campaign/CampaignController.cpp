#include "Game/Campaign/CampaignController.h"

#include "Core/Utils/Logger.h"
#include "Game/Components.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>

namespace Game::Campaign
{
using Core::Utils::Logger;

namespace
{
    std::string TrimObjectiveLine(const std::string& Text)
    {
        size_t Begin = 0;
        size_t End = Text.size();
        while (Begin < End && std::isspace((unsigned char)Text[Begin]))
            ++Begin;
        while (End > Begin && std::isspace((unsigned char)Text[End - 1]))
            --End;

        std::string Line = Text.substr(Begin, End - Begin);
        if (Line.size() >= 2 &&
            (Line[0] == '-' || Line[0] == '*' || Line[0] == '+') &&
            std::isspace((unsigned char)Line[1]))
        {
            Line.erase(0, 2);
            return TrimObjectiveLine(Line);
        }

        if (Line.size() >= 4 && Line[0] == '[' && Line[2] == ']' &&
            std::isspace((unsigned char)Line[3]))
        {
            Line.erase(0, 4);
            return TrimObjectiveLine(Line);
        }

        return Line;
    }

    std::vector<std::string> SplitObjectiveText(const std::string& Text)
    {
        std::vector<std::string> BulletLines;
        std::vector<std::string> PlainLines;
        size_t Pos = 0;
        while (Pos <= Text.size())
        {
            size_t End = Text.find_first_of("\r\n", Pos);
            if (End == std::string::npos)
                End = Text.size();

            const std::string RawLine = Text.substr(Pos, End - Pos);
            std::string Trimmed = TrimObjectiveLine(RawLine);
            if (!Trimmed.empty())
            {
                size_t First = RawLine.find_first_not_of(" \t\r\n");
                const bool Bullet = First != std::string::npos &&
                    (RawLine[First] == '-' || RawLine[First] == '*' || RawLine[First] == '+');
                if (Bullet)
                    BulletLines.push_back(std::move(Trimmed));
                else
                    PlainLines.push_back(std::move(Trimmed));
            }

            Pos = End + 1;
            if (End == Text.size())
                break;
            if (Pos < Text.size() && Text[End] == '\r' && Text[Pos] == '\n')
                ++Pos;
        }

        return !BulletLines.empty() ? BulletLines : PlainLines;
    }

    bool ContainsObjectiveLine(const std::vector<std::string>& Lines, const std::string& Description)
    {
        return std::find(Lines.begin(), Lines.end(), Description) != Lines.end();
    }

    bool HasEnabledActionType(const Trigger::SCRTriggerData& Data, uint8_t ActionType)
    {
        for (const auto& Trigger : Data.Triggers)
        {
            for (const auto& Action : Trigger.Actions)
            {
                if (!Action.Disabled() && Action.ActionType == ActionType)
                    return true;
            }
        }
        return false;
    }

    bool HasMissionMessageAction(const Trigger::SCRTriggerData& Data)
    {
        constexpr uint8_t MessageActionTypes[] = {7, 8, 9, 29, 41, 47};
        for (uint8_t ActionType : MessageActionTypes)
        {
            if (HasEnabledActionType(Data, ActionType))
                return true;
        }
        return false;
    }
}

CampaignController::CampaignController()
    : mTriggerVM(std::make_unique<Trigger::TriggerVM>())
{
}

CampaignController::~CampaignController() = default;

bool CampaignController::Init(Core::ECS::EntityManager& EM, const Trigger::SCRTriggerData* ScriptData)
{
    (void)EM;

    mActive = false;
    mState = CampaignState::Playing;
    mLocalPlayerID = 0;
    mStateTimer = 0.0f;
    mActivePortraitUnitID = kNoPortraitUnitID;
    mSyncedObjectiveSourceCount = 0;
    mObjectives.clear();
    mMessages.clear();
    mLeaderboard = {};
    mMissionName = "StarCraft Scenario";
    mTriggerVM = std::make_unique<Trigger::TriggerVM>();

    if (!ScriptData || ScriptData->Triggers.empty())
    {
        Logger::Info("CampaignController", "No CHK TRIG data; campaign scripting inactive");
        return false;
    }

    for (int PlayerID = 0; PlayerID < 8; ++PlayerID)
    {
        if (ScriptData->Players[(size_t)PlayerID].Owner == 6)
        {
            mLocalPlayerID = PlayerID;
            break;
        }
    }

    std::string Name = ScriptData->GetString(ScriptData->ScenarioNameStringID);
    if (!Name.empty())
        mMissionName = Name;

    if (!mTriggerVM->LoadScript(*ScriptData))
    {
        Logger::Warning("CampaignController", "CHK TRIG data had no runnable triggers; campaign scripting inactive");
        return false;
    }

    if (!HasEnabledActionType(*ScriptData, 12))
        mObjectives.push_back({ "Eliminate all enemy forces.", false });

    if (!HasMissionMessageAction(*ScriptData))
    {
        CampaignMessage Message;
        Message.Text = mMissionName.empty() ? "Mission started" : mMissionName;
        Message.ActionID = 0;
        Message.PlayerID = mLocalPlayerID;
        Message.TimeRemaining = 3600.0f;
        mMessages.push_back(std::move(Message));
    }

    SyncObjectivesFromTriggerVM();
    mActive = true;
    Logger::Info("CampaignController",
        "Campaign scripting initialized: " + std::to_string(mTriggerVM->GetTriggerCount()) +
        " triggers, local_player=" + std::to_string(mLocalPlayerID));
    return true;
}

void CampaignController::Update(Core::ECS::EntityManager& EM, float DeltaTime)
{
    if (!mActive || !mTriggerVM)
        return;
    if (mState != CampaignState::Playing)
    {
        RefreshLeaderboardValue(EM);
        return;
    }

    mStateTimer += DeltaTime;
    mTriggerVM->Update(EM, DeltaTime);
    SyncLeaderboardFromTriggerVM(EM);
    SyncMessagesFromTriggerVM(DeltaTime);
    SyncObjectivesFromTriggerVM();
    SyncStateFromTriggerVM();
}

void CampaignController::Reset()
{
    mState = CampaignState::Playing;
    mStateTimer = 0.0f;
    mActivePortraitUnitID = kNoPortraitUnitID;
    mSyncedObjectiveSourceCount = 0;
    mObjectives.clear();
    mMessages.clear();
    mLeaderboard = {};
    if (mTriggerVM)
        mTriggerVM->Reset();
}

void CampaignController::SyncStateFromTriggerVM()
{
    if (!mTriggerVM)
        return;

    const CampaignState OldState = mState;
    auto Outcome = mTriggerVM->GetPlayerOutcome(mLocalPlayerID);
    switch (Outcome)
    {
        case Trigger::TriggerOutcome::Victory:
            mState = CampaignState::Victory;
            break;
        case Trigger::TriggerOutcome::Defeat:
            mState = CampaignState::Defeat;
            break;
        case Trigger::TriggerOutcome::Draw:
            mState = CampaignState::Draw;
            break;
        default:
            break;
    }

    if (mState != OldState)
    {
        mStateTimer = 0.0f;
        if (mState == CampaignState::Victory)
            for (auto& Objective : mObjectives)
                Objective.Complete = true;
    }
}

void CampaignController::SyncObjectivesFromTriggerVM()
{
    if (!mTriggerVM)
        return;

    const auto& VmObjectives = mTriggerVM->GetObjectives();
    if (VmObjectives.size() <= mSyncedObjectiveSourceCount)
        return;

    for (size_t I = mSyncedObjectiveSourceCount; I < VmObjectives.size(); ++I)
    {
        auto Lines = SplitObjectiveText(VmObjectives[I]);
        if (Lines.empty())
            continue;

        for (auto& Objective : mObjectives)
            if (!Objective.Complete && !ContainsObjectiveLine(Lines, Objective.Description))
                Objective.Complete = true;

        for (const auto& Line : Lines)
        {
            auto Existing = std::find_if(mObjectives.begin(), mObjectives.end(),
                [&](const Objective& Objective) { return Objective.Description == Line; });
            if (Existing == mObjectives.end())
                mObjectives.push_back({ Line, false });
        }
    }

    mSyncedObjectiveSourceCount = VmObjectives.size();
}

void CampaignController::SyncLeaderboardFromTriggerVM(Core::ECS::EntityManager& EM)
{
    if (!mTriggerVM)
        return;

    for (const auto& Event : mTriggerVM->GetEvents())
    {
        switch (Event.ActionID)
        {
            case 17: // Leaderboard control.
            {
                mLeaderboard.Active = true;
                mLeaderboard.ActionID = Event.ActionID;
                mLeaderboard.Label = Event.Text.empty() ? "Units" : Event.Text;
                mLeaderboard.UnitID = (uint16_t)Event.Data;
                mLeaderboard.PlayerID = Event.PlayerID;
                break;
            }
            case 32: // Leaderboard computer players.
                mLeaderboard.IncludeComputerPlayers = true;
                break;
            default:
                break;
        }
    }

    RefreshLeaderboardValue(EM);
}

void CampaignController::RefreshLeaderboardValue(Core::ECS::EntityManager& EM)
{
    if (!mLeaderboard.Active)
        return;

    int Count = 0;
    for (int EntityID : EM.GetEntitiesWith<Components::UnitComponent,
                                           Components::PlayerComponent>())
    {
        auto* Unit = EM.GetComponent<Components::UnitComponent>(EntityID);
        auto* Player = EM.GetComponent<Components::PlayerComponent>(EntityID);
        if (!Unit || !Player || Unit->HitPoints <= 0)
            continue;
        if (Unit->ChkUnitID != (int)mLeaderboard.UnitID)
            continue;
        if (!mLeaderboard.IncludeComputerPlayers && Player->PlayerID != mLeaderboard.PlayerID)
            continue;
        ++Count;
    }
    mLeaderboard.CurrentValue = Count;
}

void CampaignController::SyncMessagesFromTriggerVM(float DeltaTime)
{
    for (auto& Message : mMessages)
        Message.TimeRemaining -= DeltaTime;
    mMessages.erase(std::remove_if(mMessages.begin(), mMessages.end(),
        [](const CampaignMessage& Message) { return Message.TimeRemaining <= 0.0f; }),
        mMessages.end());

    if (!mTriggerVM)
        return;

    for (const auto& Event : mTriggerVM->GetEvents())
    {
        if (Event.ActionID == 29)
        {
            mActivePortraitUnitID = Event.Data;
            for (auto It = mMessages.rbegin(); It != mMessages.rend(); ++It)
            {
                if (It->PortraitUnitID == kNoPortraitUnitID)
                {
                    It->PortraitUnitID = Event.Data;
                    break;
                }
            }

            CampaignMessage Message;
            Message.ActionID = Event.ActionID;
            Message.PlayerID = Event.PlayerID;
            Message.Data = Event.Data;
            Message.PortraitUnitID = Event.Data;
            Message.Position = Event.Position;
            Message.TimeRemaining = Event.DurationMs > 0
                ? std::max(1.0f, (float)Event.DurationMs / 1000.0f)
                : 3.0f;
            mMessages.push_back(std::move(Message));
            continue;
        }

        switch (Event.ActionID)
        {
            case 7:  // Transmission.
            case 8:  // Play WAV.
            case 9:  // Display text.
            case 41: // Set next scenario / briefing-style text on some maps.
            case 47: // Display text message.
            {
                CampaignMessage Message;
                Message.Text = Event.Text;
                Message.ActionID = Event.ActionID;
                Message.PlayerID = Event.PlayerID;
                Message.Data = Event.Data;
                if (Event.ActionID == 7)
                    Message.PortraitUnitID = Event.Data;
                else
                    Message.PortraitUnitID = mActivePortraitUnitID;
                Message.SoundPath = Event.SoundPath;
                Message.Position = Event.Position;
                Message.TimeRemaining = Event.DurationMs > 0
                    ? std::max(2.0f, (float)Event.DurationMs / 1000.0f)
                    : (Event.ActionID == 8 ? 3.0f : 8.0f);
                if (!Message.SoundPath.empty() && mSoundCallback)
                    mSoundCallback(Message);
                if (Event.ActionID != 8 || !Message.Text.empty())
                    mMessages.push_back(std::move(Message));
                break;
            }
            default:
                break;
        }
    }

    if (mMessages.size() > 6)
        mMessages.erase(mMessages.begin(), mMessages.end() - 6);
    mTriggerVM->ClearEvents();
}

} // namespace Game::Campaign
