#include "GlueMenuController.h"
#include "GlueAnimatedImage.h"
#include "UI/GlueCampaignData.h"
#include "Game/Campaign/CampaignMissionCatalog.h"
#include "Game/Campaign/CampaignController.h"
#include <NsCore/ReflectionImplement.h>
#include <NsGui/Button.h>
#include <NsGui/Border.h>
#include <NsGui/FrameworkElement.h>
#include <NsGui/TextBlock.h>
#include <NsGui/TimeSpan.h>
#include <NsGui/UIElementEvents.h>
#include <NsGui/Uri.h>
#include <NsGui/IntegrationAPI.h>
#include <cstdio>
#include <iostream>
#include <algorithm>
#include <cmath>

namespace Game::Client::UI::Glue
{

namespace
{

bool InRect(float X, float Y, float Left, float Top, float Width, float Height)
{
    return X >= Left && X <= Left + Width && Y >= Top && Y <= Top + Height;
}

} // namespace

void GlueMenuController::Initialize()
{
    if (mInitialized)
        return;

    CacheElements();
    LoadCampaignProgress();
    SetCampaignRace(0);
    SwitchToPage(Page::MainMenu);
    ApplyState();
    mInitialized = true;
}

void GlueMenuController::CacheElements()
{
    auto* Root = this;

    mMainMenuPage = Aero::DynamicCast<Aero::Border*>(Root->FindName("MainMenuPage"));
    mCinematicsPage = Aero::DynamicCast<Aero::Border*>(Root->FindName("CinematicsPage"));
    mSinglePlayerPage = Aero::DynamicCast<Aero::Border*>(Root->FindName("SinglePlayerPage"));
    mCampaignPage = Aero::DynamicCast<Aero::Border*>(Root->FindName("CampaignPage"));
    mCustomGamePage = Aero::DynamicCast<Aero::Border*>(Root->FindName("CustomGamePage"));
    mOptionsPage = Aero::DynamicCast<Aero::Border*>(Root->FindName("OptionsPage"));
    mLoadGamePage = Aero::DynamicCast<Aero::Border*>(Root->FindName("LoadGamePage"));

    mSinglePlayerBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("SinglePlayerButton"));
    mCampaignBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("CampaignButton"));
    mCustomGameBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("CustomGameButton"));
    mMultiplayerBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("MultiplayerButton"));
    mEditorBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("EditorButton"));
    mIntroBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("IntroButton"));
    mExitBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("ExitButton"));
    mLanguageBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("LanguageButton"));
    mCinematicsBackBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("CinematicsBackButton"));

    mMainSingleLabelEn = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("MainSingleLabelEn"));
    mMainSingleLabelZh = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("MainSingleLabelZh"));
    mMainMultiplayerLabelEn = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("MainMultiplayerLabelEn"));
    mMainMultiplayerLabelZh = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("MainMultiplayerLabelZh"));
    mMainOptionsLabelEn = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("MainOptionsLabelEn"));
    mMainOptionsLabelZh = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("MainOptionsLabelZh"));
    mMainFavoritesLabelEn = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("MainFavoritesLabelEn"));
    mMainFavoritesLabelZh = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("MainFavoritesLabelZh"));
    mMainHotkeysLabelEn = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("MainHotkeysLabelEn"));
    mMainHotkeysLabelZh = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("MainHotkeysLabelZh"));
    mMainCreditsLabelEn = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("MainCreditsLabelEn"));
    mMainCreditsLabelZh = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("MainCreditsLabelZh"));
    mMainIntroLabelEn = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("MainIntroLabelEn"));
    mMainIntroLabelZh = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("MainIntroLabelZh"));
    mMainExitLabelEn = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("MainExitLabelEn"));
    mMainExitLabelZh = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("MainExitLabelZh"));
    mLanguageLabelEn = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("LanguageLabelEn"));
    mLanguageLabelZh = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("LanguageLabelZh"));

    if (mSinglePlayerBtn)
    {
        mSinglePlayerBtn->Click() += Aero::MakeDelegate(this, &GlueMenuController::OnSinglePlayer);
        mSinglePlayerBtn->MouseEnter() += Aero::MakeDelegate(this, &GlueMenuController::OnMainSingleMouseEnter);
        mSinglePlayerBtn->MouseLeave() += Aero::MakeDelegate(this, &GlueMenuController::OnMainMouseLeave);
    }
    if (mMultiplayerBtn)
    {
        mMultiplayerBtn->Click() += Aero::MakeDelegate(this, &GlueMenuController::OnCustomGame);
        mMultiplayerBtn->MouseEnter() += Aero::MakeDelegate(this, &GlueMenuController::OnMainMultiMouseEnter);
        mMultiplayerBtn->MouseLeave() += Aero::MakeDelegate(this, &GlueMenuController::OnMainMouseLeave);
    }
    if (mCampaignBtn) mCampaignBtn->Click() += Aero::MakeDelegate(this, &GlueMenuController::OnCampaign);
    if (mCustomGameBtn) mCustomGameBtn->Click() += Aero::MakeDelegate(this, &GlueMenuController::OnCustomGame);
    if (mIntroBtn) mIntroBtn->Click() += Aero::MakeDelegate(this, &GlueMenuController::OnIntro);
    if (mExitBtn)
    {
        mExitBtn->Click() += Aero::MakeDelegate(this, &GlueMenuController::OnExit);
        mExitBtn->MouseEnter() += Aero::MakeDelegate(this, &GlueMenuController::OnMainExitMouseEnter);
        mExitBtn->MouseLeave() += Aero::MakeDelegate(this, &GlueMenuController::OnMainMouseLeave);
    }
    if (mLanguageBtn) mLanguageBtn->Click() += Aero::MakeDelegate(this, &GlueMenuController::OnLanguageToggle);
    if (mCinematicsBackBtn) mCinematicsBackBtn->Click() += Aero::MakeDelegate(this, &GlueMenuController::OnBackToMain);

    mOriginalBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("OriginalButton"));
    mExpansionBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("ExpansionButton"));
    mProtossBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("ProtossButton"));
    mSinglePlayerBackBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("SinglePlayerBackBtn"));
    mSingleSelectTitleEn = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("SingleSelectTitleEn"));
    mSingleSelectTitleZh = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("SingleSelectTitleZh"));
    mOriginalLabelEn = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("OriginalLabelEn"));
    mOriginalLabelZh = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("OriginalLabelZh"));
    mExpansionLabelEn = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("ExpansionLabelEn"));
    mExpansionLabelZh = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("ExpansionLabelZh"));
    mCancelLabelEn = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("CancelLabelEn"));
    mCancelLabelZh = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("CancelLabelZh"));

    if (mOriginalBtn) mOriginalBtn->Click() += Aero::MakeDelegate(this, &GlueMenuController::OnStartOriginal);
    if (mExpansionBtn) mExpansionBtn->Click() += Aero::MakeDelegate(this, &GlueMenuController::OnStartExpansion);
    if (mSinglePlayerBackBtn) mSinglePlayerBackBtn->Click() += Aero::MakeDelegate(this, &GlueMenuController::OnBackToMain);

    mTerranTabBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("TerranTabButton"));
    mZergTabBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("ZergTabButton"));
    mProtossTabBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("ProtossTabButton"));
    mExpansionTerranTabBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("ExpansionTerranTabButton"));
    mExpansionZergTabBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("ExpansionZergTabButton"));
    mExpansionProtossTabBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("ExpansionProtossTabButton"));
    mCampaignBackBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("CampaignBackBtn"));
    mCampaignLoadBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("CampaignLoadButton"));
    mCampaignPlayCustomBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("CampaignPlayCustomButton"));

    if (mTerranTabBtn) mTerranTabBtn->Click() += Aero::MakeDelegate(this, &GlueMenuController::OnTerranTab);
    if (mZergTabBtn) mZergTabBtn->Click() += Aero::MakeDelegate(this, &GlueMenuController::OnZergTab);
    if (mProtossTabBtn) mProtossTabBtn->Click() += Aero::MakeDelegate(this, &GlueMenuController::OnProtossTab);
    if (mExpansionTerranTabBtn) mExpansionTerranTabBtn->Click() += Aero::MakeDelegate(this, &GlueMenuController::OnTerranTab);
    if (mExpansionZergTabBtn) mExpansionZergTabBtn->Click() += Aero::MakeDelegate(this, &GlueMenuController::OnZergTab);
    if (mExpansionProtossTabBtn) mExpansionProtossTabBtn->Click() += Aero::MakeDelegate(this, &GlueMenuController::OnProtossTab);
    if (mCampaignBackBtn) mCampaignBackBtn->Click() += Aero::MakeDelegate(this, &GlueMenuController::OnBackToMain);
    if (mCampaignLoadBtn) mCampaignLoadBtn->Click() += Aero::MakeDelegate(this, &GlueMenuController::OnCampaignContinue);
    if (mCampaignPlayCustomBtn) mCampaignPlayCustomBtn->Click() += Aero::MakeDelegate(this, &GlueMenuController::OnLoadGame);

    for (int i = 0; i < 10; ++i)
    {
        char Name[32];
        snprintf(Name, sizeof(Name), "CampaignMission%d", i);
        mCampaignMissionBtns[i] = Aero::DynamicCast<Aero::Button*>(Root->FindName(Name));
        if (mCampaignMissionBtns[i])
            mCampaignMissionBtns[i]->Click() += Aero::MakeDelegate(this, &GlueMenuController::OnCampaignMission);
    }

    mCampaignTitleText = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("CampaignTitleText"));
    mCampaignSetText = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("CampaignSetText"));
    mCampaignProgressText = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("CampaignProgressText"));
    mCampaignBriefingTitleText = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("CampaignBriefingTitleText"));
    mCampaignBriefingText = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("CampaignBriefingText"));
    mCampaignBriefingStatusText = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("CampaignBriefingStatusText"));
    mCampaignOriginalVisuals = Aero::DynamicCast<Aero::FrameworkElement*>(Root->FindName("CampaignOriginalVisuals"));
    mCampaignExpansionVisuals = Aero::DynamicCast<Aero::FrameworkElement*>(Root->FindName("CampaignExpansionVisuals"));
    mCampaignRaceHighlights[0][0] = Aero::DynamicCast<Aero::FrameworkElement*>(Root->FindName("CampaignOriginalTerranSelected"));
    mCampaignRaceHighlights[0][1] = Aero::DynamicCast<Aero::FrameworkElement*>(Root->FindName("CampaignOriginalZergSelected"));
    mCampaignRaceHighlights[0][2] = Aero::DynamicCast<Aero::FrameworkElement*>(Root->FindName("CampaignOriginalProtossSelected"));
    mCampaignRaceHighlights[1][0] = Aero::DynamicCast<Aero::FrameworkElement*>(Root->FindName("CampaignExpansionTerranSelected"));
    mCampaignRaceHighlights[1][1] = Aero::DynamicCast<Aero::FrameworkElement*>(Root->FindName("CampaignExpansionZergSelected"));
    mCampaignRaceHighlights[1][2] = Aero::DynamicCast<Aero::FrameworkElement*>(Root->FindName("CampaignExpansionProtossSelected"));

    mCustomGameBackBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("CustomGameBackBtn"));
    mStartGameBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("StartGameButton"));

    if (mCustomGameBackBtn) mCustomGameBackBtn->Click() += Aero::MakeDelegate(this, &GlueMenuController::OnBackToMain);
    if (mStartGameBtn) mStartGameBtn->Click() += Aero::MakeDelegate(this, &GlueMenuController::OnStartCustomGame);

    mOptionsOkBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("OptionsOkButton"));
    mOptionsRestoreBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("OptionsRestoreButton"));
    if (mOptionsOkBtn) mOptionsOkBtn->Click() += Aero::MakeDelegate(this, &GlueMenuController::OnBackToMain);

    mLoadGameOkBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("LoadGameOkButton"));
    mLoadGameBackBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("LoadGameBackButton"));
    if (mLoadGameOkBtn) mLoadGameOkBtn->Click() += Aero::MakeDelegate(this, &GlueMenuController::OnCampaignContinue);
    if (mLoadGameBackBtn) mLoadGameBackBtn->Click() += Aero::MakeDelegate(this, &GlueMenuController::OnCampaign);

    for (int i = 0; i < 5; ++i)
    {
        char Name[32];
        snprintf(Name, sizeof(Name), "CustomMap%d", i);
        mCustomMapBtns[i] = Aero::DynamicCast<Aero::Button*>(Root->FindName(Name));
        if (mCustomMapBtns[i])
        {
            mCustomMapBtns[i]->Click() += Aero::Delegate<void(Aero::BaseComponent*, const Aero::RoutedEventArgs&)>(
                [this, i](Aero::BaseComponent*, const Aero::RoutedEventArgs&) {
                    if (mActionCallback) mActionCallback("select_map_" + std::to_string(i));
                });
        }
    }

    mBriefingPage = Aero::DynamicCast<Aero::Border*>(Root->FindName("BriefingPage"));
    mBriefingBackgroundRace = Aero::DynamicCast<Aero::Image*>(Root->FindName("BriefingBackgroundRace"));
    mBriefingTitleText = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("BriefingTitleText"));
    mBriefingObjectivesText = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("BriefingObjectivesText"));
    mBriefingSubtitleSpeaker = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("BriefingSubtitleSpeaker"));
    mBriefingSubtitleText = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName("BriefingSubtitleText"));
    mBriefingStartBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("BriefingStartButton"));
    mBriefingReplayBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("BriefingReplayButton"));
    mBriefingBackBtn = Aero::DynamicCast<Aero::Button*>(Root->FindName("BriefingBackButton"));

    if (mBriefingStartBtn) mBriefingStartBtn->Click() += Aero::MakeDelegate(this, &GlueMenuController::OnBriefingStart);
    if (mBriefingReplayBtn) mBriefingReplayBtn->Click() += Aero::MakeDelegate(this, &GlueMenuController::OnBriefingReplay);
    if (mBriefingBackBtn) mBriefingBackBtn->Click() += Aero::MakeDelegate(this, &GlueMenuController::OnBriefingBack);

    for (int i = 0; i < 4; ++i)
    {
        char Name[32];
        snprintf(Name, sizeof(Name), "BriefingPortraitBorder%d", i);
        mBriefingPortraitBorders[i] = Aero::DynamicCast<Aero::Border*>(Root->FindName(Name));
        snprintf(Name, sizeof(Name), "BriefingPortrait%d", i);
        mBriefingPortraits[i] = Aero::DynamicCast<GlueAnimatedImage*>(Root->FindName(Name));
        snprintf(Name, sizeof(Name), "BriefingSpeaker%d", i);
        mBriefingSpeakers[i] = Aero::DynamicCast<Aero::TextBlock*>(Root->FindName(Name));
    }

    CacheAnimatedElements();
}

void GlueMenuController::CacheAnimatedElements()
{
    auto* Root = this;
    mMainMenuVideos[0] = Aero::DynamicCast<GlueAnimatedImage*>(Root->FindName("MainSingleHoverVideo"));
    mMainMenuVideos[1] = Aero::DynamicCast<GlueAnimatedImage*>(Root->FindName("MainMultiHoverVideo"));
    mMainMenuVideos[2] = Aero::DynamicCast<GlueAnimatedImage*>(Root->FindName("MainExitHoverVideo"));

    const char* VideoNames[2][3][2] = {
        {
            {"CampaignOriginalTerranVideo", "CampaignOriginalTerranDiskVideo"},
            {"CampaignOriginalZergVideo", "CampaignOriginalZergDiskVideo"},
            {"CampaignOriginalProtossVideo", "CampaignOriginalProtossDiskVideo"}
        },
        {
            {"CampaignExpansionTerranVideo", "CampaignExpansionTerranDiskVideo"},
            {"CampaignExpansionZergVideo", "CampaignExpansionZergDiskVideo"},
            {"CampaignExpansionProtossVideo", "CampaignExpansionProtossDiskVideo"}
        }
    };
    const char* SelectedNames[2][3] = {
        {"CampaignOriginalTerranSelected", "CampaignOriginalZergSelected", "CampaignOriginalProtossSelected"},
        {"CampaignExpansionTerranSelected", "CampaignExpansionZergSelected", "CampaignExpansionProtossSelected"}
    };
    const char* MaskNames[2][3] = {
        {"CampaignOriginalTerranMask", "CampaignOriginalZergMask", "CampaignOriginalProtossMask"},
        {"CampaignExpansionTerranMask", "CampaignExpansionZergMask", "CampaignExpansionProtossMask"}
    };

    for (int Set = 0; Set < 2; ++Set)
    {
        for (int Race = 0; Race < 3; ++Race)
        {
            for (int Part = 0; Part < 2; ++Part)
                mCampaignRaceVideos[Set][Race][Part] = Aero::DynamicCast<GlueAnimatedImage*>(Root->FindName(VideoNames[Set][Race][Part]));
            mCampaignRaceSelectedVideos[Set][Race] = Aero::DynamicCast<GlueAnimatedImage*>(Root->FindName(SelectedNames[Set][Race]));
            mCampaignRaceMasks[Set][Race] = Aero::DynamicCast<Aero::FrameworkElement*>(Root->FindName(MaskNames[Set][Race]));
        }
    }

    StopMainMenuAnimations();
    SetCampaignAnimationsEnabled(false, false);
}

void GlueMenuController::Update(double TimeSeconds)
{
    double Dt = (mLastUpdateTime > 0.0) ? (TimeSeconds - mLastUpdateTime) : 0.033;
    if (!std::isfinite(Dt) || Dt <= 0.0 || Dt > 0.25)
        Dt = 0.033;
    mLastUpdateTime = TimeSeconds;

    for (int Set = 0; Set < 2; ++Set)
    {
        for (int Race = 0; Race < 3; ++Race)
        {
            auto* Mask = mCampaignRaceMasks[Set][Race];
            if (!Mask)
                continue;
            float& Current = mCampaignRaceMaskOpacity[Set][Race];
            const float Target = mCampaignRaceMaskTarget[Set][Race];
            if (std::abs(Current - Target) <= 0.005f)
                Current = Target;
            else
                Current += (Target - Current) * 0.18f;
            Mask->SetOpacity(Current);
        }
    }

    if (mCurrentPage == Page::Briefing && !mBriefingInfo.Lines.empty())
    {
        if (mBriefingLineIndex < (int)mBriefingInfo.Lines.size())
        {
            mBriefingLineTimer -= static_cast<float>(Dt);
            if (mBriefingLineTimer <= 0.0f)
            {
                ++mBriefingLineIndex;
                if (mBriefingLineIndex < (int)mBriefingInfo.Lines.size())
                {
                    const auto& Line = mBriefingInfo.Lines[(size_t)mBriefingLineIndex];
                    const int Slot = std::max(0, std::min(Line.Slot, 3));

                    for (int i = 0; i < 4; ++i)
                    {
                        if (mBriefingPortraitBorders[i])
                            mBriefingPortraitBorders[i]->SetOpacity(i == Slot ? 1.0f : 0.45f);
                    }

                    if (mBriefingPortraits[Slot] && !Line.PortraitKey.empty())
                    {
                        std::string Webm = GetBriefingWebmPath(Line.PortraitKey, true);
                        if (!Webm.empty())
                        {
                            mBriefingPortraits[Slot]->SetWebmSource(Webm, 15.0);
                            mBriefingPortraits[Slot]->SetPlaybackActive(true, true);
                        }
                    }
                    if (mBriefingSpeakers[Slot])
                        mBriefingSpeakers[Slot]->SetText(Line.Speaker.c_str());

                    if (mBriefingSubtitleSpeaker)
                    {
                        std::string Spk = Line.Speaker.empty() ? "Transmission:" : (Line.Speaker + ":");
                        mBriefingSubtitleSpeaker->SetText(Spk.c_str());
                    }
                    if (mBriefingSubtitleText)
                        mBriefingSubtitleText->SetText(Line.Text.c_str());

                    if (!Line.ResolvedAudioFile.empty() && mAudioCallback)
                        mAudioCallback(Line.ResolvedAudioFile);

                    float Sec = Line.DurationMs > 0 ? ((float)Line.DurationMs / 1000.0f) : 4.0f;
                    mBriefingLineTimer = std::max(2.5f, Sec + 0.5f);
                }
                else
                {
                    mBriefingLineTimer = 999999.0f;
                    if (mBriefingSubtitleSpeaker)
                        mBriefingSubtitleSpeaker->SetText(mLanguage == Language::Chinese ? "简报完成" : "Briefing Completed");
                    if (mBriefingSubtitleText)
                        mBriefingSubtitleText->SetText(mLanguage == Language::Chinese ? "所有战术部署已下达。点击“开始任务”进入战场。" : "All directives issued. Click 'Start Mission' to deploy.");
                    for (int i = 0; i < 4; ++i)
                    {
                        if (mBriefingPortraitBorders[i])
                            mBriefingPortraitBorders[i]->SetOpacity(0.6f);
                    }
                }
            }
        }
    }
}

void GlueMenuController::ApplyState()
{
    ApplyLanguage();
}

void GlueMenuController::EmitMenuClick()
{
    Aero::GUI::PlayAudio(Aero::Uri("sound/glue/mousedown2.wav"), 0.6f);
}

void GlueMenuController::EmitMenuHover()
{
    Aero::GUI::PlayAudio(Aero::Uri("sound/glue/mouseover.wav"), 0.35f);
}

void GlueMenuController::SetMainMenuHover(int Index)
{
    if (mMainMenuHover == Index)
        return;

    mMainMenuHover = Index;
    for (int i = 0; i < 3; ++i)
    {
        auto* Video = mMainMenuVideos[i];
        if (!Video)
            continue;
        const bool Active = i == Index;
        Video->SetOpacity(Active ? 0.96f : 0.0f);
        Video->SetPlaybackActive(Active, Active);
    }
}

void GlueMenuController::StopMainMenuAnimations()
{
    mMainMenuHover = -1;
    for (auto* Video : mMainMenuVideos)
    {
        if (!Video)
            continue;
        Video->SetOpacity(0.0f);
        Video->SetPlaybackActive(false);
    }
}

void GlueMenuController::OnMainSingleMouseEnter(Aero::BaseComponent*, const Aero::MouseEventArgs&)
{
    EmitMenuHover();
    SetMainMenuHover(0);
}

void GlueMenuController::OnMainMultiMouseEnter(Aero::BaseComponent*, const Aero::MouseEventArgs&)
{
    EmitMenuHover();
    SetMainMenuHover(1);
}

void GlueMenuController::OnMainExitMouseEnter(Aero::BaseComponent*, const Aero::MouseEventArgs&)
{
    EmitMenuHover();
    SetMainMenuHover(2);
}

void GlueMenuController::OnMainMouseLeave(Aero::BaseComponent*, const Aero::MouseEventArgs&)
{
    StopMainMenuAnimations();
}

void GlueMenuController::SetCampaignAnimationsEnabled(bool Enabled, bool RestartActive)
{
    mCampaignAnimationsEnabled = Enabled;
    ApplyCampaignAnimationState(RestartActive);
}

void GlueMenuController::SetCampaignMaskTarget(int Set, int Race, float Opacity)
{
    if (Set < 0 || Set >= 2 || Race < 0 || Race >= 3)
        return;
    const float Clamped = std::max(0.0f, std::min(Opacity, 1.0f));
    mCampaignRaceMaskTarget[Set][Race] = Clamped;
    mCampaignRaceMaskOpacity[Set][Race] = Clamped;
    if (auto* Mask = mCampaignRaceMasks[Set][Race])
        Mask->SetOpacity(Clamped);
}

void GlueMenuController::ApplyCampaignAnimationState(bool RestartActive)
{
    const int ActiveSet = mCampaignExpansion ? 1 : 0;
    for (int Set = 0; Set < 2; ++Set)
    {
        for (int Race = 0; Race < 3; ++Race)
        {
            const bool Active = mCampaignAnimationsEnabled && Set == ActiveSet && Race == mCampaignRace;
            for (auto* Video : mCampaignRaceVideos[Set][Race])
            {
                if (Video)
                    Video->SetPlaybackActive(Active, Active && RestartActive);
            }
            if (auto* Selected = mCampaignRaceSelectedVideos[Set][Race])
                Selected->SetPlaybackActive(Active, Active && RestartActive);

            SetCampaignMaskTarget(Set, Race, Active || !mCampaignAnimationsEnabled ? 0.0f : 0.58f);
        }
    }
}

void GlueMenuController::OnSinglePlayer(Aero::BaseComponent*, const Aero::RoutedEventArgs&)
{
    SwitchToPage(Page::SinglePlayer);
}

void GlueMenuController::OnCampaign(Aero::BaseComponent*, const Aero::RoutedEventArgs&)
{
    SwitchToPage(Page::Campaign);
}

void GlueMenuController::OnCustomGame(Aero::BaseComponent*, const Aero::RoutedEventArgs&)
{
    SwitchToPage(Page::CustomGame);
}

void GlueMenuController::OnOptions(Aero::BaseComponent*, const Aero::RoutedEventArgs&)
{
    SwitchToPage(Page::Options);
}

void GlueMenuController::OnLoadGame(Aero::BaseComponent*, const Aero::RoutedEventArgs&)
{
    SwitchToPage(Page::LoadGame);
}

void GlueMenuController::OnBackToMain(Aero::BaseComponent*, const Aero::RoutedEventArgs&)
{
    SwitchToPage(Page::MainMenu);
}

void GlueMenuController::OnExit(Aero::BaseComponent*, const Aero::RoutedEventArgs&)
{
    if (mActionCallback) mActionCallback("exit");
}

void GlueMenuController::OnIntro(Aero::BaseComponent*, const Aero::RoutedEventArgs&)
{
    SwitchToPage(Page::Cinematics);
}

void GlueMenuController::OnLanguageToggle(Aero::BaseComponent*, const Aero::RoutedEventArgs&)
{
    ToggleLanguage();
}

void GlueMenuController::OnStartOriginal(Aero::BaseComponent*, const Aero::RoutedEventArgs&)
{
    mCampaignExpansion = false;
    SetCampaignRace(mCampaignRace);
    SwitchToPage(Page::Campaign);
}

void GlueMenuController::OnStartExpansion(Aero::BaseComponent*, const Aero::RoutedEventArgs&)
{
    mCampaignExpansion = true;
    SetCampaignRace(mCampaignRace);
    SwitchToPage(Page::Campaign);
}

void GlueMenuController::OnCampaignContinue(Aero::BaseComponent*, const Aero::RoutedEventArgs&)
{
    const int Mission = std::max(0, GetUnlockedMissionCount() - 1);
    OpenBriefingPage(mCampaignExpansion, mCampaignRace, Mission);
}

void GlueMenuController::OnStartCustomGame(Aero::BaseComponent*, const Aero::RoutedEventArgs&)
{
    if (mActionCallback) mActionCallback("start_custom_game");
}

void GlueMenuController::OnTerranTab(Aero::BaseComponent*, const Aero::RoutedEventArgs&)
{
    SetCampaignRace(0);
}

void GlueMenuController::OnZergTab(Aero::BaseComponent*, const Aero::RoutedEventArgs&)
{
    SetCampaignRace(1);
}

void GlueMenuController::OnProtossTab(Aero::BaseComponent*, const Aero::RoutedEventArgs&)
{
    SetCampaignRace(2);
}

void GlueMenuController::OnCampaignMission(Aero::BaseComponent* Sender, const Aero::RoutedEventArgs&)
{
    for (int i = 0; i < 10; ++i)
    {
        if (Sender == mCampaignMissionBtns[i])
        {
            if (i < GetUnlockedMissionCount())
                OpenBriefingPage(mCampaignExpansion, mCampaignRace, i);
            return;
        }
    }
}

void GlueMenuController::OpenBriefingPage(bool Expansion, int Race, int Mission)
{
    mBriefingExpansion = Expansion;
    mBriefingRace = Race;
    mBriefingMission = Mission;
    mBriefingInfo = LoadCampaignBriefingInfo(Expansion, Race, Mission, mLanguage == Language::Chinese);

    if (mBriefingTitleText)
    {
        static constexpr const char* DefaultMissions[3][10] = {
            {"Boot Camp", "Wasteland", "Backwater Station", "Desperate Alliance",
             "The Jacobs Installation", "Revolution", "Norad II",
             "The Big Push", "New Gettysburg", "The Hammer Falls"},
            {"Among the Ruins", "Egression", "The New Dominion",
             "Agent of the Swarm", "The Amerigo", "The Dark Templar",
             "The Culling", "Eye for an Eye", "The Invasion of Aiur", "Full Circle"},
            {"First Strike", "Into the Darkness", "Higher Ground",
             "The Hunt for Tassadar", "Ambush!",
             "Into the Flames", "To Chain the Beast", "The Trial of Tassadar",
             "Shadow Hunters", "The Fall of Fenix"}
        };
        std::string Title = mBriefingInfo.Title.empty() ? DefaultMissions[Race][Mission] : mBriefingInfo.Title;
        std::string Header = std::string(CampaignSetName(Expansion)) + " - " + CampaignRaceName(Race) + " " +
            (mLanguage == Language::Chinese ? "任务 " : "Mission ") + std::to_string(Mission + 1) + ": " + Title;
        mBriefingTitleText->SetText(Header.c_str());
    }

    if (mBriefingObjectivesText)
    {
        std::string Obj = mBriefingInfo.Objectives.empty() ? mBriefingInfo.Text : mBriefingInfo.Objectives;
        mBriefingObjectivesText->SetText(Obj.c_str());
    }

    if (mBriefingStartBtn)
        mBriefingStartBtn->SetContent(mLanguage == Language::Chinese ? "开始任务" : "Start Mission");
    if (mBriefingReplayBtn)
        mBriefingReplayBtn->SetContent(mLanguage == Language::Chinese ? "重新播放" : "Replay Briefing");
    if (mBriefingBackBtn)
        mBriefingBackBtn->SetContent(mLanguage == Language::Chinese ? "返回" : "Cancel");

    SwitchToPage(Page::Briefing);
    StartBriefingPlayback();
}

void GlueMenuController::StartBriefingPlayback()
{
    StopBriefingAudio();
    mBriefingLineIndex = -1;
    mBriefingLineTimer = 0.4f;

    for (int i = 0; i < 4; ++i)
    {
        if (mBriefingPortraits[i])
        {
            mBriefingPortraits[i]->ClearVideoSource();
            mBriefingPortraits[i]->SetPlaybackActive(false);
        }
        if (mBriefingSpeakers[i])
            mBriefingSpeakers[i]->SetText("");
        if (mBriefingPortraitBorders[i])
            mBriefingPortraitBorders[i]->SetOpacity(0.45f);
    }

    if (mBriefingSubtitleSpeaker)
        mBriefingSubtitleSpeaker->SetText(mLanguage == Language::Chinese ? "建立通信..." : "Establishing Communications...");
    if (mBriefingSubtitleText)
        mBriefingSubtitleText->SetText(mLanguage == Language::Chinese ? "正在接入战役战术简报频率..." : "Connecting to mission briefing link...");
}

void GlueMenuController::StopBriefingAudio()
{
}

void GlueMenuController::OnBriefingStart(Aero::BaseComponent*, const Aero::RoutedEventArgs&)
{
    EmitMenuClick();
    StopBriefingAudio();
    if (mActionCallback)
        mActionCallback(Game::Campaign::BuildCampaignMissionAction(mBriefingExpansion, mBriefingRace, mBriefingMission));
}

void GlueMenuController::OnBriefingReplay(Aero::BaseComponent*, const Aero::RoutedEventArgs&)
{
    EmitMenuClick();
    StartBriefingPlayback();
}

void GlueMenuController::OnBriefingBack(Aero::BaseComponent*, const Aero::RoutedEventArgs&)
{
    EmitMenuClick();
    StopBriefingAudio();
    SwitchToPage(Page::Campaign);
}

void GlueMenuController::SetSelectedMap(const std::string& MapName)
{
    (void)MapName;
}

void GlueMenuController::SetCampaignRace(int RaceIndex)
{
    mCampaignRace = std::max(0, std::min(RaceIndex, 2));
    const char* MissionNames[3][10] = {
        {"Boot Camp", "Wasteland", "Backwater Station", "Desperate Alliance",
         "The Jacobs Installation", "Revolution", "Norad II",
         "The Big Push", "New Gettysburg", "The Hammer Falls"},
        {"Among the Ruins", "Egression", "The New Dominion",
         "Agent of the Swarm", "The Amerigo", "The Dark Templar",
         "The Culling", "Eye for an Eye", "The Invasion of Aiur", "Full Circle"},
        {"First Strike", "Into the Darkness", "Higher Ground",
         "The Hunt for Tassadar", "Ambush!",
         "Into the Flames", "To Chain the Beast", "The Trial of Tassadar",
         "Shadow Hunters", "The Fall of Fenix"}
    };

    for (int i = 0; i < 10; ++i)
    {
        if (mCampaignMissionBtns[i])
        {
            std::string Label = "Mission " + std::to_string(i + 1) + ": " + MissionNames[mCampaignRace][i];
            mCampaignMissionBtns[i]->SetContent(Label.c_str());
        }
    }
    RefreshCampaignPage();
}

void GlueMenuController::LoadCampaignProgress()
{
    if (mCampaignProgressLoaded)
        return;

    mCampaignUnlocked = Game::Campaign::LoadCampaignProgressCounts();
    mCampaignProgressLoaded = true;
}

int GlueMenuController::GetUnlockedMissionCount() const
{
    const int Set = mCampaignExpansion ? 1 : 0;
    return mCampaignUnlocked[(size_t)Set][(size_t)mCampaignRace];
}

void GlueMenuController::RefreshCampaignPage()
{
    static constexpr const char* MissionNames[3][10] = {
        {"Boot Camp", "Wasteland", "Backwater Station", "Desperate Alliance",
         "The Jacobs Installation", "Revolution", "Norad II",
         "The Big Push", "New Gettysburg", "The Hammer Falls"},
        {"Among the Ruins", "Egression", "The New Dominion",
         "Agent of the Swarm", "The Amerigo", "The Dark Templar",
         "The Culling", "Eye for an Eye", "The Invasion of Aiur", "Full Circle"},
        {"First Strike", "Into the Darkness", "Higher Ground",
         "The Hunt for Tassadar", "Ambush!",
         "Into the Flames", "To Chain the Beast", "The Trial of Tassadar",
         "Shadow Hunters", "The Fall of Fenix"}
    };
    static constexpr const char* Briefing[3][3] = {
        {
            "Confederate outposts, marshal orders, and Raynor's first field command.",
            "The swarm expands through ruined colonies and living production chains.",
            "Aiur's defenders track Tassadar, Zeratul, and the fate of the Conclave."
        },
        {
            "The UED campaign opens with occupation, fleet recovery, and betrayal.",
            "Kerrigan's Brood War arc advances through Char, Korhal, and the final reckoning.",
            "The dark templar campaign follows Shakuras, the crystals, and the Uraj-Khalis war."
        },
        {
            "Scenario data is loaded from the original campaign CHK files.",
            "Scenario data is loaded from the expansion campaign CHK files.",
            "Scenario data is loaded from the selected campaign CHK files."
        }
    };

    const int Unlocked = GetUnlockedMissionCount();
    const int ReadyMission = std::max(0, std::min(Unlocked - 1, 9));
    const CampaignBriefingInfo BriefingInfo =
        LoadCampaignBriefingInfo(mCampaignExpansion, mCampaignRace, ReadyMission);
    if (mCampaignOriginalVisuals)
        mCampaignOriginalVisuals->SetVisibility(!mCampaignExpansion ? Aero::Visibility_Visible : Aero::Visibility_Collapsed);
    if (mCampaignExpansionVisuals)
        mCampaignExpansionVisuals->SetVisibility(mCampaignExpansion ? Aero::Visibility_Visible : Aero::Visibility_Collapsed);
    for (int Set = 0; Set < 2; ++Set)
    {
        for (int Race = 0; Race < 3; ++Race)
        {
            if (mCampaignRaceHighlights[Set][Race])
            {
                const bool Visible = Set == (mCampaignExpansion ? 1 : 0) && Race == mCampaignRace;
                mCampaignRaceHighlights[Set][Race]->SetVisibility(Visible ? Aero::Visibility_Visible : Aero::Visibility_Collapsed);
            }
        }
    }
    if (mCampaignTitleText)
    {
        std::string Title = "Select " + std::string(CampaignRaceName(mCampaignRace));
        mCampaignTitleText->SetText(Title.c_str());
    }
    if (mCampaignSetText)
    {
        std::string SetText = std::string(CampaignSetName(mCampaignExpansion)) + " Episode";
        mCampaignSetText->SetText(SetText.c_str());
    }
    if (mCampaignProgressText)
    {
        std::string Progress = std::to_string(Unlocked) + " / 10 missions available";
        mCampaignProgressText->SetText(Progress.c_str());
    }
    if (mCampaignBriefingTitleText)
    {
        const std::string Title = BriefingInfo.Title.empty()
            ? MissionNames[mCampaignRace][ReadyMission]
            : BriefingInfo.Title;
        mCampaignBriefingTitleText->SetText(Title.c_str());
    }
    if (mCampaignBriefingText)
    {
        const std::string Text = BriefingInfo.Text.empty()
            ? Briefing[mCampaignExpansion ? 1 : 0][mCampaignRace]
            : BriefingInfo.Text;
        mCampaignBriefingText->SetText(Text.c_str());
    }
    if (mCampaignBriefingStatusText)
    {
        std::string Status = Unlocked >= 10 ? "Campaign complete" : "Next mission ready";
        if (!BriefingInfo.Status.empty())
            Status += "\n" + BriefingInfo.Status;
        mCampaignBriefingStatusText->SetText(Status.c_str());
    }

    for (int i = 0; i < 10; ++i)
    {
        auto* Button = mCampaignMissionBtns[i];
        if (!Button)
            continue;

        const bool Visible = i < Unlocked;
        Button->SetVisibility(Visible ? Aero::Visibility_Visible : Aero::Visibility_Collapsed);
        Button->SetIsEnabled(i < Unlocked);
        std::string Label = std::to_string(i + 1) + ". " + MissionNames[mCampaignRace][i];
        if (i < Unlocked - 1)
            Label += "  [complete]";
        else if (i == Unlocked - 1)
            Label += "  [ready]";
        Button->SetContent(Label.c_str());
    }
}

void GlueMenuController::ToggleLanguage()
{
    mLanguage = (mLanguage == Language::English) ? Language::Chinese : Language::English;
    ApplyLanguage();
}

void GlueMenuController::SetTextPairVisibility(Aero::TextBlock* English, Aero::TextBlock* Chinese)
{
    const bool UseEnglish = mLanguage == Language::English;
    if (English)
        English->SetVisibility(UseEnglish ? Aero::Visibility_Visible : Aero::Visibility_Collapsed);
    if (Chinese)
        Chinese->SetVisibility(UseEnglish ? Aero::Visibility_Collapsed : Aero::Visibility_Visible);
}

void GlueMenuController::ApplyLanguage()
{
    SetTextPairVisibility(mMainSingleLabelEn, mMainSingleLabelZh);
    SetTextPairVisibility(mMainMultiplayerLabelEn, mMainMultiplayerLabelZh);
    SetTextPairVisibility(mMainOptionsLabelEn, mMainOptionsLabelZh);
    SetTextPairVisibility(mMainFavoritesLabelEn, mMainFavoritesLabelZh);
    SetTextPairVisibility(mMainHotkeysLabelEn, mMainHotkeysLabelZh);
    SetTextPairVisibility(mMainCreditsLabelEn, mMainCreditsLabelZh);
    SetTextPairVisibility(mMainIntroLabelEn, mMainIntroLabelZh);
    SetTextPairVisibility(mMainExitLabelEn, mMainExitLabelZh);
    SetTextPairVisibility(mLanguageLabelEn, mLanguageLabelZh);
    SetTextPairVisibility(mSingleSelectTitleEn, mSingleSelectTitleZh);
    SetTextPairVisibility(mOriginalLabelEn, mOriginalLabelZh);
    SetTextPairVisibility(mExpansionLabelEn, mExpansionLabelZh);
    SetTextPairVisibility(mCancelLabelEn, mCancelLabelZh);
}

bool GlueMenuController::HandleFallbackClick(float ViewX, float ViewY, float ViewWidth, float ViewHeight)
{
    constexpr float DesignWidth = 1920.0f;
    constexpr float DesignHeight = 1080.0f;

    float Scale = std::max(ViewWidth / DesignWidth, ViewHeight / DesignHeight);
    if (Scale <= 0.0f)
        return false;

    float OffsetX = (ViewWidth - DesignWidth * Scale) * 0.5f;
    float OffsetY = (ViewHeight - DesignHeight * Scale) * 0.5f;
    float X = (ViewX - OffsetX) / Scale;
    float Y = (ViewY - OffsetY) / Scale;

    if (X < 0.0f || Y < 0.0f || X > DesignWidth || Y > DesignHeight)
        return false;

    switch (mCurrentPage)
    {
    case Page::MainMenu:
        if (InRect(X, Y, 1557.0f, 0.0f, 351.0f, 41.0f))
        {
            EmitMenuClick();
            ToggleLanguage();
            return true;
        }
        if (InRect(X, Y, 0.0f, 68.0f, 960.0f, 337.0f))
        {
            EmitMenuClick();
            SwitchToPage(Page::SinglePlayer);
            return true;
        }
        if (InRect(X, Y, 234.0f, 428.0f, 720.0f, 360.0f))
        {
            EmitMenuClick();
            SwitchToPage(Page::CustomGame);
            return true;
        }
        if (InRect(X, Y, 0.0f, 776.0f, 480.0f, 45.0f))
        {
            EmitMenuClick();
            SwitchToPage(Page::Options);
            return true;
        }
        if (InRect(X, Y, 0.0f, 833.0f, 480.0f, 45.0f))
        {
            EmitMenuClick();
            return true;
        }
        if (InRect(X, Y, 0.0f, 889.0f, 480.0f, 45.0f))
        {
            EmitMenuClick();
            return true;
        }
        if (InRect(X, Y, 0.0f, 945.0f, 480.0f, 45.0f))
        {
            EmitMenuClick();
            return true;
        }
        if (InRect(X, Y, 0.0f, 1000.0f, 480.0f, 60.0f))
        {
            EmitMenuClick();
            SwitchToPage(Page::Cinematics);
            return true;
        }
        if (InRect(X, Y, 1185.0f, 720.0f, 693.0f, 306.0f))
        {
            EmitMenuClick();
            if (mActionCallback) mActionCallback("exit");
            return true;
        }
        break;

    case Page::Cinematics:
        if (InRect(X, Y, 1530.0f, 986.0f, 390.0f, 94.0f))
        {
            EmitMenuClick();
            SwitchToPage(Page::MainMenu);
            return true;
        }
        break;

    case Page::SinglePlayer:
        if (InRect(X, Y, 530.0f, 372.0f, 520.0f, 238.0f))
        {
            EmitMenuClick();
            mCampaignExpansion = false;
            SetCampaignRace(mCampaignRace);
            SwitchToPage(Page::Campaign);
            return true;
        }
        if (InRect(X, Y, 830.0f, 530.0f, 600.0f, 240.0f))
        {
            EmitMenuClick();
            mCampaignExpansion = true;
            SetCampaignRace(mCampaignRace);
            SwitchToPage(Page::Campaign);
            return true;
        }
        if (InRect(X, Y, 610.0f, 630.0f, 240.0f, 130.0f))
        {
            EmitMenuClick();
            SwitchToPage(Page::MainMenu);
            return true;
        }
        break;

    case Page::Campaign:
        if (mCampaignExpansion)
        {
            if (InRect(X, Y, 105.0f, 158.0f, 660.0f, 720.0f))
            {
                EmitMenuClick();
                SetCampaignRace(0);
                return true;
            }
            if (InRect(X, Y, 1206.0f, 148.0f, 690.0f, 729.0f))
            {
                EmitMenuClick();
                SetCampaignRace(1);
                return true;
            }
            if (InRect(X, Y, 528.0f, 54.0f, 858.0f, 644.0f))
            {
                EmitMenuClick();
                SetCampaignRace(2);
                return true;
            }
        }
        else
        {
            if (InRect(X, Y, 528.0f, 54.0f, 858.0f, 644.0f))
            {
                EmitMenuClick();
                SetCampaignRace(0);
                return true;
            }
            if (InRect(X, Y, 105.0f, 158.0f, 660.0f, 720.0f))
            {
                EmitMenuClick();
                SetCampaignRace(1);
                return true;
            }
            if (InRect(X, Y, 1206.0f, 148.0f, 690.0f, 729.0f))
            {
                EmitMenuClick();
                SetCampaignRace(2);
                return true;
            }
        }
        if (InRect(X, Y, 729.0f, 828.0f, 567.0f, 45.0f))
        {
            EmitMenuClick();
            const int Mission = std::max(0, GetUnlockedMissionCount() - 1);
            OpenBriefingPage(mCampaignExpansion, mCampaignRace, Mission);
            return true;
        }
        if (InRect(X, Y, 678.0f, 908.0f, 618.0f, 45.0f))
        {
            EmitMenuClick();
            SwitchToPage(Page::LoadGame);
            return true;
        }
        if (InRect(X, Y, 1410.0f, 909.0f, 474.0f, 45.0f))
        {
            EmitMenuClick();
            SwitchToPage(Page::MainMenu);
            return true;
        }
        break;

    case Page::Briefing:
        if (InRect(X, Y, 24.0f, 436.0f, 170.0f, 28.0f))
        {
            OnBriefingStart(nullptr, Aero::RoutedEventArgs());
            return true;
        }
        if (InRect(X, Y, 235.0f, 436.0f, 170.0f, 28.0f))
        {
            OnBriefingReplay(nullptr, Aero::RoutedEventArgs());
            return true;
        }
        if (InRect(X, Y, 446.0f, 436.0f, 170.0f, 28.0f))
        {
            OnBriefingBack(nullptr, Aero::RoutedEventArgs());
            return true;
        }
        break;

    case Page::CustomGame:
        if (InRect(X, Y, 180.0f, 292.0f, 864.0f, 266.0f))
        {
            EmitMenuClick();
            if (mActionCallback) mActionCallback("select_map_0");
            return true;
        }
        if (InRect(X, Y, 1431.0f, 943.0f, 396.0f, 50.0f))
        {
            EmitMenuClick();
            SwitchToPage(Page::MainMenu);
            return true;
        }
        if (InRect(X, Y, 1323.0f, 861.0f, 456.0f, 50.0f))
        {
            EmitMenuClick();
            if (mActionCallback) mActionCallback("start_custom_game");
            return true;
        }
        break;

    case Page::Options:
        if (InRect(X, Y, 1320.0f, 870.0f, 480.0f, 72.0f))
        {
            EmitMenuClick();
            SwitchToPage(Page::MainMenu);
            return true;
        }
        if (InRect(X, Y, 480.0f, 870.0f, 360.0f, 72.0f))
        {
            EmitMenuClick();
            return true;
        }
        break;

    case Page::LoadGame:
        if (InRect(X, Y, 1210.0f, 860.0f, 380.0f, 72.0f) ||
            InRect(X, Y, 840.0f, 860.0f, 360.0f, 72.0f))
        {
            EmitMenuClick();
            SwitchToPage(Page::Campaign);
            return true;
        }
        break;
    }

    return false;
}

void GlueMenuController::SwitchToPage(Page NewPage)
{
    if (mCurrentPage == NewPage)
        return;

    Page OldPage = mCurrentPage;
    if (!((OldPage == Page::MainMenu && (NewPage == Page::SinglePlayer || NewPage == Page::Options)) ||
          (OldPage == Page::Campaign && (NewPage == Page::LoadGame || NewPage == Page::Briefing))))
        SetPageVisibility(OldPage, false);

    mCurrentPage = NewPage;
    if (NewPage == Page::SinglePlayer || NewPage == Page::Options)
    {
        SetPageVisibility(Page::MainMenu, true);
    }
    else if (NewPage == Page::LoadGame)
    {
        SetPageVisibility(Page::Campaign, true);
    }
    else if (NewPage != Page::MainMenu)
    {
        SetPageVisibility(Page::MainMenu, false);
    }

    SetPageVisibility(mCurrentPage, true);
    if (mCurrentPage == Page::MainMenu)
    {
        StopMainMenuAnimations();
        SetCampaignAnimationsEnabled(false, false);
    }
    else if (mCurrentPage == Page::Campaign)
    {
        StopMainMenuAnimations();
        SetCampaignAnimationsEnabled(true, true);
    }
    else
    {
        StopMainMenuAnimations();
        SetCampaignAnimationsEnabled(false, false);
    }
}

void GlueMenuController::SetPageVisibility(Page Page, bool Visible)
{
    auto* Border = GetPageBorder(Page);
    if (!Border) return;
    Border->SetVisibility(Visible ? Aero::Visibility_Visible : Aero::Visibility_Collapsed);
}

Aero::Border* GlueMenuController::GetPageBorder(Page Page)
{
    switch (Page)
    {
        case Page::MainMenu:     return mMainMenuPage;
        case Page::Cinematics:   return mCinematicsPage;
        case Page::SinglePlayer: return mSinglePlayerPage;
        case Page::Campaign:     return mCampaignPage;
        case Page::CustomGame:   return mCustomGamePage;
        case Page::Options:      return mOptionsPage;
        case Page::LoadGame:     return mLoadGamePage;
        case Page::Briefing:     return mBriefingPage;
    }
    return nullptr;
}

NS_IMPLEMENT_REFLECTION(GlueMenuController, "UI.Glue.GlueMenuController")
{
    NsProp("CampaignRace", &GlueMenuController::GetCampaignRace, &GlueMenuController::SetCampaignRace);
}

} // namespace Game::Client::UI::Glue
