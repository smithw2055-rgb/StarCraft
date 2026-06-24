#pragma once

#include <cstdint>
#include <memory>

namespace Core::Audio
{

enum class SoundEffect : uint8_t
{
    WeaponFire,
    Explosion,
    BuildingComplete,
    UIClick,
    Count
};

class AudioSystem
{
public:
    AudioSystem();
    ~AudioSystem();

    bool Init();
    void Shutdown();
    void Play(SoundEffect Effect);
    bool IsValid() const { return mInitialized; }

private:
    bool GenerateSounds();
    bool mInitialized = false;

    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Core::Audio