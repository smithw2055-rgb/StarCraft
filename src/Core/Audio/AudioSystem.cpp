#define MINIAUDIO_IMPLEMENTATION
#include "third_party/miniaudio/miniaudio.h"

#include "Core/Audio/AudioSystem.h"
#include "Core/Utils/Logger.h"

#include <vector>
#include <cmath>
#include <cstring>
#include <cstdlib>

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

// ── AudioSystem ─────────────────────────────────────────────

struct AudioSystem::Impl
{
    ma_engine Engine;
    bool EngineInit = false;
    struct SoundEntry {
        std::vector<uint8_t> Wav;
        std::string FilePath;   // temp file written at init
        bool Loaded = false;
    };
    SoundEntry Sounds[(int)SoundEffect::Count];
};

static std::string WriteTempWav(const std::vector<uint8_t>& Data, const char* Name)
{
    std::string Path = std::string("/tmp/sc_") + Name + ".wav";
    FILE* F = fopen(Path.c_str(), "wb");
    if (!F) return "";
    fwrite(Data.data(), 1, Data.size(), F);
    fclose(F);
    return Path;
}

AudioSystem::AudioSystem() : mImpl(std::make_unique<Impl>()) {}
AudioSystem::~AudioSystem() { Shutdown(); }

bool AudioSystem::Init()
{
    ma_engine_config Config = ma_engine_config_init();
    if (ma_engine_init(&Config, &mImpl->Engine) != MA_SUCCESS)
    {
        Core::Utils::Logger::Warning("AudioSystem", "No audio device available");
        return false;
    }
    mImpl->EngineInit = true;

    mImpl->Sounds[(int)SoundEffect::WeaponFire].Wav = GenerateWav(GenSineSweep(0.08f, 1200, 400, 0.25f));
    mImpl->Sounds[(int)SoundEffect::Explosion].Wav = GenerateWav(GenNoise(0.3f, 0.3f));
    {
        auto T1 = GenTone(0.15f, 400, 0.25f);
        auto T2 = GenTone(0.15f, 600, 0.25f);
        T1.insert(T1.end(), T2.begin(), T2.end());
        mImpl->Sounds[(int)SoundEffect::BuildingComplete].Wav = GenerateWav(T1);
    }
    mImpl->Sounds[(int)SoundEffect::UIClick].Wav = GenerateWav(GenTone(0.04f, 1000, 0.15f));

    static const char* kNames[] = {"weapon", "explosion", "building", "click"};
    for (int I = 0; I < (int)SoundEffect::Count; I++)
    {
        auto& S = mImpl->Sounds[I];
        S.FilePath = WriteTempWav(S.Wav, kNames[I]);
        if (!S.FilePath.empty())
            S.Loaded = true;
    }

    mInitialized = true;
    Core::Utils::Logger::Info("AudioSystem", "Initialized");
    return true;
}

void AudioSystem::Shutdown()
{
    if (!mImpl) return;
    if (mImpl->EngineInit)
        ma_engine_uninit(&mImpl->Engine);
    // Clean up temp files
    for (int I = 0; I < (int)SoundEffect::Count; I++)
        if (!mImpl->Sounds[I].FilePath.empty())
            remove(mImpl->Sounds[I].FilePath.c_str());
    mInitialized = false;
}

void AudioSystem::Play(SoundEffect Effect)
{
    if (!mInitialized) return;
    int Idx = (int)Effect;
    if (Idx < 0 || Idx >= (int)SoundEffect::Count) return;
    auto& S = mImpl->Sounds[Idx];
    if (!S.Loaded) return;

    ma_engine_play_sound(&mImpl->Engine, S.FilePath.c_str(), nullptr);
}

} // namespace Core::Audio