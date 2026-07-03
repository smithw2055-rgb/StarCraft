#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Game
{

namespace IScriptOpcodes
{
    enum : int
    {
        Opc_PlayFram = 0x00,
        Opc_PlayFramTile = 0x01,
        Opc_SetHorPos = 0x02,
        Opc_SetVertPos = 0x03,
        Opc_SetPos = 0x04,
        Opc_Wait = 0x05,
        Opc_WaitRand = 0x06,
        Opc_Goto = 0x07,
        Opc_ImgOl = 0x08,
        Opc_ImgUl = 0x09,
        Opc_ImgOlOrig = 0x0A,
        Opc_SwitchUl = 0x0B,
        Opc_Unknown0C = 0x0C,
        Opc_ImgOlUseLo = 0x0D,
        Opc_ImgUlUseLo = 0x0E,
        Opc_SprOl = 0x0F,
        Opc_HighSprOl = 0x10,
        Opc_LowSprUl = 0x11,
        Opc_UflUnstable = 0x12,
        Opc_SprUlUseLo = 0x13,
        Opc_SprUl = 0x14,
        Opc_SprOlUseLo = 0x15,
        Opc_End = 0x16,
        Opc_SetFlipState = 0x17,
        Opc_PlaySnd = 0x18,
        Opc_PlaySndRand = 0x19,
        Opc_PlaySndBtwn = 0x1A,
        Opc_DoMissileDmg = 0x1B,
        Opc_AttackMelee = 0x1C,
        Opc_FollowMainGraphic = 0x1D,
        Opc_RandCondJmp = 0x1E,
        Opc_TurnCCWise = 0x1F,
        Opc_TurnCWise = 0x20,
        Opc_Turn1CWise = 0x21,
        Opc_TurnRand = 0x22,
        Opc_SetSpawnFrame = 0x23,
        Opc_SigOrder = 0x24,
        Opc_AttackWith = 0x25,
        Opc_Attack = 0x26,
        Opc_CastSpell = 0x27,
        Opc_UseWeapon = 0x28,
        Opc_Move = 0x29,
        Opc_GotoRepeatAttk = 0x2A,
        Opc_EngFrame = 0x2B,
        Opc_EngSet = 0x2C,
        Opc_Unknown2D = 0x2D,
        Opc_NoBrkCodeStart = 0x2E,
        Opc_NoBrkCodeEnd = 0x2F,
        Opc_IgnoreRest = 0x30,
        Opc_AttkShiftProj = 0x31,
        Opc_TmpRmGraphicStart = 0x32,
        Opc_TmpRmGraphicEnd = 0x33,
        Opc_SetFlDirect = 0x34,
        Opc_Call = 0x35,
        Opc_Return = 0x36,
        Opc_SetFlSpeed = 0x37,
        Opc_CreateGasOverlays = 0x38,
        Opc_PwrUpCondJmp = 0x39,
        Opc_TrgtRangeCondJmp = 0x3A,
        Opc_TrgtArcCondJmp = 0x3B,
        Opc_CurDirectCondJmp = 0x3C,
        Opc_ImgUlNextId = 0x3D,
        Opc_Unknown3E = 0x3E,
        Opc_LiftOffCondJmp = 0x3F,
        Opc_WarpOverlay = 0x40,
        Opc_OrderDone = 0x41,
        Opc_GrdSprOl = 0x42,
        Opc_Unknown43 = 0x43,
        Opc_DoGrdDamage = 0x44,
        kCount = 0x45
    };
}

namespace IScriptAnims
{
    enum : int
    {
        Anim_Init = 0,
        Anim_Death = 1,
        Anim_GndAttkInit = 2,
        Anim_AirAttkInit = 3,
        Anim_Unused1 = 4,
        Anim_GndAttkRpt = 5,
        Anim_AirAttkRpt = 6,
        Anim_CastSpell = 7,
        Anim_GndAttkToIdle = 8,
        Anim_AirAttkToIdle = 9,
        Anim_Unused2 = 10,
        Anim_Walking = 11,
        Anim_WalkingToIdle = 12,
        Anim_SpecialState1 = 13,
        Anim_SpecialState2 = 14,
        Anim_AlmostBuilt = 15,
        Anim_Built = 16,
        Anim_Landing = 17,
        Anim_LiftOff = 18,
        Anim_IsWorking = 19,
        Anim_WorkingToIdle = 20,
        Anim_WarpIn = 21,
        Anim_Unused3 = 22,
        Anim_StarEditInit = 23,
        Anim_Disable = 24,
        Anim_Burrow = 25,
        Anim_UnBurrow = 26,
        Anim_Enable = 27,
        kCount = 28
    };
}

struct IScriptProgram
{
    struct Script
    {
        int Id = -1;
        std::vector<int> AnimationPC;
    };

    std::vector<int> ProgramData;
    std::unordered_map<int, Script> Scripts;
};

struct IScriptImageInfo
{
    bool HasIScriptAnimations = false;
    int IScriptID = -1;
};

enum class IScriptEventType : uint8_t
{
    PlaySound,
    PlayRandomSound,
    PlaySoundBetween,
    CreateImageOverlay,
    CreateSpriteOverlay,
    CreateGasOverlays,
    WarpOverlay,
    Attack,
    AttackMelee,
    UseWeapon,
    DoMissileDamage,
    DoGroundDamage,
    CastSpell,
    OrderSignal,
    OrderDone,
    FlightState,
    SpawnFrame,
    TemporaryGraphicState
};

struct IScriptEvent
{
    IScriptEventType Type = IScriptEventType::PlaySound;
    int A = 0;
    int B = 0;
    int C = 0;
    int D = 0;
};

class IScriptDatabase
{
public:
    bool Load(const std::string& IScriptPath, const std::string& ImagesDatPath);
    const IScriptProgram::Script* FindScript(int ScriptID) const;
    const IScriptImageInfo* FindImageInfo(int ImageID) const;
    const IScriptProgram& GetProgram() const { return mProgram; }
    bool IsLoaded() const { return mLoaded; }

private:
    bool LoadImagesDat(const std::string& ImagesDatPath);
    bool LoadIScriptBin(const std::string& IScriptPath);

    IScriptProgram mProgram;
    std::vector<IScriptImageInfo> mImages;
    bool mLoaded = false;
};

class IScriptVM
{
public:
    struct SpriteContext
    {
        int SpriteID = -1;
        int ImageID = -1;
        int FrameIndex = 0;
        int HorPos = 0;
        int VertPos = 0;
        bool FlipState = false;
        int FlSpeed = 8;
        int FlDirect = 0;
        int OrderSignal = 0;
        int CurrentDirection = 0;
        int TargetRangePixels = 0;
        int TargetArc = 0;
        int RandomSeed = 1;
        bool IsWalking = false;
        bool IsAttacking = false;
        bool IsPowered = true;
        bool OrderDone = false;
        bool LiftOffState = false;
        bool HasWarpOverlay = false;
        bool SpawnFrameSet = false;
        int SpawnFrame = -1;
        int PC = 0;
    };

    IScriptVM();

    void LoadScript(int AnimationID, const std::vector<int>* ProgramData, int EntryPC);
    void Reset();
    bool ExecuteFrame();

    int GetCurrentFrame() const { return mSprite.FrameIndex; }
    bool IsRunning() const { return mSprite.PC >= 0 && mProgramData; }
    int GetCurrentAnim() const { return mCurrentAnim; }
    const std::vector<IScriptEvent>& GetEvents() const { return mEvents; }
    std::vector<IScriptEvent> ConsumeEvents();

    SpriteContext& GetContext() { return mSprite; }
    const SpriteContext& GetContext() const { return mSprite; }

private:
    using OpcodeHandler = std::function<bool(IScriptVM&)>;
    bool DispatchOpcode(int Opcode);
    int ReadArg();
    int PeekArg(int Offset = 1) const;
    void AdvanceArgs(int Count);
    void SkipVectorArgs();
    void Emit(IScriptEventType Type, int A = 0, int B = 0, int C = 0, int D = 0);
    void RegisterDefaultOpcodes();

    SpriteContext mSprite;
    const std::vector<int>* mProgramData = nullptr;
    std::vector<int> mCallStack;
    int mWaitRemaining = 0;
    int mCurrentAnim = -1;
    std::unordered_map<int, OpcodeHandler> mOpcodeHandlers;
    std::vector<IScriptEvent> mEvents;
};

} // namespace Game
