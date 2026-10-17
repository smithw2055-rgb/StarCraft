#pragma once

#include "UI/GlueCampaignData.h"

#include <NsCore/Aero.h>
#include <NsGui/UserControl.h>
#include <array>
#include <functional>
#include <string>

namespace Aero {
class Button;
class Border;
class FrameworkElement;
class Image;
struct MouseEventArgs;
class TextBlock;
}

namespace Game::Client::UI::Glue
{

class GlueAnimatedImage;

class GlueMenuController final : public Aero::UserControl
{
    NS_DECLARE_REFLECTION(GlueMenuController, UserControl)

public:
    GlueMenuController() = default;

    void Initialize();
    void Update(double TimeSeconds);
    void ApplyState();

    using ActionCallback = std::function<void(const std::string& Action)>;
    void SetActionCallback(ActionCallback Cb) { mActionCallback = std::move(Cb); }

    using AudioCallback = std::function<void(const std::string& Path)>;
    void SetAudioCallback(AudioCallback Cb) { mAudioCallback = std::move(Cb); }

    void OnSinglePlayer(Aero::BaseComponent* Sender, const Aero::RoutedEventArgs& Args);
    void OnCampaign(Aero::BaseComponent* Sender, const Aero::RoutedEventArgs& Args);
    void OnCustomGame(Aero::BaseComponent* Sender, const Aero::RoutedEventArgs& Args);
    void OnOptions(Aero::BaseComponent* Sender, const Aero::RoutedEventArgs& Args);
    void OnLoadGame(Aero::BaseComponent* Sender, const Aero::RoutedEventArgs& Args);
    void OnBackToMain(Aero::BaseComponent* Sender, const Aero::RoutedEventArgs& Args);
    void OnExit(Aero::BaseComponent* Sender, const Aero::RoutedEventArgs& Args);
    void OnIntro(Aero::BaseComponent* Sender, const Aero::RoutedEventArgs& Args);
    void OnStartOriginal(Aero::BaseComponent* Sender, const Aero::RoutedEventArgs& Args);
    void OnStartExpansion(Aero::BaseComponent* Sender, const Aero::RoutedEventArgs& Args);
    void OnStartCustomGame(Aero::BaseComponent* Sender, const Aero::RoutedEventArgs& Args);
    void OnCampaignMission(Aero::BaseComponent* Sender, const Aero::RoutedEventArgs& Args);
    void OnCampaignContinue(Aero::BaseComponent* Sender, const Aero::RoutedEventArgs& Args);
    void OnTerranTab(Aero::BaseComponent* Sender, const Aero::RoutedEventArgs& Args);
    void OnZergTab(Aero::BaseComponent* Sender, const Aero::RoutedEventArgs& Args);
    void OnProtossTab(Aero::BaseComponent* Sender, const Aero::RoutedEventArgs& Args);
    void OnLanguageToggle(Aero::BaseComponent* Sender, const Aero::RoutedEventArgs& Args);

    void OnBriefingStart(Aero::BaseComponent* Sender, const Aero::RoutedEventArgs& Args);
    void OnBriefingReplay(Aero::BaseComponent* Sender, const Aero::RoutedEventArgs& Args);
    void OnBriefingBack(Aero::BaseComponent* Sender, const Aero::RoutedEventArgs& Args);

    void OpenBriefingPage(bool Expansion, int Race, int Mission);
    void StartBriefingPlayback();
    void StopBriefingAudio();

    void SetSelectedMap(const std::string& MapName);
    int GetCampaignRace() const { return mCampaignRace; }
    void SetCampaignRace(int RaceIndex); // 0=Terran, 1=Zerg, 2=Protoss
    bool HandleFallbackClick(float ViewX, float ViewY, float ViewWidth, float ViewHeight);

private:
    enum class Page : uint8_t { MainMenu, Cinematics, SinglePlayer, Campaign, CustomGame, Options, LoadGame, Briefing };
    enum class Language : uint8_t { English, Chinese };

    void CacheElements();
    void SwitchToPage(Page NewPage);
    void SetPageVisibility(Page Page, bool Visible);
    Aero::Border* GetPageBorder(Page Page);
    void ToggleLanguage();
    void ApplyLanguage();
    void SetTextPairVisibility(Aero::TextBlock* English, Aero::TextBlock* Chinese);
    void LoadCampaignProgress();
    void RefreshCampaignPage();
    int GetUnlockedMissionCount() const;
    void EmitMenuHover();
    void EmitMenuClick();
    void CacheAnimatedElements();
    void SetMainMenuHover(int Index);
    void StopMainMenuAnimations();
    void OnMainSingleMouseEnter(Aero::BaseComponent* Sender, const Aero::MouseEventArgs& Args);
    void OnMainMultiMouseEnter(Aero::BaseComponent* Sender, const Aero::MouseEventArgs& Args);
    void OnMainExitMouseEnter(Aero::BaseComponent* Sender, const Aero::MouseEventArgs& Args);
    void OnMainMouseLeave(Aero::BaseComponent* Sender, const Aero::MouseEventArgs& Args);
    void SetCampaignAnimationsEnabled(bool Enabled, bool RestartActive);
    void ApplyCampaignAnimationState(bool RestartActive);
    void SetCampaignMaskTarget(int Set, int Race, float Opacity);

    ActionCallback mActionCallback;

    bool mInitialized = false;
    Page mCurrentPage = Page::MainMenu;
    Language mLanguage = Language::English;
    bool mCampaignExpansion = false;
    int mCampaignRace = 0;
    bool mCampaignProgressLoaded = false;
    std::array<std::array<int, 3>, 2> mCampaignUnlocked = {};

    // Page borders
    Aero::Border* mMainMenuPage = nullptr;
    Aero::Border* mCinematicsPage = nullptr;
    Aero::Border* mSinglePlayerPage = nullptr;
    Aero::Border* mCampaignPage = nullptr;
    Aero::Border* mCustomGamePage = nullptr;
    Aero::Border* mOptionsPage = nullptr;
    Aero::Border* mLoadGamePage = nullptr;

    // Main menu buttons
    Aero::Button* mSinglePlayerBtn = nullptr;
    Aero::Button* mCampaignBtn = nullptr;
    Aero::Button* mCustomGameBtn = nullptr;
    Aero::Button* mMultiplayerBtn = nullptr;
    Aero::Button* mEditorBtn = nullptr;
    Aero::Button* mIntroBtn = nullptr;
    Aero::Button* mExitBtn = nullptr;
    Aero::Button* mLanguageBtn = nullptr;
    Aero::Button* mCinematicsBackBtn = nullptr;

    Aero::TextBlock* mMainSingleLabelEn = nullptr;
    Aero::TextBlock* mMainSingleLabelZh = nullptr;
    Aero::TextBlock* mMainMultiplayerLabelEn = nullptr;
    Aero::TextBlock* mMainMultiplayerLabelZh = nullptr;
    Aero::TextBlock* mMainOptionsLabelEn = nullptr;
    Aero::TextBlock* mMainOptionsLabelZh = nullptr;
    Aero::TextBlock* mMainFavoritesLabelEn = nullptr;
    Aero::TextBlock* mMainFavoritesLabelZh = nullptr;
    Aero::TextBlock* mMainHotkeysLabelEn = nullptr;
    Aero::TextBlock* mMainHotkeysLabelZh = nullptr;
    Aero::TextBlock* mMainCreditsLabelEn = nullptr;
    Aero::TextBlock* mMainCreditsLabelZh = nullptr;
    Aero::TextBlock* mMainIntroLabelEn = nullptr;
    Aero::TextBlock* mMainIntroLabelZh = nullptr;
    Aero::TextBlock* mMainExitLabelEn = nullptr;
    Aero::TextBlock* mMainExitLabelZh = nullptr;
    Aero::TextBlock* mLanguageLabelEn = nullptr;
    Aero::TextBlock* mLanguageLabelZh = nullptr;
    GlueAnimatedImage* mMainMenuVideos[3] = {};
    int mMainMenuHover = -1;

    // Single player buttons
    Aero::Button* mOriginalBtn = nullptr;
    Aero::Button* mExpansionBtn = nullptr;
    Aero::Button* mProtossBtn = nullptr;
    Aero::Button* mSinglePlayerBackBtn = nullptr;
    Aero::TextBlock* mSingleSelectTitleEn = nullptr;
    Aero::TextBlock* mSingleSelectTitleZh = nullptr;
    Aero::TextBlock* mOriginalLabelEn = nullptr;
    Aero::TextBlock* mOriginalLabelZh = nullptr;
    Aero::TextBlock* mExpansionLabelEn = nullptr;
    Aero::TextBlock* mExpansionLabelZh = nullptr;
    Aero::TextBlock* mCancelLabelEn = nullptr;
    Aero::TextBlock* mCancelLabelZh = nullptr;

    // Campaign buttons
    Aero::Button* mTerranTabBtn = nullptr;
    Aero::Button* mZergTabBtn = nullptr;
    Aero::Button* mProtossTabBtn = nullptr;
    Aero::Button* mExpansionTerranTabBtn = nullptr;
    Aero::Button* mExpansionZergTabBtn = nullptr;
    Aero::Button* mExpansionProtossTabBtn = nullptr;
    Aero::Button* mCampaignBackBtn = nullptr;
    Aero::Button* mCampaignLoadBtn = nullptr;
    Aero::Button* mCampaignPlayCustomBtn = nullptr;
    Aero::Button* mCampaignMissionBtns[10] = {};
    Aero::FrameworkElement* mCampaignOriginalVisuals = nullptr;
    Aero::FrameworkElement* mCampaignExpansionVisuals = nullptr;
    Aero::FrameworkElement* mCampaignRaceHighlights[2][3] = {};
    GlueAnimatedImage* mCampaignRaceVideos[2][3][2] = {};
    GlueAnimatedImage* mCampaignRaceSelectedVideos[2][3] = {};
    Aero::FrameworkElement* mCampaignRaceMasks[2][3] = {};
    float mCampaignRaceMaskOpacity[2][3] = {};
    float mCampaignRaceMaskTarget[2][3] = {};
    bool mCampaignAnimationsEnabled = false;
    Aero::TextBlock* mCampaignTitleText = nullptr;
    Aero::TextBlock* mCampaignSetText = nullptr;
    Aero::TextBlock* mCampaignProgressText = nullptr;
    Aero::TextBlock* mCampaignBriefingTitleText = nullptr;
    Aero::TextBlock* mCampaignBriefingText = nullptr;
    Aero::TextBlock* mCampaignBriefingStatusText = nullptr;

    // Custom game buttons
    Aero::Button* mCustomGameBackBtn = nullptr;
    Aero::Button* mStartGameBtn = nullptr;
    Aero::Button* mCustomMapBtns[5] = {};

    // Options buttons
    Aero::Button* mOptionsOkBtn = nullptr;
    Aero::Button* mOptionsRestoreBtn = nullptr;

    // Load game buttons
    Aero::Button* mLoadGameOkBtn = nullptr;
    Aero::Button* mLoadGameBackBtn = nullptr;

    // Briefing room elements
    Aero::Border* mBriefingPage = nullptr;
    Aero::Image* mBriefingBackgroundRace = nullptr;
    Aero::TextBlock* mBriefingTitleText = nullptr;
    Aero::TextBlock* mBriefingObjectivesText = nullptr;
    Aero::TextBlock* mBriefingSubtitleSpeaker = nullptr;
    Aero::TextBlock* mBriefingSubtitleText = nullptr;
    Aero::Button* mBriefingStartBtn = nullptr;
    Aero::Button* mBriefingReplayBtn = nullptr;
    Aero::Button* mBriefingBackBtn = nullptr;
    Aero::Border* mBriefingPortraitBorders[4] = {};
    GlueAnimatedImage* mBriefingPortraits[4] = {};
    Aero::TextBlock* mBriefingSpeakers[4] = {};

    AudioCallback mAudioCallback;
    CampaignBriefingInfo mBriefingInfo;
    bool mBriefingExpansion = false;
    int mBriefingRace = 0;
    int mBriefingMission = 0;
    int mBriefingLineIndex = -1;
    float mBriefingLineTimer = 0.0f;
    double mLastUpdateTime = 0.0;
};

} // namespace Game::Client::UI::Glue
