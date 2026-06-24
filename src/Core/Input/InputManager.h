#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cstdint>

#include "Core/Math/Vec2.h"

namespace Core::Input
{

enum class MouseButton : uint8_t
{
    Left = 0,
    Right,
    Middle,
    Side,
    Extra,
    COUNT
};

enum class KeyAction
{
    MoveUp,
    MoveDown,
    MoveLeft,
    MoveRight,
    Select,
    AddToSelection,
    AttackMove,
    Stop,
    HoldPosition,
    Patrol,
    Build,
    Harvest,
    TrainUnit,
    ShowMiniMap,
    ToggleDebug,
    Pause,
    ESC,
    COUNT
};

struct InputFrameState
{
    bool KeysDown[512] = {};
    bool KeysPressed[512] = {};
    bool KeysReleased[512] = {};
    bool MouseButtonsDown[(int)MouseButton::COUNT] = {};
    bool MouseButtonsPressed[(int)MouseButton::COUNT] = {};
    bool MouseButtonsReleased[(int)MouseButton::COUNT] = {};
    Core::Math::Vec2 MousePosition;
    Core::Math::Vec2 MouseDelta;
    float MouseWheelMove = 0.0f;
};

class InputManager
{
public:
    void Poll();

    bool IsKeyDown(int Key) const;
    bool IsKeyPressed(int Key) const;
    bool IsKeyReleased(int Key) const;

    bool IsMouseButtonDown(MouseButton Button) const;
    bool IsMouseButtonPressed(MouseButton Button) const;
    bool IsMouseButtonReleased(MouseButton Button) const;

    Core::Math::Vec2 GetMousePosition() const;
    Core::Math::Vec2 GetMouseDelta() const;
    float GetMouseWheelMove() const;

    bool IsActionActive(KeyAction Action) const;
    bool IsActionPressed(KeyAction Action) const;

    void BindAction(KeyAction Action, int PrimaryKey, int SecondaryKey = 0);

    void SetCursorVisible(bool Visible);
    void SetCursorMode(int Mode);

    const InputFrameState& GetState() const { return mState; }

    void InitDefaultBindings();

    static InputManager& GetInstance()
    {
        static InputManager Instance;
        return Instance;
    }

private:
    InputManager() = default;

    InputFrameState mState;
    InputFrameState mPrevState;

    struct ActionBinding
    {
        int PrimaryKey = 0;
        int SecondaryKey = 0;
    };
    ActionBinding mActionBindings[(int)KeyAction::COUNT] = {};
    bool mBindingsInitialized = false;
};

} // namespace Core::Input
