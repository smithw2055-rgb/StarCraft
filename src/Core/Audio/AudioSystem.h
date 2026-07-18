#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <chrono>

namespace Core::Audio
{

enum class SoundEffect : uint8_t
{
    WeaponFire,
    Explosion,
    BuildingComplete,
    UIClick,
    UnitSelected,
    UnitMoveAck,
    UnitAttackAck,
    UnitDeath,
    NukeLaunch,
    SpellCast,
    Count
};

// Sound type flags (matching openbw-ref sfxdata.dat flags)
enum class SoundFlags : uint8_t
{
    None          = 0,
    PerUnitType   = 0x02,   // Only one instance per unit type at a time
    PerSound      = 0x10,   // Only one instance per sound ID at a time
    Uninterruptible = 0x20, // Channel never gets evicted by priority
};

struct SoundType
{
    uint32_t Id = 0;
    int Priority = 0;
    uint8_t Flags = 0;
    int Race = 0;
    int MinVolume = 0;  // classic minimum-volume floor (0-100 scale)
};

struct SoundPlaybackPolicy
{
    int Priority = 0;
    uint8_t Flags = 0;
    int MinVolume = 0;
};

// sfxdata.tbl stores paths relative to the classic "sound" directory, while
// archive readers expect a path rooted at the game data directory.
inline std::string NormalizeArchiveSoundPath(std::string Path)
{
    std::replace(Path.begin(), Path.end(), '\\', '/');
    while (!Path.empty() && Path.front() == '/')
        Path.erase(Path.begin());

    constexpr char Prefix[] = "sound/";
    bool HasPrefix = Path.size() >= sizeof(Prefix) - 1;
    for (size_t I = 0; HasPrefix && I < sizeof(Prefix) - 1; ++I)
    {
        HasPrefix = std::tolower((unsigned char)Path[I]) == Prefix[I];
    }
    return Path.empty() || HasPrefix ? Path : std::string(Prefix) + Path;
}

// sfxdata.dat stores a one-based index into sfxdata.tbl.  TblDecoder exposes
// the table as a normal zero-based vector, so consuming the DAT value directly
// selects the next sound (for example Marine 69 became Goliath fire, and
// Dragoon 99 became the projectile hit sound).
inline std::string ResolveArchiveSoundPath(
    const std::vector<std::string>& TblPaths,
    uint32_t OneBasedTblIndex)
{
    if (OneBasedTblIndex == 0 || OneBasedTblIndex > TblPaths.size())
        return {};
    return NormalizeArchiveSoundPath(TblPaths[OneBasedTblIndex - 1]);
}

inline SoundPlaybackPolicy ResolveSoundPlaybackPolicy(
    const SoundType* Metadata,
    int RequestedPriority,
    SoundFlags RequestedFlags)
{
    SoundPlaybackPolicy Policy;
    Policy.Priority = RequestedPriority;
    Policy.Flags = static_cast<uint8_t>(RequestedFlags);
    if (Metadata)
    {
        Policy.Priority = std::max(Policy.Priority, Metadata->Priority);
        Policy.Flags |= Metadata->Flags;
        Policy.MinVolume = Metadata->MinVolume;
    }
    return Policy;
}

struct AudioChannel
{
    int ChannelID = 0;
    int Priority = 0;
    uint8_t Flags = 0;
    int SoundID = -1;
    int UnitTypeKey = -1;
    bool Active = false;
};

class AudioSystem
{
public:
    static constexpr int kMaxChannels = 8;

    AudioSystem();
    ~AudioSystem();

    bool Init();
    void Shutdown();
    bool IsValid() const { return mInitialized; }

    // Simple enum-based play (backward compat)
    void Play(SoundEffect Effect);

    // Full positional play with priority and flags
    // Returns channel ID (0..7) or -1 if dropped
    int PlaySound(int SoundID, float SourceX, float SourceY,
                  int Priority = 0, int UnitType = -1,
                  SoundFlags Flags = SoundFlags::None);
    int PlayFile(const std::string& FilePath, float SourceX, float SourceY,
                 int Priority = 0, SoundFlags Flags = SoundFlags::None);

    // Stop a specific channel
    void StopSound(int ChannelID);
    void StopAllSounds();

    // Listener and screen state for distance attenuation
    void SetListenerPosition(float X, float Y);
    void SetScreenRect(float ScreenX, float ScreenY, float ScreenW, float ScreenH);

    // Global volume 0..100
    void SetMasterVolume(int Volume);
    int GetMasterVolume() const { return mGlobalVolume; }

    // Per-frame update
    void Update(float DeltaTime);

    // WAV data loading (from game archives)
    using LoadCallback = std::function<std::vector<uint8_t>(const std::string& FilePath)>;
    void SetFileLoader(LoadCallback Callback) { mFileLoader = std::move(Callback); }
    void SetSoundPath(int SoundID, std::string FilePath);
    bool LoadWavData(int SoundID, const std::vector<uint8_t>& WavData);
    bool LoadWavFromFile(const std::string& FilePath, int SoundID);

    // Sound type metadata
    void SetSoundType(int SoundID, const SoundType& Type);
    const SoundType* GetSoundType(int SoundID) const;

    size_t GetLoadedSoundCount() const { return mWavCache.size(); }

private:
    void InitBuiltinSounds();
    int FindFreeChannel(int Priority, uint8_t Flags);
    float CalculateAttenuation(float SourceX, float SourceY) const;
    bool CheckSoundLimits(int SoundID, int UnitType, uint8_t Flags) const;

    bool mInitialized = false;
    int mGlobalVolume = 100;
    float mListenerX = 0.0f;
    float mListenerY = 0.0f;

    // Screen rectangle for distance attenuation
    float mScreenX = 0.0f;
    float mScreenY = 0.0f;
    float mScreenW = 1.0f;
    float mScreenH = 1.0f;

    AudioChannel mChannels[kMaxChannels];

    // WAV data cache (indexed by sound ID)
    std::vector<std::vector<uint8_t>> mWavCache;
    std::vector<std::string> mSoundPaths;

    // Sound type metadata
    std::vector<SoundType> mSoundTypes;

    // Per-sound cooldown (80ms)
    std::vector<std::chrono::steady_clock::time_point> mLastPlayed;

    LoadCallback mFileLoader;

    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

inline SoundFlags operator|(SoundFlags A, SoundFlags B)
{
    return static_cast<SoundFlags>(static_cast<uint8_t>(A) | static_cast<uint8_t>(B));
}
inline bool operator&(SoundFlags A, SoundFlags B)
{
    return (static_cast<uint8_t>(A) & static_cast<uint8_t>(B)) != 0;
}

} // namespace Core::Audio
