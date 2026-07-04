#ifdef _MSC_VER
#pragma warning(push, 0)
#pragma warning(disable: 4701)
#endif
#define STB_VORBIS_HEADER_ONLY
#include "third_party/stb_vorbis.c"
#define MINIAUDIO_IMPLEMENTATION
#include "third_party/miniaudio/miniaudio.h"
#undef STB_VORBIS_HEADER_ONLY
#include "third_party/stb_vorbis.c"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "Core/Audio/AudioSystem.h"
#include "Core/Utils/Logger.h"

#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <chrono>

namespace Core::Audio
{

// ── WAV generation helpers ─────────────────────────────────

#pragma pack(push, 1)
struct WavHeader
{
    char     RIFF[4] = {'R', 'I', 'F', 'F'};
    uint32_t FileSize = 0;
    char     WAVE[4] = {'W', 'A', 'V', 'E'};
    char     FmtID[4] = {'f', 'm', 't', ' '};
    uint32_t FmtSize = 16;
    uint16_t AudioFormat = 1;
    uint16_t NumChannels = 1;
    uint32_t SampleRate = 22050;
    uint32_t ByteRate = 0;
    uint16_t BlockAlign = 0;
    uint16_t BitsPerSample = 16;
    char     DataID[4] = {'d', 'a', 't', 'a'};
    uint32_t DataSize = 0;
};
#pragma pack(pop)

static constexpr int kSampleRate = 22050;

static std::vector<uint8_t> GenerateWav(const std::vector<int16_t>& Samples)
{
    WavHeader H;
    H.ByteRate = kSampleRate * 2;
    H.BlockAlign = 2;
    H.DataSize = (uint32_t)(Samples.size() * sizeof(int16_t));
    H.FileSize = sizeof(WavHeader) - 8 + H.DataSize;

    std::vector<uint8_t> Result(sizeof(WavHeader) + H.DataSize);
    std::memcpy(Result.data(), &H, sizeof(WavHeader));
    std::memcpy(Result.data() + sizeof(WavHeader), Samples.data(), H.DataSize);
    return Result;
}

static std::vector<int16_t> GenSineSweep(float DurationSec, float FreqStart, float FreqEnd, float Volume = 0.4f)
{
    int N = (int)(DurationSec * kSampleRate);
    std::vector<int16_t> S(N);
    for (int I = 0; I < N; I++)
    {
        float Frac = (float)I / N;
        float Freq = FreqStart + (FreqEnd - FreqStart) * Frac;
        float Env = 1.0f - Frac;
        float Val = sinf(2.0f * 3.14159f * Freq * (float)I / kSampleRate) * Volume * Env;
        S[I] = (int16_t)(Val * 32767.0f);
    }
    return S;
}

static std::vector<int16_t> GenNoise(float DurationSec, float Volume = 0.3f)
{
    int N = (int)(DurationSec * kSampleRate);
    std::vector<int16_t> S(N);
    for (int I = 0; I < N; I++)
    {
        float Env = 1.0f - (float)I / N;
        float Val = ((float)(std::rand() % 20001 - 10000) / 10000.0f) * Volume * Env;
        S[I] = (int16_t)(Val * 32767.0f);
    }
    return S;
}

static std::vector<int16_t> GenTone(float DurationSec, float Freq, float Volume = 0.3f)
{
    int N = (int)(DurationSec * kSampleRate);
    std::vector<int16_t> S(N);
    for (int I = 0; I < N; I++)
    {
        float Env = 1.0f - (float)I / N;
        float Val = sinf(2.0f * 3.14159f * Freq * (float)I / kSampleRate) * Volume * Env;
        S[I] = (int16_t)(Val * 32767.0f);
    }
    return S;
}

// ── AudioSystem::Impl ───────────────────────────────────────

struct AudioSystem::Impl
{
    ma_engine Engine;
    bool EngineInit = false;

    // Pre-allocated sound objects for the 8 channels
    ma_sound ChannelSounds[kMaxChannels];
    bool ChannelSoundInit[kMaxChannels] = {};
    ma_audio_buffer ChannelBuffers[kMaxChannels];
    bool ChannelBufferInit[kMaxChannels] = {};

    // Builtin enum sounds (backward compat)
    struct SoundEntry
    {
        std::vector<uint8_t> Wav;
        bool Loaded = false;
    };
    SoundEntry BuiltinSounds[(int)SoundEffect::Count];
};

AudioSystem::AudioSystem() : mImpl(std::make_unique<Impl>()) {}
AudioSystem::~AudioSystem() { Shutdown(); }

// ── Init / Shutdown ─────────────────────────────────────────

bool AudioSystem::Init()
{
    ma_engine_config Config = ma_engine_config_init();
    Config.listenerCount = 1;
    if (ma_engine_init(&Config, &mImpl->Engine) != MA_SUCCESS)
    {
        Core::Utils::Logger::Warning("AudioSystem", "No audio device available");
        return false;
    }
    mImpl->EngineInit = true;

    // Set engine volume to 100%
    ma_engine_set_volume(&mImpl->Engine, 1.0f);

    InitBuiltinSounds();

    mInitialized = true;
    Core::Utils::Logger::Info("AudioSystem", "Initialized (8 spatial channels)");
    return true;
}

void AudioSystem::InitBuiltinSounds()
{
    auto& S = mImpl->BuiltinSounds;
    S[(int)SoundEffect::WeaponFire].Wav = GenerateWav(GenSineSweep(0.08f, 1200, 400, 0.25f));
    S[(int)SoundEffect::WeaponFire].Loaded = true;

    S[(int)SoundEffect::Explosion].Wav = GenerateWav(GenNoise(0.3f, 0.3f));
    S[(int)SoundEffect::Explosion].Loaded = true;

    {
        auto T1 = GenTone(0.15f, 400, 0.25f);
        auto T2 = GenTone(0.15f, 600, 0.25f);
        T1.insert(T1.end(), T2.begin(), T2.end());
        S[(int)SoundEffect::BuildingComplete].Wav = GenerateWav(T1);
        S[(int)SoundEffect::BuildingComplete].Loaded = true;
    }

    S[(int)SoundEffect::UIClick].Wav = GenerateWav(GenTone(0.04f, 1000, 0.15f));
    S[(int)SoundEffect::UIClick].Loaded = true;

    // Unit selection acknowledgement
    {
        auto T1 = GenTone(0.05f, 800, 0.2f);
        auto T2 = GenTone(0.05f, 1000, 0.15f);
        T1.insert(T1.end(), T2.begin(), T2.end());
        S[(int)SoundEffect::UnitSelected].Wav = GenerateWav(T1);
        S[(int)SoundEffect::UnitSelected].Loaded = true;
    }

    // Move acknowledgement
    {
        auto T = GenTone(0.06f, 500, 0.15f);
        S[(int)SoundEffect::UnitMoveAck].Wav = GenerateWav(T);
        S[(int)SoundEffect::UnitMoveAck].Loaded = true;
    }

    // Attack acknowledgement
    {
        auto T = GenTone(0.08f, 700, 0.2f);
        S[(int)SoundEffect::UnitAttackAck].Wav = GenerateWav(T);
        S[(int)SoundEffect::UnitAttackAck].Loaded = true;
    }

    // Unit death
    {
        auto T1 = GenTone(0.15f, 300, 0.3f);
        auto T2 = GenTone(0.15f, 100, 0.2f);
        T1.insert(T1.end(), T2.begin(), T2.end());
        S[(int)SoundEffect::UnitDeath].Wav = GenerateWav(T1);
        S[(int)SoundEffect::UnitDeath].Loaded = true;
    }

    // Nuke launch
    {
        auto T1 = GenNoise(0.5f, 0.5f);
        auto T2 = GenNoise(0.3f, 0.3f);
        T1.insert(T1.end(), T2.begin(), T2.end());
        S[(int)SoundEffect::NukeLaunch].Wav = GenerateWav(T1);
        S[(int)SoundEffect::NukeLaunch].Loaded = true;
    }

    // Spell cast
    {
        auto T1 = GenTone(0.1f, 1200, 0.3f);
        auto T2 = GenTone(0.1f, 800, 0.25f);
        T1.insert(T1.end(), T2.begin(), T2.end());
        S[(int)SoundEffect::SpellCast].Wav = GenerateWav(T1);
        S[(int)SoundEffect::SpellCast].Loaded = true;
    }

    // Register as sound type IDs for the enum-based sounds
    SoundType WpnType = {(uint32_t)SoundEffect::WeaponFire, 0, 0, 0, 127};
    SoundType ExpType = {(uint32_t)SoundEffect::Explosion, 0, 0, 0, 127};
    SetSoundType((int)SoundEffect::WeaponFire, WpnType);
    SetSoundType((int)SoundEffect::Explosion, ExpType);
}

void AudioSystem::Shutdown()
{
    // Uninitialize all ma_sound objects first
    for (int i = 0; i < kMaxChannels; ++i)
    {
        if (mImpl->ChannelSoundInit[i])
        {
            ma_sound_uninit(&mImpl->ChannelSounds[i]);
            mImpl->ChannelSoundInit[i] = false;
        }
        if (mImpl->ChannelBufferInit[i])
        {
            ma_audio_buffer_uninit(&mImpl->ChannelBuffers[i]);
            mImpl->ChannelBufferInit[i] = false;
        }
        mChannels[i].Active = false;
    }

    if (mImpl->EngineInit)
        ma_engine_uninit(&mImpl->Engine);
    mImpl->EngineInit = false;
    mInitialized = false;
}

// ── Simple enum play (backward compat) ──────────────────────

void AudioSystem::Play(SoundEffect Effect)
{
    if (!mInitialized) return;
    int Idx = (int)Effect;
    if (Idx < 0 || Idx >= (int)SoundEffect::Count) return;
    if (!mImpl->BuiltinSounds[Idx].Loaded) return;

    int ch = FindFreeChannel(0, 0);
    if (ch < 0) return;

    auto& Data = mImpl->BuiltinSounds[Idx].Wav;
    auto& Snd = mImpl->ChannelSounds[ch];

    if (mImpl->ChannelSoundInit[ch])
    {
        ma_sound_uninit(&Snd);
        mImpl->ChannelSoundInit[ch] = false;
    }
    if (mImpl->ChannelBufferInit[ch])
    {
        ma_audio_buffer_uninit(&mImpl->ChannelBuffers[ch]);
        mImpl->ChannelBufferInit[ch] = false;
    }

    // Create audio buffer from WAV data
    ma_audio_buffer_config BufferConfig = ma_audio_buffer_config_init(
        ma_format_s16, 1, Data.size() / 2, Data.data(), nullptr);
    auto& Buffer = mImpl->ChannelBuffers[ch];
    if (ma_audio_buffer_init(&BufferConfig, &Buffer) != MA_SUCCESS)
        return;
    mImpl->ChannelBufferInit[ch] = true;

    ma_result Result = ma_sound_init_from_data_source(&mImpl->Engine, &Buffer,
        MA_SOUND_FLAG_NO_PITCH | MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &Snd);
    if (Result != MA_SUCCESS)
    {
        ma_audio_buffer_uninit(&Buffer);
        mImpl->ChannelBufferInit[ch] = false;
        return;
    }

    ma_sound_set_volume(&Snd, (float)mGlobalVolume / 100.0f);
    ma_sound_start(&Snd);
    mImpl->ChannelSoundInit[ch] = true;

    mChannels[ch].Active = true;
    mChannels[ch].Priority = 0;
    mChannels[ch].Flags = 0;
    mChannels[ch].SoundID = Idx;
}

// ── Full positional play ───────────────────────────────────

int AudioSystem::PlaySound(int SoundID, float SourceX, float SourceY,
                            int Priority, int UnitType, SoundFlags Flags)
{
    if (!mInitialized) return -1;

    // 80ms per-sound cooldown
    if (SoundID >= 0 && SoundID < (int)mLastPlayed.size())
    {
        auto Now = std::chrono::steady_clock::now();
        auto Elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            Now - mLastPlayed[SoundID]).count();
        if (Elapsed < 80) return -1;
        mLastPlayed[SoundID] = Now;
    }

    uint8_t F = static_cast<uint8_t>(Flags);

    // Sound type limiting
    if (CheckSoundLimits(SoundID, UnitType, F))
        return -1;

    // Channel allocation with priority preemption
    int ch = FindFreeChannel(Priority, F);
    if (ch < 0) return -1;

    // Load WAV data
    const std::vector<uint8_t>* WavData = nullptr;

    // Check builtin sounds first
    if (SoundID >= 0 && SoundID < (int)SoundEffect::Count &&
        mImpl->BuiltinSounds[SoundID].Loaded)
    {
        WavData = &mImpl->BuiltinSounds[SoundID].Wav;
    }
    // Check WAV cache (external loaded sounds)
    else if (SoundID >= 0 && SoundID < (int)mWavCache.size() &&
             !mWavCache[SoundID].empty())
    {
        WavData = &mWavCache[SoundID];
    }

    if (!WavData || WavData->empty()) return -1;

    // Distance attenuation
    float Atten = CalculateAttenuation(SourceX, SourceY);
    float Volume = (float)mGlobalVolume / 100.0f;

    // Apply min_volume from sound type
    if (SoundID >= 0 && SoundID < (int)mSoundTypes.size())
    {
        int MinVol = mSoundTypes[SoundID].MinVolume;
        float MinVolF = (float)MinVol / 127.0f;
        float DistVol = Atten * Volume;
        if (DistVol < MinVolF) DistVol = MinVolF;
        Volume = DistVol;
    }
    else
    {
        Volume = Atten * Volume;
    }

    constexpr float kMinVolume = 0.08f; // ~10/127
    if (Volume < kMinVolume) return -1;

    // Initialize ma_sound for this channel
    auto& Snd = mImpl->ChannelSounds[ch];
    if (mImpl->ChannelSoundInit[ch])
    {
        ma_sound_uninit(&Snd);
        mImpl->ChannelSoundInit[ch] = false;
    }
    if (mImpl->ChannelBufferInit[ch])
    {
        ma_audio_buffer_uninit(&mImpl->ChannelBuffers[ch]);
        mImpl->ChannelBufferInit[ch] = false;
    }

    // Create audio buffer from WAV data
    ma_audio_buffer_config BufferConfig = ma_audio_buffer_config_init(
        ma_format_s16, 1, WavData->size() / 2, WavData->data(), nullptr);
    auto& Buffer = mImpl->ChannelBuffers[ch];
    if (ma_audio_buffer_init(&BufferConfig, &Buffer) != MA_SUCCESS)
        return -1;
    mImpl->ChannelBufferInit[ch] = true;

    // Init sound from buffer with full spatialization
    ma_result Result = ma_sound_init_from_data_source(&mImpl->Engine, &Buffer, 0, nullptr, &Snd);
    if (Result != MA_SUCCESS)
    {
        ma_audio_buffer_uninit(&Buffer);
        mImpl->ChannelBufferInit[ch] = false;
        return -1;
    }

    // Enable spatialization
    ma_sound_set_positioning(&Snd, ma_positioning_absolute);
    ma_sound_set_attenuation_model(&Snd, ma_attenuation_model_inverse);
    ma_sound_set_position(&Snd, SourceX, SourceY, 0.0f);
    ma_sound_set_rolloff(&Snd, 1.0f);
    ma_sound_set_min_distance(&Snd, 10.0f);
    ma_sound_set_max_distance(&Snd, 500.0f);
    ma_sound_set_volume(&Snd, Volume);
    ma_sound_start(&Snd);

    mImpl->ChannelSoundInit[ch] = true;

    mChannels[ch].Active = true;
    mChannels[ch].Priority = Priority;
    mChannels[ch].Flags = F;
    mChannels[ch].SoundID = SoundID;
    mChannels[ch].UnitTypeKey = UnitType;

    return ch;
}

// ── Channel management ──────────────────────────────────────

int AudioSystem::PlayFile(const std::string& FilePath, float SourceX, float SourceY,
                          int Priority, SoundFlags Flags)
{
    if (!mInitialized || FilePath.empty())
        return -1;

    const uint8_t F = static_cast<uint8_t>(Flags);
    int ch = FindFreeChannel(Priority, F);
    if (ch < 0)
        return -1;

    auto& Snd = mImpl->ChannelSounds[ch];
    if (mImpl->ChannelSoundInit[ch])
    {
        ma_sound_uninit(&Snd);
        mImpl->ChannelSoundInit[ch] = false;
    }
    if (mImpl->ChannelBufferInit[ch])
    {
        ma_audio_buffer_uninit(&mImpl->ChannelBuffers[ch]);
        mImpl->ChannelBufferInit[ch] = false;
    }

    ma_result Result = ma_sound_init_from_file(&mImpl->Engine, FilePath.c_str(),
        MA_SOUND_FLAG_STREAM, nullptr, nullptr, &Snd);
    if (Result != MA_SUCCESS)
    {
        Core::Utils::Logger::Warning("AudioSystem", "Failed to play audio file: " + FilePath);
        return -1;
    }

    const float Volume = std::max(0.08f, CalculateAttenuation(SourceX, SourceY)) *
        ((float)mGlobalVolume / 100.0f);
    ma_sound_set_positioning(&Snd, ma_positioning_absolute);
    ma_sound_set_attenuation_model(&Snd, ma_attenuation_model_inverse);
    ma_sound_set_position(&Snd, SourceX, SourceY, 0.0f);
    ma_sound_set_rolloff(&Snd, 1.0f);
    ma_sound_set_min_distance(&Snd, 10.0f);
    ma_sound_set_max_distance(&Snd, 500.0f);
    ma_sound_set_volume(&Snd, Volume);
    ma_sound_start(&Snd);

    mImpl->ChannelSoundInit[ch] = true;
    mChannels[ch].Active = true;
    mChannels[ch].Priority = Priority;
    mChannels[ch].Flags = F;
    mChannels[ch].SoundID = -1;
    mChannels[ch].UnitTypeKey = -1;
    return ch;
}

int AudioSystem::FindFreeChannel(int Priority, uint8_t Flags)
{
    // Pass 1: find idle or finished channel
    for (int i = 0; i < kMaxChannels; ++i)
    {
        if (mChannels[i].Active)
        {
            if (mImpl->ChannelSoundInit[i] &&
                !ma_sound_is_playing(&mImpl->ChannelSounds[i]))
            {
                mChannels[i].Active = false;
                return i;
            }
        }
        else
        {
            return i;
        }
    }

    // Pass 2: evict lowest priority channel (skip uninterruptible)
    int bestChannel = -1;
    int lowestPrio = Priority;
    for (int i = 0; i < kMaxChannels; ++i)
    {
        if (mChannels[i].Flags & static_cast<uint8_t>(SoundFlags::Uninterruptible))
            continue;
        if (mChannels[i].Priority < lowestPrio)
        {
            lowestPrio = mChannels[i].Priority;
            bestChannel = i;
        }
    }
    return bestChannel;
}

bool AudioSystem::CheckSoundLimits(int SoundID, int UnitType, uint8_t Flags) const
{
    if (Flags & static_cast<uint8_t>(SoundFlags::PerSound))
    {
        for (int i = 0; i < kMaxChannels; ++i)
        {
            if (mChannels[i].Active && mChannels[i].SoundID == SoundID)
            {
                if (mImpl->ChannelSoundInit[i] &&
                    ma_sound_is_playing(&mImpl->ChannelSounds[i]))
                    return true; // blocked: same sound already playing
                // Otherwise channel is stale, mark inactive
            }
        }
    }

    if (Flags & static_cast<uint8_t>(SoundFlags::PerUnitType) && UnitType >= 0)
    {
        for (int i = 0; i < kMaxChannels; ++i)
        {
            if (mChannels[i].Active && mChannels[i].UnitTypeKey == UnitType &&
                (mChannels[i].Flags & static_cast<uint8_t>(SoundFlags::PerUnitType)))
            {
                if (mImpl->ChannelSoundInit[i] &&
                    ma_sound_is_playing(&mImpl->ChannelSounds[i]))
                    return true;
            }
        }
    }

    return false;
}

// ── Distance attenuation ───────────────────────────────────

float AudioSystem::CalculateAttenuation(float SourceX, float SourceY) const
{
    // openbw-ref formula: Manhattan distance to screen edge
    // volume = 99 - 99 * distance / 512
    // silence if volume <= 10

    int distance = 0;

    float ScreenLeft = mScreenX;
    float ScreenRight = mScreenX + mScreenW;
    float ScreenTop = mScreenY;
    float ScreenBottom = mScreenY + mScreenH;

    if (SourceX < ScreenLeft)
        distance += (int)(ScreenLeft - SourceX);
    else if (SourceX > ScreenRight)
        distance += (int)(SourceX - ScreenRight);

    if (SourceY < ScreenTop)
        distance += (int)(ScreenTop - SourceY);
    else if (SourceY > ScreenBottom)
        distance += (int)(SourceY - ScreenBottom);

    int distVolume = 99 - 99 * distance / 512;
    if (distVolume < 0) return 0.0f;

    return (float)distVolume / 99.0f;
}

// ── Stop / Update ───────────────────────────────────────────

void AudioSystem::StopSound(int ChannelID)
{
    if (ChannelID >= 0 && ChannelID < kMaxChannels)
    {
        mChannels[ChannelID].Active = false;
        if (mImpl->ChannelSoundInit[ChannelID])
            ma_sound_stop(&mImpl->ChannelSounds[ChannelID]);
    }
}

void AudioSystem::StopAllSounds()
{
    for (int i = 0; i < kMaxChannels; ++i)
    {
        mChannels[i].Active = false;
        if (mImpl->ChannelSoundInit[i])
            ma_sound_stop(&mImpl->ChannelSounds[i]);
    }
}

void AudioSystem::SetListenerPosition(float X, float Y)
{
    mListenerX = X;
    mListenerY = Y;
    ma_engine_listener_set_position(&mImpl->Engine, 0, X, Y, 0.0f);
}

void AudioSystem::SetScreenRect(float ScreenX, float ScreenY, float ScreenW, float ScreenH)
{
    mScreenX = ScreenX;
    mScreenY = ScreenY;
    mScreenW = ScreenW > 1.0f ? ScreenW : 1.0f;
    mScreenH = ScreenH > 1.0f ? ScreenH : 1.0f;
}

void AudioSystem::SetMasterVolume(int Volume)
{
    if (Volume < 0) Volume = 0;
    if (Volume > 100) Volume = 100;
    mGlobalVolume = Volume;

    float EngineVol = (float)Volume / 100.0f;
    ma_engine_set_volume(&mImpl->Engine, EngineVol);
}

void AudioSystem::Update(float DeltaTime)
{
    for (int i = 0; i < kMaxChannels; ++i)
    {
        if (mChannels[i].Active && mImpl->ChannelSoundInit[i])
        {
            if (!ma_sound_is_playing(&mImpl->ChannelSounds[i]))
                mChannels[i].Active = false;
        }
    }
    (void)DeltaTime;
}

// ── WAV loading ─────────────────────────────────────────────

bool AudioSystem::LoadWavData(int SoundID, const std::vector<uint8_t>& WavData)
{
    if (WavData.empty()) return false;
    if (SoundID >= (int)mWavCache.size())
        mWavCache.resize(SoundID + 1);
    mWavCache[SoundID] = WavData;

    // Extend cooldown tracker
    if (SoundID >= (int)mLastPlayed.size())
        mLastPlayed.resize(SoundID + 1);

    return true;
}

bool AudioSystem::LoadWavFromFile(const std::string& FilePath, int SoundID)
{
    if (!mFileLoader) return false;
    std::vector<uint8_t> Data = mFileLoader(FilePath);
    if (Data.empty()) return false;
    return LoadWavData(SoundID, Data);
}

// ── Sound type metadata ─────────────────────────────────────

void AudioSystem::SetSoundType(int SoundID, const SoundType& Type)
{
    if (SoundID >= (int)mSoundTypes.size())
        mSoundTypes.resize(SoundID + 1);
    mSoundTypes[SoundID] = Type;
    mSoundTypes[SoundID].Id = SoundID;

    // Extend cooldown tracker
    if (SoundID >= (int)mLastPlayed.size())
        mLastPlayed.resize(SoundID + 1);
}

const SoundType* AudioSystem::GetSoundType(int SoundID) const
{
    if (SoundID >= 0 && SoundID < (int)mSoundTypes.size())
        return &mSoundTypes[SoundID];
    return nullptr;
}

} // namespace Core::Audio
