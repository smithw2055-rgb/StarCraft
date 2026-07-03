#pragma once

#include <memory>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "Core/ECS/SystemManager.h"
#include "Game/Trigger/SCRTriggerParser.h"
#include "Game/Trigger/TriggerVM.h"

namespace Game::Campaign
{

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
    uint32_t PortraitUnitID = 0;
    std::string SoundPath;
    Core::Math::Vec2 Position;
    float TimeRemaining = 0.0f;
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
    const std::vector<Objective>& GetObjectives() const { return mObjectives; }
    const std::vector<CampaignMessage>& GetMessages() const { return mMessages; }
    float GetStateTimer() const { return mStateTimer; }
    const std::string& GetMissionName() const { return mMissionName; }
    using SoundCallback = std::function<void(const CampaignMessage&)>;
    void SetSoundCallback(SoundCallback Callback) { mSoundCallback = std::move(Callback); }

    Trigger::TriggerVM* GetTriggerVM() { return mTriggerVM.get(); }
    const Trigger::TriggerVM* GetTriggerVM() const { return mTriggerVM.get(); }

private:
    void SyncStateFromTriggerVM();
    void SyncObjectivesFromTriggerVM();
    void SyncMessagesFromTriggerVM(float DeltaTime);

    bool mActive = false;
    CampaignState mState = CampaignState::Playing;
    std::string mMissionName = "StarCraft Scenario";
    std::vector<Objective> mObjectives;
    std::vector<CampaignMessage> mMessages;
    float mStateTimer = 0.0f;
    uint32_t mActivePortraitUnitID = 0;
    size_t mSyncedObjectiveSourceCount = 0;
    SoundCallback mSoundCallback;
    std::unique_ptr<Trigger::TriggerVM> mTriggerVM;
};

} // namespace Game::Campaign
