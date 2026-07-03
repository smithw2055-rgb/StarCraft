#pragma once

#include <functional>
#include <string>

namespace Game::Client::GameLoop
{

struct GameMenuRuntime
{
    void ToggleOpenAndPauseWhenOpened()
    {
        Open = !Open;
        if (Open)
            Paused = true;
    }

    void OpenAndPause()
    {
        Open = true;
        Paused = true;
    }

    void EnsureOpenWhenPaused()
    {
        if (Paused)
            Open = true;
    }

    void CloseAndResume()
    {
        Paused = false;
        Open = false;
        StatusText.clear();
    }

    void TogglePause()
    {
        Paused = !Paused;
        StatusText = Paused ? "Game paused" : "Game resumed";
    }

    void SetResult(bool Ok, const char* SuccessText, const char* FailureText)
    {
        HasSaveResult = true;
        LastSaveOk = Ok;
        StatusText = Ok ? SuccessText : FailureText;
    }

    void RestorePaused(bool PausedValue)
    {
        Paused = PausedValue;
    }

    bool IsOpen() const
    {
        return Open;
    }

    bool IsPaused() const
    {
        return Paused;
    }

    bool HasResult() const
    {
        return HasSaveResult;
    }

    bool WasLastSaveOk() const
    {
        return LastSaveOk;
    }

    const std::string& GetStatusText() const
    {
        return StatusText;
    }

    template <typename SaveGameT, typename LoadGameT>
    bool HandleMenuButtonAction(int Button,
                                const std::string& QuickSavePath,
                                SaveGameT&& SaveGame,
                                LoadGameT&& LoadGame)
    {
        switch (Button)
        {
        case 0:
            CloseAndResume();
            return true;
        case 1:
            TogglePause();
            return true;
        case 2:
            SetResult(SaveGame(QuickSavePath), "Saved quicksave.scsave", "Save failed");
            return true;
        case 3:
        {
            const bool Loaded = LoadGame(QuickSavePath);
            SetResult(Loaded, "Loaded quicksave.scsave", "Load failed");
            return true;
        }
        case 4:
            CloseAndResume();
            return true;
        default:
            return false;
        }
    }

    bool Open = false;
    bool Paused = false;
    bool LastSaveOk = false;
    bool HasSaveResult = false;
    std::string StatusText;
};

} // namespace Game::Client::GameLoop
