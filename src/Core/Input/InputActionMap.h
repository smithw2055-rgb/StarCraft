#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Core/Platform/Platform.h"

namespace Core::Input
{

enum class InputModifierMask : uint8_t
{
    None    = 0,
    Shift   = 1u << 0,
    Control = 1u << 1,
    Alt     = 1u << 2,
    Any     = 0xFFu
};

inline constexpr InputModifierMask operator|(InputModifierMask A, InputModifierMask B)
{
    return static_cast<InputModifierMask>(static_cast<uint8_t>(A) | static_cast<uint8_t>(B));
}

inline constexpr InputModifierMask operator&(InputModifierMask A, InputModifierMask B)
{
    return static_cast<InputModifierMask>(static_cast<uint8_t>(A) & static_cast<uint8_t>(B));
}

enum class InputTriggerPhase : uint8_t
{
    Pressed = 0,
    Down    = 1,
    Released = 2
};

struct InputBinding
{
    int KeyCode = 0;
    InputModifierMask Modifiers = InputModifierMask::Any;
    InputTriggerPhase Phase = InputTriggerPhase::Pressed;

    constexpr bool IsValid() const { return KeyCode > 0; }
};

struct InputActionEntry
{
    std::string Name;
    bool Enabled = true;
    InputBinding Primary{};
    InputBinding Secondary{};
};

// Pluggable keyboard state provider (defaults to Core::Platform, overridable in headless tests)
struct InputStateProvider
{
    std::function<bool(int)> IsKeyPressedFn = [](int K) { return Core::Platform::IsKeyPressed(K); };
    std::function<bool(int)> IsKeyDownFn    = [](int K) { return Core::Platform::IsKeyDown(K); };
    std::function<bool(int)> IsKeyReleasedFn = [](int K) { return Core::Platform::IsKeyReleased(K); };
    std::function<bool()> IsShiftDownFn     = []() { return Core::Platform::IsShiftDown(); };
    std::function<bool()> IsControlDownFn   = []() { return Core::Platform::IsControlDown(); };
    std::function<bool()> IsAltDownFn       = []() { return false; };
};

// ============================================================================
// InputActionMap: Declarative Action & Chord Mapping System
// Referenced from Godot 4 InputMap & Unity InputSystem (InputActionAsset):
// - Decouples gameplay command shortcuts from hardcoded ASCII/keycode numbers
// - Supports modifier chords (Shift/Ctrl/Alt), primary/secondary bindings,
//   runtime rebinding, and deterministic headless test injection.
// ============================================================================
class InputActionMap
{
public:
    static InputActionMap& Instance()
    {
        static InputActionMap sInstance = CreateDefaultRTSMap();
        return sInstance;
    }

    static InputActionMap CreateDefaultRTSMap()
    {
        InputActionMap Map;
        Map.RegisterAction("Command.Stop",              {'S', InputModifierMask::Any, InputTriggerPhase::Pressed});
        Map.RegisterAction("Command.HoldPosition",      {'H', InputModifierMask::Any, InputTriggerPhase::Pressed});
        Map.RegisterAction("Command.Patrol",            {'P', InputModifierMask::Any, InputTriggerPhase::Pressed});
        Map.RegisterAction("Command.Follow",            {'F', InputModifierMask::Any, InputTriggerPhase::Pressed});
        Map.RegisterAction("Command.AttackMoveModifier",{'A', InputModifierMask::Any, InputTriggerPhase::Down});
        Map.RegisterAction("System.ToggleGui",          {Core::Platform::KeyCode::F9, InputModifierMask::Any, InputTriggerPhase::Pressed});
        Map.RegisterAction("System.EscapeOrCancel",     {Core::Platform::KeyCode::Escape, InputModifierMask::Any, InputTriggerPhase::Pressed});
        Map.RegisterAction("Debug.SpawnAuditFormation", {Core::Platform::KeyCode::F6, InputModifierMask::Any, InputTriggerPhase::Pressed});
        Map.RegisterAction("Match.Restart",             {Core::Platform::KeyCode::R, InputModifierMask::Any, InputTriggerPhase::Pressed});
        Map.RegisterAction("Match.QuitToMenu",          {Core::Platform::KeyCode::Q, InputModifierMask::Any, InputTriggerPhase::Pressed},
                                                        {Core::Platform::KeyCode::Escape, InputModifierMask::Any, InputTriggerPhase::Pressed});
        return Map;
    }

    void RegisterAction(std::string ActionName,
                        InputBinding Primary,
                        InputBinding Secondary = {})
    {
        InputActionEntry Entry;
        Entry.Name = ActionName;
        Entry.Enabled = true;
        Entry.Primary = Primary;
        Entry.Secondary = Secondary;
        mActions[ std::move(ActionName) ] = std::move(Entry);
    }

    bool RebindAction(const std::string& ActionName,
                      InputBinding NewPrimary,
                      InputBinding NewSecondary = {})
    {
        auto It = mActions.find(ActionName);
        if (It == mActions.end())
            return false;
        It->second.Primary = NewPrimary;
        It->second.Secondary = NewSecondary;
        return true;
    }

    bool SetActionEnabled(const std::string& ActionName, bool Enabled)
    {
        auto It = mActions.find(ActionName);
        if (It == mActions.end())
            return false;
        It->second.Enabled = Enabled;
        return true;
    }

    bool IsActionEnabled(const std::string& ActionName) const
    {
        auto It = mActions.find(ActionName);
        return It != mActions.end() && It->second.Enabled;
    }

    const InputActionEntry* FindAction(const std::string& ActionName) const
    {
        auto It = mActions.find(ActionName);
        return It != mActions.end() ? &It->second : nullptr;
    }

    bool IsActionTriggered(const std::string& ActionName) const
    {
        auto It = mActions.find(ActionName);
        if (It == mActions.end() || !It->second.Enabled)
            return false;

        const InputModifierMask ActiveMods = QueryActiveModifiers();
        const auto& Entry = It->second;
        if (EvaluateBinding(Entry.Primary, ActiveMods))
            return true;
        if (EvaluateBinding(Entry.Secondary, ActiveMods))
            return true;
        return false;
    }

    void SetStateProvider(InputStateProvider Provider)
    {
        mProvider = std::move(Provider);
    }

    void ResetToDefaultRTS()
    {
        *this = CreateDefaultRTSMap();
    }

    size_t GetActionCount() const { return mActions.size(); }

private:
    InputModifierMask QueryActiveModifiers() const
    {
        InputModifierMask Mask = InputModifierMask::None;
        if (mProvider.IsShiftDownFn && mProvider.IsShiftDownFn())
            Mask = Mask | InputModifierMask::Shift;
        if (mProvider.IsControlDownFn && mProvider.IsControlDownFn())
            Mask = Mask | InputModifierMask::Control;
        if (mProvider.IsAltDownFn && mProvider.IsAltDownFn())
            Mask = Mask | InputModifierMask::Alt;
        return Mask;
    }

    bool EvaluateBinding(const InputBinding& B, InputModifierMask ActiveMods) const
    {
        if (!B.IsValid())
            return false;

        if (B.Modifiers != InputModifierMask::Any && B.Modifiers != ActiveMods)
            return false;

        switch (B.Phase)
        {
            case InputTriggerPhase::Pressed:
                return mProvider.IsKeyPressedFn && mProvider.IsKeyPressedFn(B.KeyCode);
            case InputTriggerPhase::Down:
                return mProvider.IsKeyDownFn && mProvider.IsKeyDownFn(B.KeyCode);
            case InputTriggerPhase::Released:
                return mProvider.IsKeyReleasedFn && mProvider.IsKeyReleasedFn(B.KeyCode);
        }
        return false;
    }

    std::unordered_map<std::string, InputActionEntry> mActions;
    InputStateProvider mProvider{};
};

} // namespace Core::Input
