#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Core/Input/InputActionMap.h"

namespace Engine::Input
{

// ============================================================================
// CommandRouter (Phase 39):
// - Connects Godot-style semantic InputActionMap actions to deterministic
//   tick-stamped command queues for RTS/2D games.
// - Decouples raw physical keys/buttons from simulation command packets.
// ============================================================================
template <typename CommandPayloadT>
class CommandRouter
{
public:
    struct StampedCommand
    {
        uint32_t TargetTick = 0;
        uint8_t PlayerID = 0;
        std::string SourceAction;
        CommandPayloadT Payload{};
    };

    using ActionTranslatorFn = std::function<bool(uint8_t LocalPlayerID, CommandPayloadT& OutPayload)>;

    void SetLocalPlayerID(uint8_t PlayerID) noexcept { mLocalPlayerID = PlayerID; }
    uint8_t GetLocalPlayerID() const noexcept { return mLocalPlayerID; }

    void SetInputLatencyTicks(uint32_t LatencyTicks) noexcept { mInputLatencyTicks = LatencyTicks; }
    uint32_t GetInputLatencyTicks() const noexcept { return mInputLatencyTicks; }

    void BindActionTranslator(std::string ActionName, ActionTranslatorFn Translator)
    {
        mTranslators[std::move(ActionName)] = std::move(Translator);
    }

    size_t PollActionIfTriggered(const Core::Input::InputActionMap& ActionMap,
                                 const std::string& ActionName,
                                 uint32_t CurrentTick)
    {
        if (!ActionMap.IsActionTriggered(ActionName))
            return 0;
        return EmitActionCommand(ActionName, CurrentTick);
    }

    size_t TriggerActionByKey(const Core::Input::InputActionMap& ActionMap,
                              const std::string& ActionName,
                              int PressedKeyCode,
                              uint32_t CurrentTick,
                              Core::Input::InputModifierMask ActiveMods = Core::Input::InputModifierMask::None)
    {
        const auto* Entry = ActionMap.FindAction(ActionName);
        if (!Entry || !Entry->Enabled)
            return 0;

        auto Matches = [&](const Core::Input::InputBinding& B) -> bool {
            if (!B.IsValid() || B.KeyCode != PressedKeyCode)
                return false;
            return B.Modifiers == Core::Input::InputModifierMask::Any || B.Modifiers == ActiveMods;
        };

        if (!Matches(Entry->Primary) && !Matches(Entry->Secondary))
            return 0;

        return EmitActionCommand(ActionName, CurrentTick);
    }

    size_t EmitActionCommand(const std::string& ActionName, uint32_t CurrentTick)
    {
        auto It = mTranslators.find(ActionName);
        if (It == mTranslators.end() || !It->second)
            return 0;

        CommandPayloadT Payload{};
        if (!It->second(mLocalPlayerID, Payload))
            return 0;

        StampedCommand Cmd{};
        Cmd.TargetTick = CurrentTick + mInputLatencyTicks;
        Cmd.PlayerID = mLocalPlayerID;
        Cmd.SourceAction = ActionName;
        Cmd.Payload = std::move(Payload);
        mPendingCommands.push_back(std::move(Cmd));
        ++mTotalRoutedCommands;
        return 1;
    }

    void EnqueueDirectCommand(uint32_t TargetTick, uint8_t PlayerID, CommandPayloadT Payload)
    {
        StampedCommand Cmd{};
        Cmd.TargetTick = TargetTick;
        Cmd.PlayerID = PlayerID;
        Cmd.SourceAction = "Direct";
        Cmd.Payload = std::move(Payload);
        mPendingCommands.push_back(std::move(Cmd));
        ++mTotalRoutedCommands;
    }

    std::vector<StampedCommand> DrainCommandsForTick(uint32_t Tick)
    {
        std::vector<StampedCommand> Ready;
        auto It = mPendingCommands.begin();
        while (It != mPendingCommands.end())
        {
            if (It->TargetTick <= Tick)
            {
                Ready.push_back(std::move(*It));
                It = mPendingCommands.erase(It);
            }
            else
            {
                ++It;
            }
        }
        return Ready;
    }

    size_t GetPendingCount() const noexcept { return mPendingCommands.size(); }
    uint64_t GetTotalRoutedCommands() const noexcept { return mTotalRoutedCommands; }

private:
    uint8_t mLocalPlayerID = 0;
    uint32_t mInputLatencyTicks = 2;
    std::unordered_map<std::string, ActionTranslatorFn> mTranslators;
    std::vector<StampedCommand> mPendingCommands;
    uint64_t mTotalRoutedCommands = 0;
};

} // namespace Engine::Input
