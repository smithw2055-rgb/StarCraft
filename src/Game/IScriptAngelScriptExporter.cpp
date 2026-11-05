#include "Game/IScriptAngelScriptExporter.h"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <utility>
#include <vector>

namespace Game
{

namespace
{
    struct DecodedInstruction
    {
        size_t PC = 0;
        int Opcode = 0;
        std::vector<int> Args;
        size_t NextPC = 0;
    };

    const char* GetAnimationName(size_t Animation)
    {
        static const char* Names[] =
        {
            "Anim_Init",
            "Anim_Death",
            "Anim_GndAttkInit",
            "Anim_AirAttkInit",
            "Anim_Unused1",
            "Anim_GndAttkRpt",
            "Anim_AirAttkRpt",
            "Anim_CastSpell",
            "Anim_GndAttkToIdle",
            "Anim_AirAttkToIdle",
            "Anim_Unused2",
            "Anim_Walking",
            "Anim_WalkingToIdle",
            "Anim_SpecialState1",
            "Anim_SpecialState2",
            "Anim_AlmostBuilt",
            "Anim_Built",
            "Anim_Landing",
            "Anim_LiftOff",
            "Anim_IsWorking",
            "Anim_WorkingToIdle",
            "Anim_WarpIn",
            "Anim_Unused3",
            "Anim_StarEditInit",
            "Anim_Disable",
            "Anim_Burrow",
            "Anim_UnBurrow",
            "Anim_Enable"
        };
        return Animation < sizeof(Names) / sizeof(Names[0])
            ? Names[Animation]
            : nullptr;
    }

    bool IsDigitSpec(char C)
    {
        return C == '1' || C == '2';
    }

    bool DecodeProgram(const IScriptProgram& Program,
                       std::vector<DecodedInstruction>& Instructions,
                       std::string& Error)
    {
        const auto& Words = Program.ProgramData;
        size_t PC = 0;
        while (PC < Words.size())
        {
            const int Opcode = Words[PC];
            const char* Spec = GetIScriptInstructionSpec(Opcode);
            if (!Spec)
            {
                std::ostringstream Message;
                Message << "program word " << PC << " has invalid opcode 0x"
                        << std::hex << Opcode;
                Error = Message.str();
                return false;
            }

            size_t ArgCount = 0;
            for (const char* C = Spec; *C; ++C)
            {
                if (IsDigitSpec(*C))
                {
                    ++ArgCount;
                }
                else if (*C == 's')
                {
                    ++C;
                    if (!*C || !IsDigitSpec(*C))
                    {
                        Error = "invalid signed argument specification";
                        return false;
                    }
                    ++ArgCount;
                }
                else if (*C == 'v')
                {
                    if (PC + 1 + ArgCount >= Words.size())
                    {
                        Error = "variable-length instruction is truncated";
                        return false;
                    }
                    const int Count = Words[PC + 1 + ArgCount];
                    if (Count < 0)
                    {
                        Error = "variable-length instruction has a negative count";
                        return false;
                    }
                    ArgCount += 1 + static_cast<size_t>(Count);
                }
                else if (*C == 'j' || *C == 'b')
                {
                    ++ArgCount;
                }
                else if (*C == 'e')
                {
                    // The end marker terminates the original decode block but
                    // has no encoded argument.
                }
                else
                {
                    Error = "invalid instruction argument specification";
                    return false;
                }
            }

            const size_t NextPC = PC + 1 + ArgCount;
            if (NextPC > Words.size())
            {
                std::ostringstream Message;
                Message << "instruction at program word " << PC << " is truncated";
                Error = Message.str();
                return false;
            }

            DecodedInstruction Instruction;
            Instruction.PC = PC;
            Instruction.Opcode = Opcode;
            Instruction.NextPC = NextPC;
            Instruction.Args.assign(Words.begin() + static_cast<std::ptrdiff_t>(PC + 1),
                                    Words.begin() + static_cast<std::ptrdiff_t>(NextPC));
            Instructions.push_back(std::move(Instruction));
            PC = NextPC;
        }
        return true;
    }

    std::string IntList(const std::vector<int>& Args)
    {
        std::ostringstream Out;
        for (size_t I = 0; I < Args.size(); ++I)
        {
            if (I != 0) Out << ", ";
            Out << Args[I];
        }
        return Out.str();
    }

    int Arg(const DecodedInstruction& Instruction, size_t Index)
    {
        return Index < Instruction.Args.size() ? Instruction.Args[Index] : 0;
    }

    void EmitPCAdvance(std::ostream& Output, const DecodedInstruction& Instruction)
    {
        Output << "            state.pc = " << Instruction.NextPC << ";\n"
               << "            break;\n";
    }

    void EmitUnknown(std::ostream& Output, const DecodedInstruction& Instruction)
    {
        Output << "            host.UnknownOpcode(0x" << std::hex << Instruction.Opcode
               << std::dec << ", " << Arg(Instruction, 0) << ", "
               << Arg(Instruction, 1) << ", " << Arg(Instruction, 2) << ");\n";
        EmitPCAdvance(Output, Instruction);
    }

    void EmitRefreshDirectionalFrame(std::ostream& Output)
    {
        Output << R"AS(
void RefreshDirectionalFrame(IScriptState &inout state)
{
    if (!state.hasDirectionalFrames)
    {
        state.frameIndex = state.baseFrameIndex;
        return;
    }
    int dir32 = ((state.currentDirection & 0xff) + 4) / 8;
    int clampedDir32 = dir32 >= 32 ? 0 : dir32;
    bool flip = clampedDir32 > 16;
    int directionFrame = flip ? 32 - clampedDir32 : clampedDir32;
    state.frameIndex = state.baseFrameIndex + directionFrame;
    state.flipState = flip;
}

uint NextRandom(IScriptState &inout state)
{
    state.randomSeed = state.randomSeed * 22695477 + 1;
    return (state.randomSeed >> 16) & 0x7fff;
}
)AS";
    }

    void EmitCallStackMethods(std::ostream& Output)
    {
        Output << R"AS(
    bool PushCall(int address)
    {
        if (callDepth >= 64) return false;
        switch (callDepth)
        {
)AS";
        for (int I = 0; I < 64; ++I)
            Output << "        case " << I << ": callStack" << I << " = address; break;\n";
        Output << R"AS(
        }
        ++callDepth;
        return true;
    }

    int PopCall()
    {
        if (callDepth <= 0) return -1;
        --callDepth;
        switch (callDepth)
        {
)AS";
        for (int I = 0; I < 64; ++I)
            Output << "        case " << I << ": return callStack" << I << ";\n";
        Output << R"AS(
        }
        return -1;
    }
)AS";
    }

    void EmitPrelude(std::ostream& Output)
    {
Output << R"AS(// Generated from StarCraft iscript.bin.
// This file is intentionally self-contained. Implement IScriptHost in the
// embedding engine to connect script effects to rendering and gameplay.

enum IScriptAnimation
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
    Anim_Enable = 27
}

interface IScriptHost
{
    void PlayFrame(int frame);
    void SetHorPos(int value);
    void SetVertPos(int value);
    void SetPos(int horizontal, int vertical);
    void CreateImageOverlay(int imageId, int horizontal, int vertical, bool above);
    void CreateImageOverlayLo(int imageId, int horizontal, int vertical, bool above);
    void CreateImageOverlaySpecialOffset(int imageId, bool above);
    void CreateSpriteOverlay(int spriteId, int horizontal, int vertical, bool above);
    void CreateSpriteOverlayLo(int spriteId, int horizontal, int vertical, bool above);
    void CreateGasOverlays(int imageId);
    void WarpOverlay(int frame, int modifier);
    void PlaySound(int soundId);
    void PlayRandomSound(int soundId, int count);
    void PlaySoundBetween(int soundId, int minSoundId, int maxSoundId);
    void Attack(int weaponId, int flags);
    void AttackMelee();
    void UseWeapon(int weaponId);
    void DoMissileDamage();
    void DoGroundDamage();
    void CastSpell();
    void OrderSignal(int signal);
    void OrderDone(int signal);
    void FlightState(int direction, int speed, int changedField);
    void SpawnFrame(int frame);
    void TemporaryGraphicState(bool removed);
    void UnknownOpcode(int opcode, int a, int b, int c);
}

class IScriptState
{
    int spriteId;
    int imageId;
    int frameIndex;
    int baseFrameIndex;
    int horPos;
    int vertPos;
    bool horPosSet;
    bool vertPosSet;
    bool flipState;
    int flSpeed;
    int flDirect;
    int orderSignal;
    int currentDirection;
    bool hasDirectionalFrames;
    bool followMainGraphic;
    int targetRangePixels;
    int targetArc;
    uint randomSeed;
    bool isWalking;
    bool isAttacking;
    bool isPowered;
    bool orderDone;
    bool liftOffState;
    bool hasWarpOverlay;
    int warpOverlayFrame;
    bool spawnFrameSet;
    int spawnFrame;
    bool noBreakCode;
    bool readyToRepeatAttack;
    int repeatAttackPC;
    int pc;
    int currentAnim;
    int waitRemaining;
    bool running;
    int callDepth;
)AS";
        for (int I = 0; I < 64; ++I)
            Output << "    int callStack" << I << ";\n";
        Output << R"AS(

    IScriptState()
    {
        Reset();
    }

    void Reset()
    {
        spriteId = -1;
        imageId = -1;
        frameIndex = 0;
        baseFrameIndex = 0;
        horPos = 0;
        vertPos = 0;
        horPosSet = false;
        vertPosSet = false;
        flipState = false;
        flSpeed = 8;
        flDirect = 0;
        orderSignal = 0;
        currentDirection = 0;
        hasDirectionalFrames = false;
        followMainGraphic = false;
        targetRangePixels = 0;
        targetArc = 0;
        randomSeed = 42;
        isWalking = false;
        isAttacking = false;
        isPowered = true;
        orderDone = false;
        liftOffState = false;
        hasWarpOverlay = false;
        warpOverlayFrame = 0;
        spawnFrameSet = false;
        spawnFrame = -1;
        noBreakCode = false;
        readyToRepeatAttack = false;
        repeatAttackPC = -1;
        pc = -1;
        currentAnim = -1;
        waitRemaining = 0;
        running = false;
        callDepth = 0;
    }
)AS";
        EmitCallStackMethods(Output);
        Output << R"AS(
}

)AS";
        EmitRefreshDirectionalFrame(Output);
    }

    void EmitEntryTable(std::ostream& Output, const IScriptProgram& Program)
    {
        Output << "int GetAnimationEntry(int scriptId, IScriptAnimation animationId)\n{\n"
               << "    switch (scriptId)\n    {\n";
        std::vector<int> ScriptIds;
        ScriptIds.reserve(Program.Scripts.size());
        for (const auto& Pair : Program.Scripts)
            ScriptIds.push_back(Pair.first);
        std::sort(ScriptIds.begin(), ScriptIds.end());

        for (int ScriptId : ScriptIds)
        {
            const auto& Script = Program.Scripts.at(ScriptId);
            Output << "    case " << ScriptId << ":\n"
                   << "        switch (animationId)\n        {\n";
            for (size_t Animation = 0; Animation < Script.AnimationPC.size(); ++Animation)
            {
                const char* AnimationName = GetAnimationName(Animation);
                Output << "        case ";
                if (AnimationName)
                    Output << AnimationName;
                else
                    Output << Animation;
                Output << ": return " << Script.AnimationPC[Animation] << ";\n";
            }
            Output << "        default: return 0;\n        }\n";
        }
        Output << "    default: return 0;\n    }\n}\n\n"
               << R"AS(void StartScript(int scriptId, IScriptAnimation animationId, IScriptState &inout state)
{
    state.currentAnim = animationId;
    state.pc = GetAnimationEntry(scriptId, animationId);
    state.running = state.pc >= 0;
    state.orderDone = false;
    state.hasWarpOverlay = false;
    state.warpOverlayFrame = 0;
    state.noBreakCode = false;
    state.readyToRepeatAttack = false;
    state.repeatAttackPC = -1;
    state.waitRemaining = 0;
    state.callDepth = 0;
}

)AS";
    }

    void EmitInstruction(std::ostream& Output, const DecodedInstruction& I)
    {
        const auto A = [&](size_t Index) { return Arg(I, Index); };
        Output << "        case " << I.PC << ": // 0x" << std::hex << I.Opcode
               << std::dec << " " << GetIScriptOpcodeName(I.Opcode)
               << "(" << IntList(I.Args) << ")\n        {\n";

        using namespace IScriptOpcodes;
        switch (I.Opcode)
        {
        case Opc_PlayFram:
        case Opc_PlayFramTile:
            Output << "            state.baseFrameIndex = " << A(0) << ";\n"
                   << "            RefreshDirectionalFrame(state);\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_SetHorPos:
            Output << "            state.horPos = " << A(0) << "; state.horPosSet = true;\n"
                   << "            host.SetHorPos(state.horPos);\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_SetVertPos:
            Output << "            state.vertPos = " << A(0) << "; state.vertPosSet = true;\n"
                   << "            host.SetVertPos(state.vertPos);\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_SetPos:
            Output << "            state.horPos = " << A(0) << "; state.vertPos = " << A(1)
                   << "; state.horPosSet = true; state.vertPosSet = true;\n"
                   << "            host.SetPos(state.horPos, state.vertPos);\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_Wait:
            Output << "            state.waitRemaining = " << A(0)
                   << " > 0 ? " << A(0) << " - 1 : 0;\n";
            EmitPCAdvance(Output, I);
            Output << "            return true;\n";
            break;
        case Opc_WaitRand:
            Output << "            int waitRange_" << I.PC << " = " << A(1) << " > " << A(0)
                   << " ? " << A(1) << " - " << A(0) << " + 1 : 1;\n"
                   << "            int waitFrames_" << I.PC << " = " << A(0)
                   << " + int((NextRandom(state) & 0xff) % waitRange_" << I.PC << ");\n"
                   << "            state.waitRemaining = waitFrames_" << I.PC
                   << " > 0 ? waitFrames_" << I.PC << " - 1 : 0;\n";
            EmitPCAdvance(Output, I);
            Output << "            return true;\n";
            break;
        case Opc_Goto:
            Output << "            state.pc = " << A(0) << ";\n            break;\n";
            break;
        case Opc_ImgOl:
        case Opc_ImgUl:
            Output << "            host.CreateImageOverlay(" << A(0) << ", " << A(1) << ", "
                   << A(2) << ", " << (I.Opcode == Opc_ImgOl ? "true" : "false") << ");\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_ImgOlOrig:
        case Opc_SwitchUl:
            Output << "            host.CreateImageOverlaySpecialOffset(" << A(0) << ", "
                   << (I.Opcode == Opc_ImgOlOrig ? "true" : "false") << ");\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_ImgOlUseLo:
        case Opc_ImgUlUseLo:
            Output << "            host.CreateImageOverlayLo(" << A(0) << ", " << A(1) << ", "
                   << A(2) << ", " << (I.Opcode == Opc_ImgOlUseLo ? "true" : "false") << ");\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_SprOl:
        case Opc_HighSprOl:
        case Opc_LowSprUl:
        case Opc_SprUlUseLo:
        case Opc_SprUl:
        case Opc_GrdSprOl:
            Output << "            host.CreateSpriteOverlay(" << A(0) << ", " << A(1) << ", "
                   << A(2) << ", "
                   << ((I.Opcode == Opc_LowSprUl || I.Opcode == Opc_SprUlUseLo ||
                        I.Opcode == Opc_SprUl) ? "false" : "true") << ");\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_SprOlUseLo:
            Output << "            host.CreateSpriteOverlayLo(" << A(0) << ", " << A(1)
                   << ", 0, true);\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_End:
        case Opc_IgnoreRest:
            Output << "            state.running = false;\n            return true;\n";
            break;
        case Opc_SetFlipState:
            Output << "            state.flipState = " << A(0) << " != 0;\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_PlaySnd:
            Output << "            host.PlaySound(" << A(0) << ");\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_PlaySndRand:
            Output << "            int soundCount_" << I.PC << " = " << A(0) << ";\n"
                   << "            int soundIndex_" << I.PC << " = soundCount_" << I.PC
                   << " > 0 ? int(NextRandom(state) % soundCount_" << I.PC << ") : -1;\n"
                   << "            int soundId_" << I.PC << " = -1;\n";
            for (size_t ArgIndex = 1; ArgIndex < I.Args.size(); ++ArgIndex)
            {
                Output << "            if (soundIndex_" << I.PC << " == " << (ArgIndex - 1)
                       << ") soundId_" << I.PC << " = " << A(ArgIndex) << ";\n";
            }
            Output << "            host.PlayRandomSound(soundId_" << I.PC << ", soundCount_"
                   << I.PC << ");\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_PlaySndBtwn:
            Output << "            int soundRange_" << I.PC << " = " << A(1) << " >= " << A(0)
                   << " ? " << A(1) << " - " << A(0) << " + 1 : 1;\n"
                   << "            int soundId_" << I.PC << " = " << A(0)
                   << " + int(NextRandom(state) % soundRange_" << I.PC << ");\n"
                   << "            host.PlaySoundBetween(soundId_" << I.PC << ", " << A(0)
                   << ", " << A(1) << ");\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_AttackMelee:
            Output << "            int meleeCount_" << I.PC << " = " << A(0) << ";\n"
                   << "            int meleeIndex_" << I.PC << " = meleeCount_" << I.PC
                   << " > 0 ? int(NextRandom(state) % meleeCount_" << I.PC << ") : -1;\n"
                   << "            int meleeSound_" << I.PC << " = -1;\n";
            for (size_t ArgIndex = 1; ArgIndex < I.Args.size(); ++ArgIndex)
            {
                Output << "            if (meleeIndex_" << I.PC << " == " << (ArgIndex - 1)
                       << ") meleeSound_" << I.PC << " = " << A(ArgIndex) << ";\n";
            }
            Output << "            host.AttackMelee();\n"
                   << "            host.PlayRandomSound(meleeSound_" << I.PC << ", meleeCount_"
                   << I.PC << ");\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_FollowMainGraphic:
            Output << "            state.followMainGraphic = true;\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_SetSpawnFrame:
            Output << "            state.spawnFrame = " << A(0) << "; state.spawnFrameSet = true;\n"
                   << "            host.SpawnFrame(state.spawnFrame);\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_SigOrder:
            Output << "            state.orderSignal |= " << A(0) << "; state.orderDone = false;\n"
                   << "            host.OrderSignal(" << A(0) << ");\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_AttackWith:
            Output << "            host.Attack(" << A(0) << ", 0);\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_Attack:
            Output << "            host.Attack(0, 0);\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_CastSpell:
            Output << "            host.CastSpell();\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_UseWeapon:
            Output << "            host.UseWeapon(" << A(0) << ");\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_DoMissileDmg:
            Output << "            host.DoMissileDamage();\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_DoGrdDamage:
            Output << "            host.DoGroundDamage();\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_AttkShiftProj:
            Output << "            host.Attack(" << A(0) << ", 1);\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_SetFlDirect:
            Output << "            state.flDirect = (" << A(0) << " * 8) & 0xff;\n"
                   << "            host.FlightState(state.flDirect, state.flSpeed, 1);\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_Call:
            Output << "            if (state.callDepth >= 64)\n            {\n"
                   << "                host.UnknownOpcode(0x35, " << A(0) << ", 0, 0);\n"
                   << "                state.running = false; return true;\n            }\n"
                   << "            state.PushCall(" << I.NextPC << ");\n"
                   << "            state.pc = " << A(0) << ";\n            break;\n";
            break;
        case Opc_Return:
            Output << "            if (state.callDepth <= 0) state.running = false;\n"
                   << "            else state.pc = state.PopCall();\n"
                   << "            break;\n";
            break;
        case Opc_CreateGasOverlays:
            Output << "            host.CreateGasOverlays(" << A(0) << ");\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_PwrUpCondJmp:
            Output << "            state.pc = state.isPowered ? " << A(0) << " : " << I.NextPC << ";\n"
                   << "            break;\n";
            break;
        case Opc_TrgtRangeCondJmp:
            Output << "            state.pc = state.targetRangePixels <= " << A(0) << " ? " << A(1)
                   << " : " << I.NextPC << ";\n            break;\n";
            break;
        case Opc_TrgtArcCondJmp:
            Output << "            int targetArc_" << I.PC << " = state.targetArc < 0 ? -state.targetArc : state.targetArc;\n"
                   << "            state.pc = (targetArc_" << I.PC << " >= " << A(0) << " && targetArc_"
                   << I.PC << " <= " << A(1) << ") ? " << A(2) << " : " << I.NextPC << ";\n"
                   << "            break;\n";
            break;
        case Opc_CurDirectCondJmp:
            Output << "            int currentDirection_" << I.PC << " = state.currentDirection & 0xff;\n"
                   << "            state.pc = (currentDirection_" << I.PC << " >= " << A(0)
                   << " && currentDirection_" << I.PC << " <= " << A(1) << ") ? " << A(2)
                   << " : " << I.NextPC << ";\n            break;\n";
            break;
        case Opc_RandCondJmp:
            Output << "            int randomRoll_" << I.PC
                   << " = int(NextRandom(state) & 0xff);\n"
                   << "            state.pc = randomRoll_" << I.PC << " <= " << A(0)
                   << " ? " << A(1) << " : " << I.NextPC << ";\n            break;\n";
            break;
        case Opc_ImgUlNextId:
            Output << "            host.CreateImageOverlay(state.imageId + 1, " << A(0) << ", "
                   << A(1) << ", false);\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_LiftOffCondJmp:
            Output << "            state.pc = state.liftOffState ? " << A(0) << " : " << I.NextPC << ";\n"
                   << "            break;\n";
            break;
        case Opc_WarpOverlay:
            Output << "            state.hasWarpOverlay = true; state.warpOverlayFrame = " << A(0)
                   << " & 0xff;\n"
                   << "            host.WarpOverlay(state.warpOverlayFrame, (" << A(0)
                   << " >> 8) & 0xff);\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_OrderDone:
            Output << "            state.orderSignal &= ~" << A(0) << ";\n"
                   << "            state.orderDone = state.orderSignal == 0;\n"
                   << "            host.OrderDone(" << A(0) << ");\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_Move:
            EmitPCAdvance(Output, I);
            break;
        case Opc_GotoRepeatAttk:
            Output << "            state.noBreakCode = false;\n"
                   << "            state.readyToRepeatAttack = true;\n"
                   << "            if (state.repeatAttackPC >= 0) state.pc = state.repeatAttackPC;\n"
                   << "            else state.pc = " << I.NextPC << ";\n";
            break;
        case Opc_EngFrame:
            Output << "            state.baseFrameIndex = " << A(0) << ";\n"
                   << "            RefreshDirectionalFrame(state);\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_EngSet:
            Output << "            state.baseFrameIndex = " << (A(0) * 17) << ";\n"
                   << "            RefreshDirectionalFrame(state);\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_NoBrkCodeStart:
            Output << "            state.noBreakCode = true;\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_NoBrkCodeEnd:
            Output << "            state.noBreakCode = false;\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_Unknown0C:
        case Opc_UflUnstable:
        case Opc_Unknown2D:
        case Opc_Unknown3E:
        case Opc_Unknown43:
            EmitUnknown(Output, I);
            break;
        case Opc_TurnCWise:
            Output << "            state.currentDirection = (state.currentDirection + "
                   << A(0) << " * 8) & 0xff;\n"
                   << "            state.flDirect = state.currentDirection;\n"
                   << "            RefreshDirectionalFrame(state);\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_TurnCCWise:
            Output << "            state.currentDirection = (state.currentDirection - "
                   << A(0) << " * 8) & 0xff;\n"
                   << "            state.flDirect = state.currentDirection;\n"
                   << "            RefreshDirectionalFrame(state);\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_Turn1CWise:
            Output << "            state.currentDirection = (state.currentDirection + 8) & 0xff;\n"
                   << "            state.flDirect = state.currentDirection;\n"
                   << "            RefreshDirectionalFrame(state);\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_TurnRand:
            Output << "            int turnRange_" << I.PC << " = " << A(0)
                   << " > 0 ? " << A(0) << " : 32;\n"
                   << "            int turnStep_" << I.PC
                   << " = int((NextRandom(state) & 0xff) % turnRange_" << I.PC << ") * 8;\n"
                   << "            state.currentDirection = (state.currentDirection + turnStep_"
                   << I.PC << ") & 0xff;\n"
                   << "            state.flDirect = state.currentDirection;\n"
                   << "            RefreshDirectionalFrame(state);\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_SetFlSpeed:
            Output << "            state.flSpeed = " << A(0) << ";\n"
                   << "            host.FlightState(state.flDirect, state.flSpeed, 2);\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_TmpRmGraphicStart:
            Output << "            host.TemporaryGraphicState(true);\n";
            EmitPCAdvance(Output, I);
            break;
        case Opc_TmpRmGraphicEnd:
            Output << "            host.TemporaryGraphicState(false);\n";
            EmitPCAdvance(Output, I);
            break;
        default:
            EmitUnknown(Output, I);
            break;
        }
        Output << "        }\n";
    }
}

bool ExportIScriptToAngelScript(const IScriptProgram& Program,
                               std::ostream& Output,
                               IScriptAngelScriptExportStats* Stats,
                               std::string* Error)
{
    std::vector<DecodedInstruction> Instructions;
    std::string LocalError;
    if (!DecodeProgram(Program, Instructions, LocalError))
    {
        if (Error) *Error = LocalError;
        return false;
    }
    if (Program.Scripts.empty())
    {
        if (Error) *Error = "IScript program contains no scripts";
        return false;
    }

    EmitPrelude(Output);
    EmitEntryTable(Output, Program);
    Output << "bool TickScript(IScriptState &inout state, IScriptHost@ host)\n{\n"
           << "    if (!state.running) return false;\n"
           << "    state.followMainGraphic = false;\n"
           << "    if (state.waitRemaining > 0)\n    {\n"
           << "        --state.waitRemaining;\n        return true;\n    }\n"
           << "    int iterations = 0;\n"
           << "    while (iterations++ < 96 && state.running)\n    {\n"
           << "        switch (state.pc)\n        {\n";
    for (const auto& Instruction : Instructions)
        EmitInstruction(Output, Instruction);
    Output << "        default:\n"
           << "            state.running = false;\n"
           << "            return false;\n"
           << "        }\n"
           << "    }\n"
           << "    return state.running;\n"
           << "}\n";

    if (!Output)
    {
        if (Error) *Error = "failed while writing AngelScript output";
        return false;
    }

    if (Stats)
    {
        Stats->ScriptCount = Program.Scripts.size();
        Stats->ProgramWordCount = Program.ProgramData.size();
        Stats->InstructionCount = Instructions.size();
        for (const auto& Pair : Program.Scripts)
            Stats->AnimationEntryCount += Pair.second.AnimationPC.size();
    }
    return true;
}

} // namespace Game
