#include "Client/CameraController.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace TestPlatformState
{
    bool Focused = true;
    bool Client = true;
    bool GameContent = true;
    std::array<bool, 3> MouseDown{};
    std::array<bool, 512> Keys{};
    float MouseX = 0.0f;
    float MouseY = 0.0f;
    float MouseDeltaX = 0.0f;
    float MouseDeltaY = 0.0f;
    float MouseWheel = 0.0f;
}

namespace Core::Platform
{
    bool IsWindowFocused() { return TestPlatformState::Focused; }
    bool IsMouseInClientArea() { return TestPlatformState::Client; }
    bool IsMouseInGameContentArea() { return TestPlatformState::GameContent; }

    bool IsMouseButtonDown(int Button)
    {
        return Button >= 0 &&
               Button < static_cast<int>(TestPlatformState::MouseDown.size()) &&
               TestPlatformState::MouseDown[static_cast<size_t>(Button)];
    }

    float GetMouseX() { return TestPlatformState::MouseX; }
    float GetMouseY() { return TestPlatformState::MouseY; }

    float GetMouseDeltaX()
    {
        const float Value = TestPlatformState::MouseDeltaX;
        TestPlatformState::MouseDeltaX = 0.0f;
        return Value;
    }

    float GetMouseDeltaY()
    {
        const float Value = TestPlatformState::MouseDeltaY;
        TestPlatformState::MouseDeltaY = 0.0f;
        return Value;
    }

    float GetMouseWheel()
    {
        const float Value = TestPlatformState::MouseWheel;
        TestPlatformState::MouseWheel = 0.0f;
        return Value;
    }

    bool IsKeyDown(int KeyCode)
    {
        return KeyCode >= 0 &&
               KeyCode < static_cast<int>(TestPlatformState::Keys.size()) &&
               TestPlatformState::Keys[static_cast<size_t>(KeyCode)];
    }
}

namespace
{
    constexpr float Dt = 1.0f / 60.0f;
    constexpr float StartX = 1000.0f;
    constexpr float StartY = 1000.0f;

    void Require(bool Condition, const char* Message)
    {
        if (!Condition)
        {
            std::cerr << "Requirement failed: " << Message << "\n";
            std::exit(1);
        }
    }

    void RequireNear(float Actual, float Expected, const char* Message)
    {
        if (std::fabs(Actual - Expected) > 0.001f)
        {
            std::cerr << "Requirement failed: " << Message
                      << " actual=" << Actual << " expected=" << Expected << "\n";
            std::exit(1);
        }
    }

    void ResetInput()
    {
        TestPlatformState::Focused = true;
        TestPlatformState::Client = true;
        TestPlatformState::GameContent = true;
        TestPlatformState::MouseDown.fill(false);
        TestPlatformState::Keys.fill(false);
        TestPlatformState::MouseX = 160.0f;
        TestPlatformState::MouseY = 120.0f;
        TestPlatformState::MouseDeltaX = 0.0f;
        TestPlatformState::MouseDeltaY = 0.0f;
        TestPlatformState::MouseWheel = 0.0f;
    }

    Render::Camera2D MakeCamera()
    {
        Render::Camera2D Camera;
        Camera.SetViewport(320, 240);
        Camera.SetZoom(1.0f);
        Camera.SetMapBounds(200, 200);
        Camera.SetPosition(StartX, StartY);
        return Camera;
    }

    Game::Client::CameraController MakeController(Render::Camera2D& Camera)
    {
        Game::Client::CameraController Controller;
        Controller.Attach(Camera);
        Controller.SetBounds(200, 200);
        Controller.SetTopBlockedPx(24.0f);
        Controller.SetBottomBlockedPx(48.0f);
        return Controller;
    }
}

int main()
{
    {
        ResetInput();
        auto Camera = MakeCamera();
        auto Controller = MakeController(Camera);
        TestPlatformState::MouseX = 1.0f;
        TestPlatformState::MouseY = 100.0f;
        Controller.Update(Dt);
        Require(Camera.GetTargetX() < StartX, "left content edge pans left");
    }

    {
        ResetInput();
        auto Camera = MakeCamera();
        auto Controller = MakeController(Camera);
        TestPlatformState::MouseX = 319.0f;
        TestPlatformState::MouseY = 100.0f;
        Controller.Update(Dt);
        Require(Camera.GetTargetX() > StartX, "right content edge pans right");
    }

    {
        ResetInput();
        auto Camera = MakeCamera();
        auto Controller = MakeController(Camera);
        TestPlatformState::MouseX = 160.0f;
        TestPlatformState::MouseY = 25.0f;
        Controller.Update(Dt);
        Require(Camera.GetTargetY() < StartY, "top content edge pans up");
    }

    {
        ResetInput();
        auto Camera = MakeCamera();
        auto Controller = MakeController(Camera);
        TestPlatformState::MouseX = 160.0f;
        TestPlatformState::MouseY = 188.0f;
        Controller.Update(Dt);
        Require(Camera.GetTargetY() > StartY, "bottom content edge pans down");
    }

    {
        ResetInput();
        auto Camera = MakeCamera();
        auto Controller = MakeController(Camera);
        TestPlatformState::MouseX = 0.0f;
        TestPlatformState::MouseY = 100.0f;
        TestPlatformState::GameContent = false;
        Controller.Update(Dt);
        RequireNear(Camera.GetTargetX(), StartX, "clamped non-content edge does not pan");
        RequireNear(Camera.GetTargetY(), StartY, "clamped non-content edge keeps Y");
    }

    {
        ResetInput();
        auto Camera = MakeCamera();
        auto Controller = MakeController(Camera);
        TestPlatformState::MouseX = 1.0f;
        TestPlatformState::MouseY = 100.0f;
        TestPlatformState::Focused = false;
        Controller.Update(Dt);
        RequireNear(Camera.GetTargetX(), StartX, "unfocused window does not edge pan");
    }

    {
        ResetInput();
        auto Camera = MakeCamera();
        auto Controller = MakeController(Camera);
        TestPlatformState::MouseX = 1.0f;
        TestPlatformState::MouseY = 100.0f;
        TestPlatformState::MouseDown[0] = true;
        Controller.Update(Dt);
        RequireNear(Camera.GetTargetX(), StartX, "mouse button blocks edge pan");
    }

    {
        ResetInput();
        auto Camera = MakeCamera();
        auto Controller = MakeController(Camera);
        TestPlatformState::MouseX = 160.0f;
        TestPlatformState::MouseY = 10.0f;
        Controller.Update(Dt);
        RequireNear(Camera.GetTargetY(), StartY, "top HUD band blocks edge pan");
    }

    {
        ResetInput();
        auto Camera = MakeCamera();
        auto Controller = MakeController(Camera);
        TestPlatformState::MouseX = 160.0f;
        TestPlatformState::MouseY = 230.0f;
        Controller.Update(Dt);
        RequireNear(Camera.GetTargetY(), StartY, "bottom HUD band blocks edge pan");
    }

    std::cout << "CameraEdgeScrollTest passed\n";
    return 0;
}
