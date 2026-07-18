#include "Core/Audio/AudioSystem.h"
#include "Game/IScriptVM.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <vector>

namespace
{
    void Require(bool Value, const char* Message)
    {
        if (!Value)
        {
            std::fprintf(stderr, "FAIL: %s\n", Message);
            std::exit(1);
        }
    }

    int WalkingFrameSignatureForDirection(const Game::IScriptDatabase& Database,
                                          int ImageID,
                                          int Direction)
    {
        const auto* ImageInfo = Database.FindImageInfo(ImageID);
        Require(ImageInfo && ImageInfo->IScriptID >= 0, "image has iscript");
        const auto* Script = Database.FindScript(ImageInfo->IScriptID);
        Require(Script, "image script exists");
        Require(Game::IScriptAnims::Anim_Walking < (int)Script->AnimationPC.size() &&
                Script->AnimationPC[(size_t)Game::IScriptAnims::Anim_Walking] > 0,
            "image has walking animation");

        Game::IScriptVM VM;
        VM.LoadScript(Game::IScriptAnims::Anim_Walking,
                      &Database.GetProgram().ProgramData,
                      Script->AnimationPC[(size_t)Game::IScriptAnims::Anim_Walking]);
        VM.GetContext().ImageID = ImageID;
        VM.GetContext().CurrentDirection = Direction;
        VM.GetContext().HasDirectionalFrames = ImageInfo->HasDirectionalFrames;
        int Signature = 0;
        for (int Step = 0; Step < 48 && VM.IsRunning(); ++Step)
        {
            VM.ExecuteFrame();
            Signature = Signature * 131 + VM.GetCurrentFrame();
        }
        return Signature;
    }

    Game::IScriptEvent RunOverlayOpcode(int Opcode, int A, int B, int C)
    {
        std::vector<int> Program = {
            Opcode, A, B, C,
            Game::IScriptOpcodes::Opc_End
        };
        Game::IScriptVM VM;
        VM.LoadScript(0, &Program, 0);
        VM.ExecuteFrame();
        auto Events = VM.ConsumeEvents();
        Require(!Events.empty(), "overlay opcode emits an event");
        return Events.front();
    }

    Game::IScriptEvent RunOverlayOpcode2(int Opcode, int A, int B)
    {
        std::vector<int> Program = {
            Opcode, A, B,
            Game::IScriptOpcodes::Opc_End
        };
        Game::IScriptVM VM;
        VM.LoadScript(0, &Program, 0);
        VM.ExecuteFrame();
        auto Events = VM.ConsumeEvents();
        Require(!Events.empty(), "two-operand overlay opcode emits an event");
        return Events.front();
    }
}

int main()
{
    const std::string ProjectileSoundPath =
        Core::Audio::NormalizeArchiveSoundPath("bullet\\blastcan.wav");
    Require(ProjectileSoundPath == "sound/bullet/blastcan.wav",
        "sfxdata relative paths are rooted in the classic sound directory");
    Require(std::filesystem::exists(
                std::filesystem::path("assets") / ProjectileSoundPath),
        "normalized projectile sound resolves to an extracted archive asset");

    Core::Audio::SoundType SoundMetadata;
    SoundMetadata.Priority = 47;
    SoundMetadata.Flags =
        static_cast<uint8_t>(Core::Audio::SoundFlags::PerUnitType);
    SoundMetadata.MinVolume = 12;
    const auto PlaybackPolicy = Core::Audio::ResolveSoundPlaybackPolicy(
        &SoundMetadata, 3, Core::Audio::SoundFlags::None);
    Require(PlaybackPolicy.Priority == 47 &&
            PlaybackPolicy.Flags == SoundMetadata.Flags &&
            PlaybackPolicy.MinVolume == 12,
        "archive priority flags and minimum volume drive sound playback");

    std::vector<int> RandomSoundProgram = {
        Game::IScriptOpcodes::Opc_PlaySndRand,
        4, 101, 202, 303, 404,
        Game::IScriptOpcodes::Opc_AttackMelee,
        3, 501, 502, 503,
        Game::IScriptOpcodes::Opc_End
    };
    Game::IScriptVM RandomSoundVM;
    RandomSoundVM.LoadScript(0, &RandomSoundProgram, 0);
    RandomSoundVM.GetContext().RandomSeed = 1;
    RandomSoundVM.ExecuteFrame();
    const auto RandomSoundEvents = RandomSoundVM.ConsumeEvents();
    Require(RandomSoundEvents.size() == 3,
        "playsndrand and attackmelee emit their authored sound events");
    Require(RandomSoundEvents[0].Type ==
                Game::IScriptEventType::PlayRandomSound &&
            RandomSoundEvents[0].A == 303 &&
            RandomSoundEvents[0].B == 4,
        "playsndrand selects deterministically across the complete vector");
    Require(RandomSoundEvents[1].Type ==
                Game::IScriptEventType::AttackMelee &&
            RandomSoundEvents[2].Type ==
                Game::IScriptEventType::PlayRandomSound &&
            RandomSoundEvents[2].A >= 501 &&
            RandomSoundEvents[2].A <= 503 &&
            RandomSoundEvents[2].B == 3,
        "attackmelee preserves damage ordering and its complete sound vector");

    Game::IScriptDatabase Database;
    Require(Database.Load("assets/scripts/iscript.bin", "assets/arr/images.dat"),
        "load iscript database");

    const auto* ShieldInfo = Database.FindImageInfo(1);
    const auto* ShieldScript = ShieldInfo && ShieldInfo->IScriptID >= 0
        ? Database.FindScript(ShieldInfo->IScriptID)
        : nullptr;
    Require(ShieldScript && !ShieldScript->AnimationPC.empty(),
        "Protoss shield mask has a real Init iscript");
    Game::IScriptVM ShieldVM;
    ShieldVM.LoadScript(Game::IScriptAnims::Anim_Init,
        &Database.GetProgram().ProgramData,
        ShieldScript->AnimationPC[(size_t)Game::IScriptAnims::Anim_Init]);
    int ShieldFollowCount = 0;
    for (int Frame = 0; Frame < 16 && ShieldVM.IsRunning(); ++Frame)
    {
        ShieldVM.ExecuteFrame();
        if (ShieldVM.GetContext().FollowMainGraphic)
            ++ShieldFollowCount;
    }
    Require(ShieldFollowCount >= 2,
        "real Protoss shield mask repeatedly executes followmaingraphic");

    const auto* RefineryInfo = Database.FindImageInfo(307);
    const auto* RefineryScript = RefineryInfo && RefineryInfo->IScriptID >= 0
        ? Database.FindScript(RefineryInfo->IScriptID)
        : nullptr;
    Require(RefineryScript, "Terran Refinery has a real iscript");
    auto RunRefineryAnimation = [&](int Anim, bool& SawShadow) -> int
    {
        Require(Anim < (int)RefineryScript->AnimationPC.size() &&
                RefineryScript->AnimationPC[(size_t)Anim] > 0,
            "Terran Refinery exposes the requested real animation");
        Game::IScriptVM RefineryVM;
        RefineryVM.LoadScript(Anim, &Database.GetProgram().ProgramData,
            RefineryScript->AnimationPC[(size_t)Anim]);
        RefineryVM.ExecuteFrame();
        for (const auto& Event : RefineryVM.ConsumeEvents())
        {
            SawShadow = SawShadow ||
                (Event.Type == Game::IScriptEventType::CreateImageOverlay &&
                 Event.A == 308 && Event.B == 0 && Event.C == 0 && Event.D == 0);
        }
        return RefineryVM.GetCurrentFrame();
    };
    bool SawRefineryShadow = false;
    Require(RunRefineryAnimation(Game::IScriptAnims::Anim_Init, SawRefineryShadow) == 1,
        "Refinery Init selects its fragmented construction frame");
    Require(SawRefineryShadow,
        "Refinery Init attaches its project shadow at the sprite origin");
    bool BuiltCreatedShadow = false;
    Require(RunRefineryAnimation(Game::IScriptAnims::Anim_Built, BuiltCreatedShadow) == 0,
        "Refinery Built selects the complete project frame");

    std::vector<int> FollowMainProgram = {
        Game::IScriptOpcodes::Opc_FollowMainGraphic,
        Game::IScriptOpcodes::Opc_PlayFram, 9,
        Game::IScriptOpcodes::Opc_End
    };
    Game::IScriptVM FollowMainVM;
    FollowMainVM.LoadScript(0, &FollowMainProgram, 0);
    FollowMainVM.ExecuteFrame();
    Require(FollowMainVM.GetContext().FollowMainGraphic &&
            FollowMainVM.GetContext().BaseFrameIndex == 9,
        "followmaingraphic raises a frame request and preserves the next opcode boundary");

    constexpr int kMissileTurretBaseImage = 296;
    constexpr int kMissileTurretTurretImage = 297;
    const auto* ImageInfo = Database.FindImageInfo(kMissileTurretBaseImage);
    Require(ImageInfo && ImageInfo->IScriptID >= 0,
        "missile turret base has iscript");

    const auto* Script = Database.FindScript(ImageInfo->IScriptID);
    Require(Script, "missile turret base script exists");
    bool SawTurretOverlay = false;
    int OverlayAnim = -1;
    int OverlayX = 0;
    int OverlayY = 0;
    for (int Anim = 0; Anim < (int)Script->AnimationPC.size() && !SawTurretOverlay; ++Anim)
    {
        if (Script->AnimationPC[(size_t)Anim] <= 0)
            continue;

        Game::IScriptVM VM;
        VM.LoadScript(Anim,
                      &Database.GetProgram().ProgramData,
                      Script->AnimationPC[(size_t)Anim]);
        VM.GetContext().ImageID = kMissileTurretBaseImage;

        for (int Frame = 0; Frame < 32 && VM.IsRunning() && !SawTurretOverlay; ++Frame)
        {
            VM.ExecuteFrame();
            for (const auto& Event : VM.ConsumeEvents())
            {
                if (Event.Type == Game::IScriptEventType::CreateImageOverlay &&
                    Event.A == kMissileTurretTurretImage)
                {
                    SawTurretOverlay = true;
                    OverlayAnim = Anim;
                    OverlayX = Event.B;
                    OverlayY = Event.C;
                    break;
                }
            }
        }
    }

    Require(SawTurretOverlay,
        "missile turret base creates missileT image overlay");
    Require(OverlayAnim == Game::IScriptAnims::Anim_Built,
        "missile turret overlay is created by the Built animation");
    Require(OverlayX > -64 && OverlayX < 64 && OverlayY > -64 && OverlayY < 64,
        "missile turret overlay offset stays near the base");

    const auto* BattlecruiserInfo = Database.FindImageInfo(218);
    Require(BattlecruiserInfo && BattlecruiserInfo->IScriptID >= 0,
        "Battlecruiser image has iscript metadata");
    const auto* BattlecruiserScript = Database.FindScript(BattlecruiserInfo->IScriptID);
    Require(BattlecruiserScript && !BattlecruiserScript->AnimationPC.empty(),
        "Battlecruiser iscript has Init animation");
    bool SawBattlecruiserEngine = false;
    int BattlecruiserEngineAnim = -1;
    for (int Anim = 0; Anim < (int)BattlecruiserScript->AnimationPC.size(); ++Anim)
    {
        if (BattlecruiserScript->AnimationPC[(size_t)Anim] <= 0)
            continue;
        Game::IScriptVM BattlecruiserVM;
        BattlecruiserVM.LoadScript(Anim, &Database.GetProgram().ProgramData,
            BattlecruiserScript->AnimationPC[(size_t)Anim]);
        BattlecruiserVM.GetContext().ImageID = 218;
        for (int Frame = 0; Frame < 32 && BattlecruiserVM.IsRunning(); ++Frame)
        {
            BattlecruiserVM.ExecuteFrame();
            for (const auto& Event : BattlecruiserVM.ConsumeEvents())
            {
                if (Event.Type == Game::IScriptEventType::CreateImageOverlay &&
                    Event.A == 220)
                {
                    SawBattlecruiserEngine = true;
                    BattlecruiserEngineAnim = Anim;
                }
            }
        }
    }
    Require(SawBattlecruiserEngine,
        "real Battlecruiser iscript creates its movement engine image");
    Require(BattlecruiserEngineAnim == Game::IScriptAnims::Anim_Walking,
        "Battlecruiser engine flame is attached by Walking rather than Idle");

    auto HighSprite = RunOverlayOpcode(Game::IScriptOpcodes::Opc_HighSprOl, 321, 4, -3);
    Require(HighSprite.Type == Game::IScriptEventType::CreateSpriteOverlay,
        "HighSprOl emits a sprite overlay event, not an image overlay");
    Require(HighSprite.A == 321 && HighSprite.B == 4 && HighSprite.C == -3 && HighSprite.D == 1,
        "HighSprOl preserves sprite overlay id, offset, and draw-above flag");

    auto LowSprite = RunOverlayOpcode(Game::IScriptOpcodes::Opc_LowSprUl, 322, -5, 6);
    Require(LowSprite.Type == Game::IScriptEventType::CreateSpriteOverlay,
        "LowSprUl emits a sprite overlay event, not an image overlay");
    Require(LowSprite.A == 322 && LowSprite.B == -5 && LowSprite.C == 6 && LowSprite.D == 0,
        "LowSprUl preserves sprite overlay id, offset, and draw-below flag");

    const auto ImageLo = RunOverlayOpcode(
        Game::IScriptOpcodes::Opc_ImgOlUseLo, 401, 2, 7);
    Require(ImageLo.Type == Game::IScriptEventType::CreateImageOverlayLo &&
            ImageLo.A == 401 && ImageLo.B == 2 && ImageLo.C == 7 &&
            ImageLo.D == 1,
        "ImgOlUseLo preserves LO table type/index instead of treating them as x/y");

    const auto ImageUnderLo = RunOverlayOpcode(
        Game::IScriptOpcodes::Opc_ImgUlUseLo, 402, 3, 5);
    Require(ImageUnderLo.Type == Game::IScriptEventType::CreateImageOverlayLo &&
            ImageUnderLo.A == 402 && ImageUnderLo.B == 3 &&
            ImageUnderLo.C == 5 && ImageUnderLo.D == 0,
        "ImgUlUseLo preserves LO table type/index and draw-below ordering");

    const auto SpriteUnderLo = RunOverlayOpcode(
        Game::IScriptOpcodes::Opc_SprUlUseLo, 403, 4, 6);
    Require(SpriteUnderLo.Type == Game::IScriptEventType::CreateSpriteOverlay &&
            SpriteUnderLo.A == 403 && SpriteUnderLo.B == 4 &&
            SpriteUnderLo.C == 6 && SpriteUnderLo.D == 0,
        "SprUlUseLo preserves its explicit x/y offset and draw-below ordering");

    const auto SpriteOverLo = RunOverlayOpcode2(
        Game::IScriptOpcodes::Opc_SprOlUseLo, 404, 1);
    Require(SpriteOverLo.Type == Game::IScriptEventType::CreateSpriteOverlayLo &&
            SpriteOverLo.A == 404 && SpriteOverLo.B == 1 &&
            SpriteOverLo.C == 0 && SpriteOverLo.D == 1,
        "SprOlUseLo retains its LO table type and canonical index zero");

    bool SawRealLoOverlay = false;
    std::set<std::pair<int, int>> AuditedScriptAnimations;
    for (int ImageID = 0; ImageID < 999 && !SawRealLoOverlay; ++ImageID)
    {
        const auto* LoImageInfo = Database.FindImageInfo(ImageID);
        const auto* LoScript = LoImageInfo && LoImageInfo->IScriptID >= 0
            ? Database.FindScript(LoImageInfo->IScriptID)
            : nullptr;
        if (!LoScript)
            continue;
        for (int Anim = 0;
             Anim < (int)LoScript->AnimationPC.size() && !SawRealLoOverlay;
             ++Anim)
        {
            const int EntryPC = LoScript->AnimationPC[(size_t)Anim];
            if (EntryPC <= 0 ||
                !AuditedScriptAnimations.emplace(
                    LoImageInfo->IScriptID, Anim).second)
            {
                continue;
            }
            Game::IScriptVM VM;
            VM.LoadScript(Anim, &Database.GetProgram().ProgramData, EntryPC);
            VM.GetContext().ImageID = ImageID;
            VM.GetContext().HasDirectionalFrames =
                LoImageInfo->HasDirectionalFrames;
            for (int Step = 0; Step < 256 && VM.IsRunning(); ++Step)
            {
                VM.ExecuteFrame();
                for (const auto& Event : VM.ConsumeEvents())
                {
                    if (Event.Type ==
                            Game::IScriptEventType::CreateImageOverlayLo ||
                        Event.Type ==
                            Game::IScriptEventType::CreateSpriteOverlayLo)
                    {
                        SawRealLoOverlay = true;
                        break;
                    }
                }
            }
        }
    }
    Require(SawRealLoOverlay,
        "real Brood War IScript emits an LO-indexed weapon/effect overlay");

    std::vector<int> NextImageProgram = {
        Game::IScriptOpcodes::Opc_ImgUlNextId, -4, 7,
        Game::IScriptOpcodes::Opc_End
    };
    Game::IScriptVM NextImageVM;
    NextImageVM.LoadScript(0, &NextImageProgram, 0);
    NextImageVM.GetContext().ImageID = 218;
    NextImageVM.ExecuteFrame();
    const auto NextImageEvents = NextImageVM.ConsumeEvents();
    Require(NextImageEvents.size() == 1 &&
            NextImageEvents[0].Type == Game::IScriptEventType::CreateImageOverlay &&
            NextImageEvents[0].A == 219 &&
            NextImageEvents[0].B == -4 && NextImageEvents[0].C == 7 &&
            NextImageEvents[0].D == 0,
        "ImgUlNextId creates the next image below using both operands as offsets");

    std::vector<int> SwitchUnderProgram = {
        Game::IScriptOpcodes::Opc_SwitchUl, 220,
        Game::IScriptOpcodes::Opc_PlayFram, 9,
        Game::IScriptOpcodes::Opc_End
    };
    Game::IScriptVM SwitchUnderVM;
    SwitchUnderVM.LoadScript(0, &SwitchUnderProgram, 0);
    SwitchUnderVM.ExecuteFrame();
    const auto SwitchUnderEvents = SwitchUnderVM.ConsumeEvents();
    Require(SwitchUnderEvents.size() == 1 &&
            SwitchUnderEvents[0].Type ==
                Game::IScriptEventType::CreateImageOverlaySpecialOffset &&
            SwitchUnderEvents[0].A == 220 && SwitchUnderEvents[0].D == 0,
        "SwitchUl creates a below-image that tracks the main image LO offset");
    Require(SwitchUnderVM.GetContext().BaseFrameIndex == 9,
        "SwitchUl consumes one image operand and preserves the next opcode boundary");

    auto UnderSprite = RunOverlayOpcode(Game::IScriptOpcodes::Opc_SprUl, 323, 7, 8);
    Require(UnderSprite.Type == Game::IScriptEventType::CreateSpriteOverlay,
        "SprUl emits a sprite overlay event, not an image overlay");
    Require(UnderSprite.A == 323 && UnderSprite.B == 7 && UnderSprite.C == 8 && UnderSprite.D == 0,
        "SprUl preserves sprite overlay id, offset, and draw-below flag");

    std::vector<int> SignalProgram = {
        Game::IScriptOpcodes::Opc_SigOrder, 0x01,
        Game::IScriptOpcodes::Opc_SigOrder, 0x04,
        Game::IScriptOpcodes::Opc_OrderDone, 0x01,
        Game::IScriptOpcodes::Opc_End
    };
    Game::IScriptVM SignalVM;
    SignalVM.LoadScript(0, &SignalProgram, 0);
    SignalVM.ExecuteFrame();
    Require(SignalVM.GetContext().OrderSignal == 0x04,
        "sigorder accumulates bits and orderdone clears only its requested bits");
    Require(!SignalVM.GetContext().OrderDone,
        "orderdone does not finish while another order signal remains active");

    std::vector<int> FlightProgram = {
        Game::IScriptOpcodes::Opc_SetFlDirect, 3,
        Game::IScriptOpcodes::Opc_SetFlSpeed, 0x1234,
        Game::IScriptOpcodes::Opc_PlayFram, 7,
        Game::IScriptOpcodes::Opc_End
    };
    Game::IScriptVM FlightVM;
    FlightVM.LoadScript(0, &FlightProgram, 0);
    FlightVM.ExecuteFrame();
    const auto FlightEvents = FlightVM.ConsumeEvents();
    Require(FlightVM.GetContext().FlDirect == 24 &&
            FlightVM.GetContext().FlSpeed == 0x1234,
        "flight opcodes use the OpenBW heading unit and one 16-bit speed operand");
    Require(FlightEvents.size() == 2 &&
            FlightEvents[0].Type == Game::IScriptEventType::FlightState &&
            FlightEvents[0].C == 1 &&
            FlightEvents[1].Type == Game::IScriptEventType::FlightState &&
            FlightEvents[1].C == 2,
        "flight events distinguish heading changes from top-speed changes");
    Require(FlightVM.GetContext().BaseFrameIndex == 7,
        "setflspeed preserves the following opcode boundary");

    std::vector<int> WarpProgram = {
        Game::IScriptOpcodes::Opc_WarpOverlay, 0x3412,
        Game::IScriptOpcodes::Opc_PlayFram, 9,
        Game::IScriptOpcodes::Opc_End
    };
    Game::IScriptVM WarpVM;
    WarpVM.LoadScript(0, &WarpProgram, 0);
    WarpVM.ExecuteFrame();
    auto WarpEvents = WarpVM.ConsumeEvents();
    Require(!WarpEvents.empty() &&
            WarpEvents.front().Type == Game::IScriptEventType::WarpOverlay &&
            WarpEvents.front().A == 0x12 && WarpEvents.front().B == 0x34,
        "warpoverlay splits its packed modifier operand without treating it as an image id");
    Require(WarpVM.GetContext().HasWarpOverlay &&
            WarpVM.GetContext().WarpOverlayFrame == 0x12,
        "warpoverlay retains modifier_data1 for textured root rendering");
    Require(WarpVM.GetContext().BaseFrameIndex == 9,
        "warpoverlay consumes one operand and preserves the following opcode boundary");

    std::vector<int> TemporaryGraphicProgram = {
        Game::IScriptOpcodes::Opc_TmpRmGraphicStart,
        Game::IScriptOpcodes::Opc_Wait, 1,
        Game::IScriptOpcodes::Opc_TmpRmGraphicEnd,
        Game::IScriptOpcodes::Opc_PlayFram, 11,
        Game::IScriptOpcodes::Opc_End
    };
    Game::IScriptVM TemporaryGraphicVM;
    TemporaryGraphicVM.LoadScript(0, &TemporaryGraphicProgram, 0);
    TemporaryGraphicVM.ExecuteFrame();
    auto TemporaryEvents = TemporaryGraphicVM.ConsumeEvents();
    Require(!TemporaryEvents.empty() &&
            TemporaryEvents.front().Type == Game::IScriptEventType::TemporaryGraphicState &&
            TemporaryEvents.front().A == 1,
        "tmprmgraphicstart reports temporary removal without owning renderer visibility");
    TemporaryGraphicVM.ExecuteFrame();
    TemporaryEvents = TemporaryGraphicVM.ConsumeEvents();
    Require(!TemporaryEvents.empty() &&
            TemporaryEvents.front().Type == Game::IScriptEventType::TemporaryGraphicState &&
            TemporaryEvents.front().A == 0 &&
            TemporaryGraphicVM.GetContext().BaseFrameIndex == 11,
        "wait 1 resumes on the next frame before tmprmgraphicend restores the image");

    const int GoliathEast = WalkingFrameSignatureForDirection(Database, 234, 0);
    const int GoliathWest = WalkingFrameSignatureForDirection(Database, 234, 128);
    const int TankEast = WalkingFrameSignatureForDirection(Database, 250, 0);
    const int TankWest = WalkingFrameSignatureForDirection(Database, 250, 128);
    Require(GoliathEast != GoliathWest,
        "goliath walking iscript uses current direction");
    Require(TankEast != TankWest,
        "tank walking iscript uses current direction");

    const auto* GoliathImage = Database.FindImageInfo(234);
    Require(GoliathImage && GoliathImage->IScriptID >= 0,
        "goliath image info exists");
    const auto* GoliathScript = Database.FindScript(GoliathImage->IScriptID);
    Require(GoliathScript &&
            Game::IScriptAnims::Anim_Walking < (int)GoliathScript->AnimationPC.size() &&
            GoliathScript->AnimationPC[(size_t)Game::IScriptAnims::Anim_Walking] > 0,
        "goliath walking script exists");
    Game::IScriptVM GoliathVM;
    GoliathVM.LoadScript(Game::IScriptAnims::Anim_Walking,
                         &Database.GetProgram().ProgramData,
                         GoliathScript->AnimationPC[(size_t)Game::IScriptAnims::Anim_Walking]);
    GoliathVM.GetContext().ImageID = 234;
    GoliathVM.GetContext().HasDirectionalFrames = GoliathImage->HasDirectionalFrames;
    GoliathVM.GetContext().CurrentDirection = 0;
    GoliathVM.ExecuteFrame();
    const int FirstDirectionFrame = GoliathVM.GetCurrentFrame();
    GoliathVM.GetContext().CurrentDirection = 128;
    GoliathVM.RefreshDirectionalFrame();
    Require(GoliathVM.GetCurrentFrame() != FirstDirectionFrame,
        "directional walking frame refreshes even while the script is waiting");

    const auto* ScvImage = Database.FindImageInfo(247);
    const auto* ScvScript = ScvImage && ScvImage->IScriptID >= 0
        ? Database.FindScript(ScvImage->IScriptID)
        : nullptr;
    Require(ScvScript, "SCV image script exists");
    const auto HasScvAnim = [ScvScript](int Anim) {
        return Anim >= 0 && Anim < (int)ScvScript->AnimationPC.size() &&
               ScvScript->AnimationPC[(size_t)Anim] > 0;
    };
    Require(HasScvAnim(Game::IScriptAnims::Anim_AlmostBuilt),
        "project SCV exposes AlmostBuilt for mineral mining");
    Require(HasScvAnim(Game::IScriptAnims::Anim_GndAttkToIdle),
        "project SCV exposes GndAttkToIdle for leaving mineral mining");
    Require(!HasScvAnim(Game::IScriptAnims::Anim_IsWorking),
        "project SCV does not use the production-building IsWorking entry");

    Game::IScriptVM ScvMiningVM;
    ScvMiningVM.LoadScript(Game::IScriptAnims::Anim_AlmostBuilt,
                           &Database.GetProgram().ProgramData,
                           ScvScript->AnimationPC[(size_t)Game::IScriptAnims::Anim_AlmostBuilt]);
    ScvMiningVM.GetContext().ImageID = 247;
    ScvMiningVM.GetContext().HasDirectionalFrames = ScvImage->HasDirectionalFrames;
    ScvMiningVM.GetContext().CurrentDirection = 64;
    int FusionCutterEvents = 0;
    for (int Step = 0; Step < 96 && ScvMiningVM.IsRunning(); ++Step)
    {
        ScvMiningVM.ExecuteFrame();
        for (const auto& Event : ScvMiningVM.ConsumeEvents())
        {
            if (Event.Type == Game::IScriptEventType::UseWeapon && Event.A == 14)
                ++FusionCutterEvents;
        }
    }
    Require(FusionCutterEvents >= 2,
        "SCV AlmostBuilt periodically emits Fusion Cutter weapon 14 while mining");

    Game::IScriptVM ScvWalkingVM;
    ScvWalkingVM.LoadScript(Game::IScriptAnims::Anim_Walking,
        &Database.GetProgram().ProgramData,
        ScvScript->AnimationPC[(size_t)Game::IScriptAnims::Anim_Walking]);
    ScvWalkingVM.GetContext().ImageID = 247;
    ScvWalkingVM.GetContext().HasDirectionalFrames = ScvImage->HasDirectionalFrames;
    ScvWalkingVM.GetContext().CurrentDirection = 64;
    bool SawWalkingGlow = false;
    for (int Step = 0; Step < 48 && ScvWalkingVM.IsRunning(); ++Step)
    {
        ScvWalkingVM.ExecuteFrame();
        for (const auto& Event : ScvWalkingVM.ConsumeEvents())
        {
            SawWalkingGlow = SawWalkingGlow ||
                (Event.Type == Game::IScriptEventType::CreateImageOverlay &&
                 Event.A == 249);
        }
    }
    Require(SawWalkingGlow,
        "project SCV Walking attaches the tscGlow dynamic image");

    const auto* ScvSparkImage = Database.FindImageInfo(526);
    const auto* ScvSparkScript = ScvSparkImage && ScvSparkImage->IScriptID >= 0
        ? Database.FindScript(ScvSparkImage->IScriptID)
        : nullptr;
    Require(ScvSparkScript, "SCV spark image script exists");
    Require(Game::IScriptAnims::Anim_Init < (int)ScvSparkScript->AnimationPC.size() &&
            ScvSparkScript->AnimationPC[(size_t)Game::IScriptAnims::Anim_Init] > 0,
        "project SCV spark image exposes its own Init animation");

    const auto* GeyserImage = Database.FindImageInfo(344);
    const auto* GeyserScript = GeyserImage && GeyserImage->IScriptID >= 0
        ? Database.FindScript(GeyserImage->IScriptID)
        : nullptr;
    Require(GeyserScript &&
            Game::IScriptAnims::Anim_Init < (int)GeyserScript->AnimationPC.size() &&
            GeyserScript->AnimationPC[(size_t)Game::IScriptAnims::Anim_Init] > 0,
        "Vespene geyser exposes the Init script that creates smoke layers");
    Game::IScriptVM GeyserVM;
    GeyserVM.LoadScript(Game::IScriptAnims::Anim_Init,
        &Database.GetProgram().ProgramData,
        GeyserScript->AnimationPC[(size_t)Game::IScriptAnims::Anim_Init]);
    GeyserVM.GetContext().ImageID = 344;
    bool GeyserSmokeSlots[3] = {};
    for (int Step = 0; Step < 192 && GeyserVM.IsRunning(); ++Step)
    {
        GeyserVM.ExecuteFrame();
        for (const auto& Event : GeyserVM.ConsumeEvents())
        {
            if (Event.Type == Game::IScriptEventType::CreateGasOverlays &&
                Event.A >= 0 && Event.A < 3)
            {
                GeyserSmokeSlots[Event.A] = true;
            }
        }
    }
    Require(GeyserSmokeSlots[0] && GeyserSmokeSlots[1] && GeyserSmokeSlots[2],
        "Vespene geyser Init emits all three animated smoke channels used by the project script");

    std::printf("IScriptOverlayTest passed\n");
    return 0;
}
