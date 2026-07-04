#include "Game/IScriptVM.h"

#include <cstdio>
#include <cstdlib>
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
}

int main()
{
    Game::IScriptDatabase Database;
    Require(Database.Load("assets/scripts/iscript.bin", "assets/arr/images.dat"),
        "load iscript database");

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

    auto UnderSprite = RunOverlayOpcode(Game::IScriptOpcodes::Opc_SprUl, 323, 7, 8);
    Require(UnderSprite.Type == Game::IScriptEventType::CreateSpriteOverlay,
        "SprUl emits a sprite overlay event, not an image overlay");
    Require(UnderSprite.A == 323 && UnderSprite.B == 7 && UnderSprite.C == 8 && UnderSprite.D == 0,
        "SprUl preserves sprite overlay id, offset, and draw-below flag");

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

    std::printf("IScriptOverlayTest passed\n");
    return 0;
}
