#pragma once

#include <memory>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "Core/ECS/SystemManager.h"
#include "Game/Trigger/SCRTriggerParser.h"
#include "Game/Trigger/TriggerVM.h"

namespace Game::Campaign
{

static constexpr uint32_t kNoPortraitUnitID = UINT32_MAX;

struct Objective
{
    std::string Description;
    bool Complete = false;
};

struct CampaignMessage
{
    std::string Text;
    uint8_t ActionID = 0;
    int PlayerID = 0;
    uint32_t Data = 0;
    uint32_t PortraitUnitID = kNoPortraitUnitID;
    std::string SoundPath;
    Core::Math::Vec2 Position;
    float TimeRemaining = 0.0f;
};

struct CampaignLeaderboard
{
    bool Active = false;
    uint8_t ActionID = 0;
    std::string Label;
    uint16_t UnitID = 0;
    int PlayerID = 0;
    int CurrentValue = 0;
    bool IncludeComputerPlayers = false;
};

enum class CampaignState : uint8_t
{
    Playing,
    Victory,
    Defeat,
    Draw
};

class CampaignController
{
public:
    CampaignController();
    ~CampaignController();

    bool Init(Core::ECS::EntityManager& EM, const Game::Trigger::SCRTriggerData* ScriptData);
    void Update(Core::ECS::EntityManager& EM, float DeltaTime);
    void Reset();

    bool IsActive() const { return mActive; }
    CampaignState GetState() const { return mState; }
    int GetLocalPlayerID() const { return mLocalPlayerID; }
    const std::vector<Objective>& GetObjectives() const { return mObjectives; }
    const std::vector<CampaignMessage>& GetMessages() const { return mMessages; }
    const CampaignLeaderboard& GetLeaderboard() const { return mLeaderboard; }
    float GetStateTimer() const { return mStateTimer; }
    const std::string& GetMissionName() const { return mMissionName; }
    using SoundCallback = std::function<void(const CampaignMessage&)>;
    void SetSoundCallback(SoundCallback Callback) { mSoundCallback = std::move(Callback); }

    Trigger::TriggerVM* GetTriggerVM() { return mTriggerVM.get(); }
    const Trigger::TriggerVM* GetTriggerVM() const { return mTriggerVM.get(); }

private:
    void SyncStateFromTriggerVM();
    void SyncObjectivesFromTriggerVM();
    void SyncLeaderboardFromTriggerVM(Core::ECS::EntityManager& EM, float DeltaTime);
    void SyncMessagesFromTriggerVM(float DeltaTime);
    void RefreshLeaderboardValue(Core::ECS::EntityManager& EM);

    bool mActive = false;
    CampaignState mState = CampaignState::Playing;
    int mLocalPlayerID = 0;
    std::string mMissionName = "StarCraft Scenario";
    std::vector<Objective> mObjectives;
    std::vector<CampaignMessage> mMessages;
    CampaignLeaderboard mLeaderboard;
    float mStateTimer = 0.0f;
    float mLeaderboardRefreshTimer = 0.0f;
    uint32_t mActivePortraitUnitID = kNoPortraitUnitID;
    size_t mSyncedObjectiveSourceCount = 0;
    SoundCallback mSoundCallback;
    std::unique_ptr<Trigger::TriggerVM> mTriggerVM;
};

} // namespace Game::Campaign
