#include "Game/IScriptVM.h"

#include "Core/Utils/Logger.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
#include <limits>
#include <cstdlib>
#include <unordered_map>

namespace Game
{

namespace
{
    uint16_t ReadU16(const std::vector<uint8_t>& Data, size_t Offset)
    {
        if (Offset + 1 >= Data.size()) return 0;
        return (uint16_t)(Data[Offset] | (Data[Offset + 1] << 8));
    }

    int16_t ReadS16(const std::vector<uint8_t>& Data, size_t Offset)
    {
        return (int16_t)ReadU16(Data, Offset);
    }

    uint32_t ReadU32(const std::vector<uint8_t>& Data, size_t Offset)
    {
        if (Offset + 3 >= Data.size()) return 0;
        return (uint32_t)Data[Offset] |
               ((uint32_t)Data[Offset + 1] << 8) |
               ((uint32_t)Data[Offset + 2] << 16) |
               ((uint32_t)Data[Offset + 3] << 24);
    }

    std::vector<uint8_t> ReadFileBytes(const std::string& Path)
    {
        std::ifstream F(Path, std::ios::binary);
        if (!F) return {};
        F.seekg(0, std::ios::end);
        std::streamoff Size = F.tellg();
        if (Size <= 0) return {};
        F.seekg(0, std::ios::beg);
        std::vector<uint8_t> Data((size_t)Size);
        F.read(reinterpret_cast<char*>(Data.data()), Size);
        return Data;
    }

    struct DataReader
    {
        const std::vector<uint8_t>& Data;
        size_t Pos = 0;

        bool Has(size_t Count) const { return Pos + Count <= Data.size(); }
        size_t Left() const { return Pos <= Data.size() ? Data.size() - Pos : 0; }
        void Seek(size_t NewPos) { Pos = NewPos; }
        void Skip(size_t Count) { Pos += Count; }
        uint8_t U8() { return Has(1) ? Data[Pos++] : 0; }
        int8_t S8() { return (int8_t)U8(); }
        uint16_t U16() { uint16_t V = ReadU16(Data, Pos); Pos += 2; return V; }
        int16_t S16() { int16_t V = ReadS16(Data, Pos); Pos += 2; return V; }
        uint32_t U32() { uint32_t V = ReadU32(Data, Pos); Pos += 4; return V; }
    };

    const std::array<const char*, IScriptOpcodes::kCount>& InstructionSpecs()
    {
        using namespace IScriptOpcodes;
        static const std::array<const char*, kCount> Specs =
        {
            "2", "2", "s1", "s1", "s1s1", "1", "11", "j",
            "211", "211", "2", "2", "", "211", "211", "2s1s1",
            "211", "211", "2", "211", "211", "21", "e", "1",
            "2", "v", "22", "", "v", "", "1b", "1",
            "1", "", "1", "1", "1", "1", "", "", "1",
            "1", "", "1", "1", "", "", "", "", "1",
            "", "", "1", "b", "", "2", "1", "b",
            "2b", "22b", "22b", "11", "", "b", "2", "1",
            "211", "", ""
        };
        return Specs;
    }

    bool DecodeInstructionArgs(DataReader& R, const char* Spec, std::vector<int>& OutArgs,
                               std::deque<std::pair<size_t, size_t>>& Branches,
                               std::unordered_map<size_t, size_t>& DecodeMap,
                               size_t& ContinueAtAddress,
                               bool& EndBlock)
    {
        ContinueAtAddress = std::numeric_limits<size_t>::max();
        EndBlock = false;

        for (const char* C = Spec; *C; ++C)
        {
            if (*C == 's')
            {
                ++C;
                if (*C == '1') OutArgs.push_back(R.S8());
                else if (*C == '2') OutArgs.push_back(R.S16());
            }
            else if (*C == '1') OutArgs.push_back(R.U8());
            else if (*C == '2') OutArgs.push_back(R.U16());
            else if (*C == 'v')
            {
                int N = R.U8();
                OutArgs.push_back(N);
                for (int I = 0; I < N; ++I)
                    OutArgs.push_back(R.U16());
            }
            else if (*C == 'j')
            {
                size_t JumpAddress = R.U16();
                auto It = DecodeMap.find(JumpAddress);
                if (It == DecodeMap.end())
                {
                    ContinueAtAddress = JumpAddress;
                    EndBlock = true;
                }
                else
                {
                    OutArgs.push_back((int)It->second);
                    EndBlock = true;
                }
            }
            else if (*C == 'b')
            {
                size_t BranchAddress = R.U16();
                Branches.emplace_back(BranchAddress, OutArgs.size());
                OutArgs.push_back(0);
            }
            else if (*C == 'e')
            {
                EndBlock = true;
            }
        }
        return true;
    }
}

bool IScriptDatabase::Load(const std::string& IScriptPath, const std::string& ImagesDatPath)
{
    mLoaded = false;
    mProgram = {};
    mImages.clear();

    if (!LoadImagesDat(ImagesDatPath))
        return false;
    if (!LoadIScriptBin(IScriptPath))
        return false;

    mLoaded = true;
    Core::Utils::Logger::Info("IScriptVM",
        "Loaded iscript database: scripts=" + std::to_string(mProgram.Scripts.size()) +
        " images=" + std::to_string(mImages.size()) +
        " program_words=" + std::to_string(mProgram.ProgramData.size()));
    return true;
}

bool IScriptDatabase::LoadImagesDat(const std::string& ImagesDatPath)
{
    const auto Data = ReadFileBytes(ImagesDatPath);
    constexpr size_t Count = 999;
    constexpr size_t ExpectedSize = Count * 38;
    if (Data.size() < ExpectedSize)
    {
        Core::Utils::Logger::Warning("IScriptVM", "images.dat missing or too small: " + ImagesDatPath);
        return false;
    }

    mImages.assign(Count, {});
    const size_t DirectionalOffset = Count * 4;
    const size_t HasIScriptOffset = Count * 4 + Count * 2;
    const size_t IScriptIDOffset = Count * 10;
    for (size_t I = 0; I < Count; ++I)
    {
        mImages[I].HasDirectionalFrames = Data[DirectionalOffset + I] != 0;
        mImages[I].HasIScriptAnimations = Data[HasIScriptOffset + I] != 0;
        mImages[I].IScriptID = (int)ReadU32(Data, IScriptIDOffset + I * 4);
    }
    return true;
}

bool IScriptDatabase::LoadIScriptBin(const std::string& IScriptPath)
{
    const auto Data = ReadFileBytes(IScriptPath);
    if (Data.size() < 8)
    {
        Core::Utils::Logger::Warning("IScriptVM", "iscript.bin missing or too small: " + IScriptPath);
        return false;
    }

    auto& Program = mProgram.ProgramData;
    Program.clear();
    Program.push_back(IScriptOpcodes::Opc_End);

    std::unordered_map<int, std::vector<int>> AnimationPCByScript;
    const auto& Specs = InstructionSpecs();

    auto DecodeScriptAt = [&](size_t ScriptAddress, int ScriptID)
    {
        if (ScriptAddress + 8 >= Data.size())
            return;

        DataReader ScriptR{Data, ScriptAddress};
        ScriptR.Skip(4); // "SCPE" signature.
        uint32_t HighestAnimation = ScriptR.U32();
        size_t AnimationCount = (HighestAnimation + 2) & ~(size_t)1;
        if (AnimationCount > 64)
            AnimationCount = 64;

        std::unordered_map<size_t, size_t> DecodeMap;
        std::deque<std::pair<size_t, size_t>> Branches;

        std::function<size_t(size_t)> DecodeAt = [&](size_t InitialAddress) -> size_t
        {
            if (InitialAddress == 0 || InitialAddress >= Data.size())
                return 0;
            auto Inserted = DecodeMap.emplace(InitialAddress, 0);
            if (!Inserted.second)
                return Inserted.first->second;

            const size_t InitialPC = Program.size();
            Inserted.first->second = InitialPC;
            DataReader R{Data, InitialAddress};
            bool Done = false;

            while (!Done && R.Pos < Data.size())
            {
                const size_t CurAddress = R.Pos;
                if (CurAddress != InitialAddress)
                {
                    auto Existing = DecodeMap.emplace(CurAddress, Program.size());
                    if (!Existing.second)
                    {
                        Program.push_back(IScriptOpcodes::Opc_Goto);
                        Program.push_back((int)Existing.first->second);
                        break;
                    }
                }

                const int Opcode = R.U8();
                if (Opcode < 0 || Opcode >= IScriptOpcodes::kCount)
                    break;

                const size_t OpcodePC = Program.size();
                Program.push_back(Opcode);
                std::vector<int> Args;
                size_t ContinueAt = std::numeric_limits<size_t>::max();
                bool EndBlock = false;
                const size_t BranchCountBefore = Branches.size();
                DecodeInstructionArgs(R, Specs[(size_t)Opcode], Args, Branches, DecodeMap, ContinueAt, EndBlock);

                if (ContinueAt != std::numeric_limits<size_t>::max())
                {
                    Program.resize(OpcodePC);
                    while (Branches.size() > BranchCountBefore)
                        Branches.pop_back();
                    R.Seek(ContinueAt);
                    continue;
                }

                Program.insert(Program.end(), Args.begin(), Args.end());
                for (size_t BranchIndex = BranchCountBefore; BranchIndex < Branches.size(); ++BranchIndex)
                    Branches[BranchIndex].second += OpcodePC + 1;
                Done = EndBlock;
            }
            return InitialPC;
        };

        auto& AnimPC = AnimationPCByScript[ScriptID];
        AnimPC.clear();
        AnimPC.reserve(AnimationCount);
        for (size_t I = 0; I < AnimationCount; ++I)
        {
            if (!ScriptR.Has(2))
            {
                AnimPC.push_back(0);
                continue;
            }
            size_t AnimAddress = ScriptR.U16();
            AnimPC.push_back(AnimAddress ? (int)DecodeAt(AnimAddress) : 0);
        }

        while (!Branches.empty())
        {
            auto [BranchAddress, ArgIndex] = Branches.front();
            Branches.pop_front();
            size_t BranchPC = DecodeAt(BranchAddress);
            if (ArgIndex < Program.size())
                Program[ArgIndex] = (int)BranchPC;
        }
    };

    DataReader ListR{Data, 0};
    size_t IdListOffset = ListR.U32();
    if (IdListOffset >= Data.size())
        return false;

    ListR.Seek(IdListOffset);
    while (ListR.Left() >= 4)
    {
        int ScriptID = ListR.S16();
        if (ScriptID == -1)
            break;
        size_t ScriptAddress = ListR.U16();
        DecodeScriptAt(ScriptAddress, ScriptID);
    }

    mProgram.Scripts.clear();
    for (auto& Pair : AnimationPCByScript)
    {
        IScriptProgram::Script Script;
        Script.Id = Pair.first;
        Script.AnimationPC = std::move(Pair.second);
        mProgram.Scripts.emplace(Script.Id, std::move(Script));
    }
    return !mProgram.Scripts.empty();
}

const IScriptProgram::Script* IScriptDatabase::FindScript(int ScriptID) const
{
    auto It = mProgram.Scripts.find(ScriptID);
    return It == mProgram.Scripts.end() ? nullptr : &It->second;
}

const IScriptImageInfo* IScriptDatabase::FindImageInfo(int ImageID) const
{
    if (ImageID < 0 || ImageID >= (int)mImages.size())
        return nullptr;
    return &mImages[(size_t)ImageID];
}

IScriptVM::IScriptVM()
{
    RegisterDefaultOpcodes();
}

void IScriptVM::LoadScript(int AnimationID, const std::vector<int>* ProgramData, int EntryPC)
{
    mProgramData = ProgramData;
    mSprite.PC = EntryPC;
    mCurrentAnim = AnimationID;
    mSprite.OrderDone = false;
    mWaitRemaining = 0;
    mEvents.clear();
    mCallStack.clear();
}

void IScriptVM::Reset()
{
    mSprite = SpriteContext();
    mProgramData = nullptr;
    mCallStack.clear();
    mWaitRemaining = 0;
    mCurrentAnim = -1;
    mEvents.clear();
}

bool IScriptVM::ExecuteFrame()
{
    if (!mProgramData)
        return false;
    if (mWaitRemaining > 0)
    {
        --mWaitRemaining;
        return true;
    }

    int Iterations = 0;
    constexpr int kMaxIterations = 96;
    while (Iterations++ < kMaxIterations &&
           mSprite.PC >= 0 &&
           mSprite.PC < (int)mProgramData->size())
    {
        int Opcode = (*mProgramData)[(size_t)mSprite.PC];
        bool Blocking = DispatchOpcode(Opcode);
        if (Blocking)
            break;
    }

    return mSprite.PC >= 0;
}

bool IScriptVM::DispatchOpcode(int Opcode)
{
    auto It = mOpcodeHandlers.find(Opcode);
    if (It != mOpcodeHandlers.end())
        return It->second(*this);
    ++mSprite.PC;
    return false;
}

int IScriptVM::ReadArg()
{
    if (!mProgramData || mSprite.PC + 1 >= (int)mProgramData->size())
        return 0;
    return (*mProgramData)[(size_t)++mSprite.PC];
}

int IScriptVM::PeekArg(int Offset) const
{
    if (!mProgramData || mSprite.PC + Offset >= (int)mProgramData->size())
        return 0;
    return (*mProgramData)[(size_t)(mSprite.PC + Offset)];
}

void IScriptVM::AdvanceArgs(int Count)
{
    mSprite.PC += Count + 1;
}

void IScriptVM::SkipVectorArgs()
{
    int Count = PeekArg(1);
    if (Count < 0) Count = 0;
    AdvanceArgs(Count + 1);
}

void IScriptVM::Emit(IScriptEventType Type, int A, int B, int C, int D)
{
    mEvents.push_back({Type, A, B, C, D});
}

std::vector<IScriptEvent> IScriptVM::ConsumeEvents()
{
    std::vector<IScriptEvent> Out = std::move(mEvents);
    mEvents.clear();
    return Out;
}

void IScriptVM::RefreshDirectionalFrame()
{
    if (!mSprite.HasDirectionalFrames)
    {
        mSprite.FrameIndex = mSprite.BaseFrameIndex;
        return;
    }

    const int Dir32 = ((mSprite.CurrentDirection & 0xff) + 4) / 8;
    const int ClampedDir32 = Dir32 >= 32 ? 0 : Dir32;
    const bool Flip = ClampedDir32 > 16;
    const int DirectionFrame = Flip ? 32 - ClampedDir32 : ClampedDir32;
    mSprite.FrameIndex = mSprite.BaseFrameIndex + DirectionFrame;
    mSprite.FlipState = Flip;
}

void IScriptVM::RegisterDefaultOpcodes()
{
    using namespace IScriptOpcodes;

    mOpcodeHandlers[Opc_PlayFram] = [](IScriptVM& VM) {
        VM.mSprite.BaseFrameIndex = VM.PeekArg();
        VM.RefreshDirectionalFrame();
        VM.AdvanceArgs(1);
        return false;
    };
    mOpcodeHandlers[Opc_PlayFramTile] = mOpcodeHandlers[Opc_PlayFram];
    mOpcodeHandlers[Opc_SetHorPos] = [](IScriptVM& VM) {
        VM.mSprite.HorPos = VM.PeekArg();
        VM.mSprite.HorPosSet = true;
        VM.AdvanceArgs(1);
        return false;
    };
    mOpcodeHandlers[Opc_SetVertPos] = [](IScriptVM& VM) {
        VM.mSprite.VertPos = VM.PeekArg();
        VM.mSprite.VertPosSet = true;
        VM.AdvanceArgs(1);
        return false;
    };
    mOpcodeHandlers[Opc_SetPos] = [](IScriptVM& VM) {
        VM.mSprite.HorPos = VM.PeekArg(1);
        VM.mSprite.VertPos = VM.PeekArg(2);
        VM.mSprite.HorPosSet = true;
        VM.mSprite.VertPosSet = true;
        VM.AdvanceArgs(2);
        return false;
    };
    mOpcodeHandlers[Opc_Wait] = [](IScriptVM& VM) {
        VM.mWaitRemaining = VM.PeekArg();
        VM.AdvanceArgs(1);
        return true;
    };
    mOpcodeHandlers[Opc_WaitRand] = [](IScriptVM& VM) {
        int Min = VM.PeekArg(1);
        int Max = VM.PeekArg(2);
        int Range = Max > Min ? Max - Min + 1 : 1;
        VM.mWaitRemaining = Min + ((VM.mSprite.PC * 17 + 13) % Range);
        VM.AdvanceArgs(2);
        return true;
    };
    mOpcodeHandlers[Opc_Goto] = [](IScriptVM& VM) { VM.mSprite.PC = VM.PeekArg(); return false; };
    mOpcodeHandlers[Opc_End] = [](IScriptVM& VM) { VM.mSprite.PC = -1; return true; };
    mOpcodeHandlers[Opc_Call] = [](IScriptVM& VM) {
        int Target = VM.PeekArg();
        VM.mCallStack.push_back(VM.mSprite.PC + 2);
        VM.mSprite.PC = Target;
        return false;
    };
    mOpcodeHandlers[Opc_Return] = [](IScriptVM& VM) {
        if (VM.mCallStack.empty()) VM.mSprite.PC = -1;
        else { VM.mSprite.PC = VM.mCallStack.back(); VM.mCallStack.pop_back(); }
        return false;
    };
    mOpcodeHandlers[Opc_SetFlipState] = [](IScriptVM& VM) { VM.mSprite.FlipState = VM.PeekArg() != 0; VM.AdvanceArgs(1); return false; };
    mOpcodeHandlers[Opc_SetSpawnFrame] = [](IScriptVM& VM) {
        VM.mSprite.SpawnFrame = VM.PeekArg();
        VM.mSprite.SpawnFrameSet = true;
        VM.Emit(IScriptEventType::SpawnFrame, VM.mSprite.SpawnFrame);
        VM.AdvanceArgs(1);
        return false;
    };
    mOpcodeHandlers[Opc_SigOrder] = [](IScriptVM& VM) {
        const int Signal = VM.PeekArg();
        VM.mSprite.OrderSignal |= Signal;
        VM.mSprite.OrderDone = false;
        VM.Emit(IScriptEventType::OrderSignal, Signal);
        VM.AdvanceArgs(1);
        return false;
    };
    mOpcodeHandlers[Opc_OrderDone] = [](IScriptVM& VM) {
        const int Signal = VM.PeekArg();
        VM.mSprite.OrderSignal &= ~Signal;
        VM.mSprite.OrderDone = VM.mSprite.OrderSignal == 0;
        VM.Emit(IScriptEventType::OrderDone, Signal);
        VM.AdvanceArgs(1);
        return false;
    };
    mOpcodeHandlers[Opc_IgnoreRest] = [](IScriptVM& VM) { VM.mSprite.PC = -1; return true; };

    auto Skip0 = [](IScriptVM& VM) { VM.AdvanceArgs(0); return false; };
    auto Skip1 = [](IScriptVM& VM) { VM.AdvanceArgs(1); return false; };
    auto Skip2 = [](IScriptVM& VM) { VM.AdvanceArgs(2); return false; };
    auto Skip3 = [](IScriptVM& VM) { VM.AdvanceArgs(3); return false; };
    auto SkipVector = [](IScriptVM& VM) { VM.SkipVectorArgs(); return false; };
    auto Branch1 = [](IScriptVM& VM) { VM.AdvanceArgs(2); return false; };

    for (int Op : {Opc_Unknown0C, Opc_FollowMainGraphic, Opc_Turn1CWise,
                   Opc_GotoRepeatAttk, Opc_Unknown2D,
                   Opc_NoBrkCodeStart, Opc_NoBrkCodeEnd, Opc_IgnoreRest,
                   Opc_TmpRmGraphicStart, Opc_TmpRmGraphicEnd, Opc_Return,
                   Opc_Unknown3E, Opc_Unknown43})
        if (!mOpcodeHandlers.count(Op)) mOpcodeHandlers[Op] = Skip0;

    for (int Op : {Opc_SetHorPos, Opc_SetVertPos, Opc_Wait, Opc_SetFlipState, Opc_TurnCCWise,
                   Opc_TurnCWise, Opc_TurnRand, Opc_SetSpawnFrame, Opc_SigOrder,
                   Opc_Move, Opc_EngFrame, Opc_EngSet, Opc_AttkShiftProj,
                   Opc_OrderDone, Opc_UflUnstable})
        if (!mOpcodeHandlers.count(Op)) mOpcodeHandlers[Op] = Skip1;

    for (int Op : {Opc_PlayFram, Opc_PlayFramTile, Opc_SetPos, Opc_WaitRand,
                   Opc_SprOlUseLo})
        if (!mOpcodeHandlers.count(Op)) mOpcodeHandlers[Op] = Skip2;

    for (int Op : {Opc_SprUlUseLo})
        if (!mOpcodeHandlers.count(Op)) mOpcodeHandlers[Op] = Skip3;

    auto ImageOverlay3Above = [](IScriptVM& VM) {
        VM.Emit(IScriptEventType::CreateImageOverlay, VM.PeekArg(1), VM.PeekArg(2), VM.PeekArg(3), 1);
        VM.AdvanceArgs(3);
        return false;
    };
    auto ImageOverlay3Below = [](IScriptVM& VM) {
        VM.Emit(IScriptEventType::CreateImageOverlay, VM.PeekArg(1), VM.PeekArg(2), VM.PeekArg(3), 0);
        VM.AdvanceArgs(3);
        return false;
    };
    auto SpriteOverlay3Above = [](IScriptVM& VM) {
        VM.Emit(IScriptEventType::CreateSpriteOverlay, VM.PeekArg(1), VM.PeekArg(2), VM.PeekArg(3), 1);
        VM.AdvanceArgs(3);
        return false;
    };
    auto SpriteOverlay3Below = [](IScriptVM& VM) {
        VM.Emit(IScriptEventType::CreateSpriteOverlay, VM.PeekArg(1), VM.PeekArg(2), VM.PeekArg(3), 0);
        VM.AdvanceArgs(3);
        return false;
    };
    mOpcodeHandlers[Opc_ImgOl] = ImageOverlay3Above;
    mOpcodeHandlers[Opc_ImgUl] = ImageOverlay3Below;
    mOpcodeHandlers[Opc_ImgOlUseLo] = ImageOverlay3Above;
    mOpcodeHandlers[Opc_ImgUlUseLo] = ImageOverlay3Below;
    mOpcodeHandlers[Opc_HighSprOl] = SpriteOverlay3Above;
    mOpcodeHandlers[Opc_LowSprUl] = SpriteOverlay3Below;
    mOpcodeHandlers[Opc_SprUlUseLo] = SpriteOverlay3Below;
    mOpcodeHandlers[Opc_SprUl] = SpriteOverlay3Below;
    mOpcodeHandlers[Opc_SprOlUseLo] = [](IScriptVM& VM) {
        VM.Emit(IScriptEventType::CreateSpriteOverlay, VM.PeekArg(1), 0, 0, 1);
        VM.AdvanceArgs(2);
        return false;
    };
    mOpcodeHandlers[Opc_GrdSprOl] = SpriteOverlay3Above;
    mOpcodeHandlers[Opc_ImgOlOrig] = [](IScriptVM& VM) {
        VM.Emit(IScriptEventType::CreateImageOverlaySpecialOffset,
                VM.PeekArg(1), 0, 0, 1);
        VM.AdvanceArgs(1);
        return false;
    };
    mOpcodeHandlers[Opc_SwitchUl] = [](IScriptVM& VM) {
        VM.Emit(IScriptEventType::CreateImageOverlaySpecialOffset,
                VM.PeekArg(1), 0, 0, 0);
        VM.AdvanceArgs(1);
        return false;
    };
    mOpcodeHandlers[Opc_SprOl] = [](IScriptVM& VM) {
        VM.Emit(IScriptEventType::CreateSpriteOverlay, VM.PeekArg(1), VM.PeekArg(2), VM.PeekArg(3), 1);
        VM.AdvanceArgs(3);
        return false;
    };
    mOpcodeHandlers[Opc_PlaySnd] = [](IScriptVM& VM) {
        VM.Emit(IScriptEventType::PlaySound, VM.PeekArg(1));
        VM.AdvanceArgs(1);
        return false;
    };
    mOpcodeHandlers[Opc_PlaySndRand] = [](IScriptVM& VM) {
        int Count = VM.PeekArg(1);
        int SoundID = Count > 0 ? VM.PeekArg(2) : -1;
        VM.Emit(IScriptEventType::PlayRandomSound, SoundID, Count);
        VM.SkipVectorArgs();
        return false;
    };
    mOpcodeHandlers[Opc_PlaySndBtwn] = [](IScriptVM& VM) {
        int MinID = VM.PeekArg(1);
        int MaxID = VM.PeekArg(2);
        int Range = MaxID >= MinID ? MaxID - MinID + 1 : 1;
        int SoundID = MinID + ((VM.mSprite.PC * 17 + 5) % Range);
        VM.Emit(IScriptEventType::PlaySoundBetween, SoundID, MinID, MaxID);
        VM.AdvanceArgs(2);
        return false;
    };
    mOpcodeHandlers[Opc_AttackMelee] = [](IScriptVM& VM) {
        VM.Emit(IScriptEventType::AttackMelee, VM.PeekArg(1), VM.PeekArg(2));
        VM.SkipVectorArgs();
        return false;
    };
    mOpcodeHandlers[Opc_Attack] = [](IScriptVM& VM) { VM.Emit(IScriptEventType::Attack); VM.AdvanceArgs(0); return false; };
    mOpcodeHandlers[Opc_AttackWith] = [](IScriptVM& VM) { VM.Emit(IScriptEventType::Attack, VM.PeekArg(1)); VM.AdvanceArgs(1); return false; };
    mOpcodeHandlers[Opc_UseWeapon] = [](IScriptVM& VM) { VM.Emit(IScriptEventType::UseWeapon, VM.PeekArg(1)); VM.AdvanceArgs(1); return false; };
    mOpcodeHandlers[Opc_DoMissileDmg] = [](IScriptVM& VM) { VM.Emit(IScriptEventType::DoMissileDamage); VM.AdvanceArgs(0); return false; };
    mOpcodeHandlers[Opc_DoGrdDamage] = [](IScriptVM& VM) { VM.Emit(IScriptEventType::DoGroundDamage); VM.AdvanceArgs(0); return false; };
    mOpcodeHandlers[Opc_CastSpell] = [](IScriptVM& VM) { VM.Emit(IScriptEventType::CastSpell); VM.AdvanceArgs(0); return false; };
    mOpcodeHandlers[Opc_SetFlDirect] = [](IScriptVM& VM) {
        VM.mSprite.FlDirect = (VM.PeekArg(1) * 8) & 0xFF;
        VM.Emit(IScriptEventType::FlightState, VM.mSprite.FlDirect, VM.mSprite.FlSpeed, 1);
        VM.AdvanceArgs(1);
        return false;
    };
    mOpcodeHandlers[Opc_SetFlSpeed] = [](IScriptVM& VM) {
        VM.mSprite.FlSpeed = VM.PeekArg(1);
        VM.Emit(IScriptEventType::FlightState, VM.mSprite.FlDirect, VM.mSprite.FlSpeed, 2);
        VM.AdvanceArgs(1);
        return false;
    };
    mOpcodeHandlers[Opc_CreateGasOverlays] = [](IScriptVM& VM) {
        VM.Emit(IScriptEventType::CreateGasOverlays, VM.PeekArg(1));
        VM.AdvanceArgs(1);
        return false;
    };
    mOpcodeHandlers[Opc_WarpOverlay] = [](IScriptVM& VM) {
        const int PackedModifier = VM.PeekArg(1);
        VM.mSprite.HasWarpOverlay = true;
        VM.Emit(IScriptEventType::WarpOverlay,
                PackedModifier & 0xff,
                (PackedModifier >> 8) & 0xff);
        VM.AdvanceArgs(1);
        return false;
    };
    mOpcodeHandlers[Opc_ImgUlNextId] = [](IScriptVM& VM) {
        VM.Emit(IScriptEventType::CreateImageOverlay,
                VM.mSprite.ImageID + 1,
                VM.PeekArg(1),
                VM.PeekArg(2),
                0);
        VM.AdvanceArgs(2);
        return false;
    };
    mOpcodeHandlers[Opc_TmpRmGraphicStart] = [](IScriptVM& VM) {
        VM.Emit(IScriptEventType::TemporaryGraphicState, 1);
        VM.AdvanceArgs(0);
        return false;
    };
    mOpcodeHandlers[Opc_TmpRmGraphicEnd] = [](IScriptVM& VM) {
        VM.Emit(IScriptEventType::TemporaryGraphicState, 0);
        VM.AdvanceArgs(0);
        return false;
    };
    mOpcodeHandlers[Opc_RandCondJmp] = [](IScriptVM& VM) {
        int Chance = VM.PeekArg(1);
        int Target = VM.PeekArg(2);
        VM.mSprite.RandomSeed = VM.mSprite.RandomSeed * 1103515245 + 12345;
        int Roll = (VM.mSprite.RandomSeed >> 16) & 0xff;
        VM.mSprite.PC = Roll < Chance ? Target : VM.mSprite.PC + 3;
        return false;
    };
    mOpcodeHandlers[Opc_PwrUpCondJmp] = [](IScriptVM& VM) {
        int Target = VM.PeekArg(1);
        VM.mSprite.PC = VM.mSprite.IsPowered ? Target : VM.mSprite.PC + 2;
        return false;
    };
    mOpcodeHandlers[Opc_TrgtRangeCondJmp] = [](IScriptVM& VM) {
        int Range = VM.PeekArg(1);
        int Target = VM.PeekArg(2);
        VM.mSprite.PC = VM.mSprite.TargetRangePixels <= Range ? Target : VM.mSprite.PC + 3;
        return false;
    };
    mOpcodeHandlers[Opc_TrgtArcCondJmp] = [](IScriptVM& VM) {
        int MinArc = VM.PeekArg(1);
        int MaxArc = VM.PeekArg(2);
        int Target = VM.PeekArg(3);
        int Arc = std::abs(VM.mSprite.TargetArc);
        VM.mSprite.PC = (Arc >= MinArc && Arc <= MaxArc) ? Target : VM.mSprite.PC + 4;
        return false;
    };
    mOpcodeHandlers[Opc_CurDirectCondJmp] = [](IScriptVM& VM) {
        int MinDir = VM.PeekArg(1);
        int MaxDir = VM.PeekArg(2);
        int Target = VM.PeekArg(3);
        int Dir = VM.mSprite.CurrentDirection & 0xff;
        VM.mSprite.PC = (Dir >= MinDir && Dir <= MaxDir) ? Target : VM.mSprite.PC + 4;
        return false;
    };
    mOpcodeHandlers[Opc_LiftOffCondJmp] = [](IScriptVM& VM) {
        int Target = VM.PeekArg();
        VM.mSprite.PC = VM.mSprite.LiftOffState ? Target : VM.mSprite.PC + 2;
        return false;
    };
}

} // namespace Game
