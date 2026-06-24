#include "Core/Input/InputManager.h"
#include "raylib.h"

namespace Core::Input
{

static int MouseToRaylib(MouseButton B)
{
    switch (B)
    {
        case MouseButton::Left:   return MOUSE_BUTTON_LEFT;
        case MouseButton::Right:  return MOUSE_BUTTON_RIGHT;
        case MouseButton::Middle: return MOUSE_BUTTON_MIDDLE;
        default:                  return MOUSE_BUTTON_LEFT;
    }
}

void InputManager::InitDefaultBindings()
{
    BindAction(KeyAction::MoveUp,          KEY_W,      KEY_UP);
    BindAction(KeyAction::MoveDown,        KEY_S,      KEY_DOWN);
    BindAction(KeyAction::MoveLeft,        KEY_A,      KEY_LEFT);
    BindAction(KeyAction::MoveRight,       KEY_D,      KEY_RIGHT);
    BindAction(KeyAction::Select,          MOUSE_BUTTON_LEFT, 0);
    BindAction(KeyAction::AddToSelection,  KEY_LEFT_SHIFT, 0);
    BindAction(KeyAction::AttackMove,      KEY_A,      0);
    BindAction(KeyAction::Stop,            KEY_S,      0);
    BindAction(KeyAction::HoldPosition,    KEY_H,      0);
    BindAction(KeyAction::Patrol,          KEY_P,      0);
    BindAction(KeyAction::Build,           KEY_B,      0);
    BindAction(KeyAction::Harvest,         KEY_G,      0);
    BindAction(KeyAction::TrainUnit,       KEY_T,      0);
    BindAction(KeyAction::ShowMiniMap,     KEY_TAB,    0);
    BindAction(KeyAction::ToggleDebug,     KEY_F12,    0);
    BindAction(KeyAction::Pause,           KEY_PAUSE,  0);
    BindAction(KeyAction::ESC,             KEY_ESCAPE, 0);
    mBindingsInitialized = true;
}

void InputManager::BindAction(KeyAction Action, int PrimaryKey, int SecondaryKey)
{
    int Idx = (int)Action;
    if (Idx >= 0 && Idx < (int)KeyAction::COUNT)
    {
        mActionBindings[Idx].PrimaryKey = PrimaryKey;
        mActionBindings[Idx].SecondaryKey = SecondaryKey;
    }
}

void InputManager::Poll()
{
    mPrevState = mState;

    for (int I = 0; I < 512; I++)
    {
        mState.KeysDown[I] = ::IsKeyDown(I);
        mState.KeysPressed[I] = mState.KeysDown[I] && !mPrevState.KeysDown[I];
        mState.KeysReleased[I] = !mState.KeysDown[I] && mPrevState.KeysDown[I];
    }

    for (int I = 0; I < (int)MouseButton::COUNT; I++)
    {
        int Rb = MouseToRaylib((MouseButton)I);
        bool Down = ::IsMouseButtonDown(Rb);
        mState.MouseButtonsDown[I] = Down;
        mState.MouseButtonsPressed[I] = Down && !mPrevState.MouseButtonsDown[I];
        mState.MouseButtonsReleased[I] = !Down && mPrevState.MouseButtonsDown[I];
    }

    Vector2 Pos = ::GetMousePosition();
    mState.MousePosition = {Pos.x, Pos.y};

    Vector2 Delta = ::GetMouseDelta();
    mState.MouseDelta = {Delta.x, Delta.y};

    mState.MouseWheelMove = ::GetMouseWheelMove();
}

bool InputManager::IsKeyDown(int Key) const
{
    if (Key < 0 || Key >= 512) return false;
    return mState.KeysDown[Key];
}

bool InputManager::IsKeyPressed(int Key) const
{
    if (Key < 0 || Key >= 512) return false;
    return mState.KeysPressed[Key];
}

bool InputManager::IsKeyReleased(int Key) const
{
    if (Key < 0 || Key >= 512) return false;
    return mState.KeysReleased[Key];
}

bool InputManager::IsMouseButtonDown(MouseButton Button) const
{
    int Idx = (int)Button;
    if (Idx < 0 || Idx >= (int)MouseButton::COUNT) return false;
    return mState.MouseButtonsDown[Idx];
}

bool InputManager::IsMouseButtonPressed(MouseButton Button) const
{
    int Idx = (int)Button;
    if (Idx < 0 || Idx >= (int)MouseButton::COUNT) return false;
    return mState.MouseButtonsPressed[Idx];
}

bool InputManager::IsMouseButtonReleased(MouseButton Button) const
{
    int Idx = (int)Button;
    if (Idx < 0 || Idx >= (int)MouseButton::COUNT) return false;
    return mState.MouseButtonsReleased[Idx];
}

Core::Math::Vec2 InputManager::GetMousePosition() const
{
    return mState.MousePosition;
}

Core::Math::Vec2 InputManager::GetMouseDelta() const
{
    return mState.MouseDelta;
}

float InputManager::GetMouseWheelMove() const
{
    return mState.MouseWheelMove;
}

bool InputManager::IsActionActive(KeyAction Action) const
{
    int Idx = (int)Action;
    if (Idx < 0 || Idx >= (int)KeyAction::COUNT) return false;

    auto& B = mActionBindings[Idx];
    if (B.PrimaryKey > 0 && IsKeyDown(B.PrimaryKey))
        return true;
    if (B.SecondaryKey > 0 && IsKeyDown(B.SecondaryKey))
        return true;
    if (B.PrimaryKey == MOUSE_BUTTON_LEFT && IsMouseButtonDown(MouseButton::Left))
        return true;
    if (B.PrimaryKey == MOUSE_BUTTON_RIGHT && IsMouseButtonDown(MouseButton::Right))
        return true;
    return false;
}

bool InputManager::IsActionPressed(KeyAction Action) const
{
    int Idx = (int)Action;
    if (Idx < 0 || Idx >= (int)KeyAction::COUNT) return false;

    auto& B = mActionBindings[Idx];
    if (B.PrimaryKey > 0 && IsKeyPressed(B.PrimaryKey))
        return true;
    if (B.SecondaryKey > 0 && IsKeyPressed(B.SecondaryKey))
        return true;
    if (B.PrimaryKey == MOUSE_BUTTON_LEFT && IsMouseButtonPressed(MouseButton::Left))
        return true;
    if (B.PrimaryKey == MOUSE_BUTTON_RIGHT && IsMouseButtonPressed(MouseButton::Right))
        return true;
    return false;
}

void InputManager::SetCursorVisible(bool Visible)
{
    if (Visible)
        ShowCursor();
    else
        HideCursor();
}

void InputManager::SetCursorMode(int Mode)
{
    if (Mode == 0)
        EnableCursor();
    else
        DisableCursor();
}

} // namespace Core::Input
