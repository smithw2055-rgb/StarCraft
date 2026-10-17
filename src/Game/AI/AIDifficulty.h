#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Game::Systems
{

enum class AIDifficulty : uint8_t
{
    Easy,
    Medium,
    Hard
};

struct AIDifficultyProfile
{
    std::string_view Name;
    float DecisionIntervalSeconds;
    float AttackCooldownSeconds;
    float AttackGroupMaxAgeSeconds;
    float ExpansionStartSeconds;
    float ExpansionIntervalSeconds;
    float FirstScoutSeconds;
    float ScoutIntervalSeconds;
    float RepairHealthThreshold;
    float ThreatRadius;
    float DefenderRadius;
    int MaxBases;
    int WorkersPerBase;
    int WorkerCap;
    int SupplyBuffer;
    int MaxTechStage;
    int MaxRepairers;
    int DefenderReservePercent;
    int ReinforcementSize;
    std::array<int, 3> AttackWaveSize;
    bool AdaptiveComposition;
    bool ContinuousReinforcement;
};

inline constexpr AIDifficultyProfile kEasyAIDifficultyProfile = {
    "Easy",
    4.0f,
    35.0f,
    120.0f,
    900.0f,
    900.0f,
    120.0f,
    0.0f,
    0.50f,
    960.0f,
    3000.0f,
    2,
    20,
    20,
    2,
    1,
    1,
    0,
    0,
    {8, 12, 8},
    false,
    false
};

inline constexpr AIDifficultyProfile kMediumAIDifficultyProfile = {
    "Medium",
    2.0f,
    20.0f,
    90.0f,
    360.0f,
    420.0f,
    45.0f,
    180.0f,
    0.70f,
    720.0f,
    960.0f,
    2,
    18,
    40,
    4,
    2,
    2,
    20,
    0,
    {15, 18, 12},
    false,
    false
};

inline constexpr AIDifficultyProfile kHardAIDifficultyProfile = {
    "Hard",
    1.0f,
    10.0f,
    45.0f,
    240.0f,
    300.0f,
    45.0f,
    90.0f,
    0.85f,
    800.0f,
    1200.0f,
    3,
    20,
    60,
    8,
    3,
    4,
    25,
    4,
    {20, 24, 16},
    true,
    true
};

inline constexpr bool AIShouldStopAfterElimination(bool ShowcaseMode,
                                                    float ElapsedSeconds,
                                                    bool HasLivingStructures)
{
    return !ShowcaseMode && ElapsedSeconds >= 10.0f && !HasLivingStructures;
}

inline constexpr bool AIActiveAttackGroupAcceptsWorker(bool FinalAssault,
                                                       bool IsWorker)
{
    return !IsWorker || FinalAssault;
}

inline constexpr bool AIShouldBuildSpawningPool(int StartedPoolCount,
                                                int DroneCount,
                                                int Minerals)
{
    return StartedPoolCount < 1 && DroneCount >= 6 && Minerals >= 200;
}

inline constexpr bool AIShouldMorphLurker(int LurkerCount,
                                          int PendingLurkerCount,
                                          int TargetCount,
                                          bool AspectResearched,
                                          int Minerals,
                                          int Gas)
{
    return LurkerCount + PendingLurkerCount < TargetCount &&
           AspectResearched && Minerals >= 50 && Gas >= 100;
}

inline constexpr bool AIShouldConsiderLurkerThreatPlayer(
    int AIPlayerID,
    int CandidatePlayerID,
    bool IsRescuePassive,
    bool IsAllied)
{
    // Lurkers react to local battlefield threats, not only to the player's
    // current strategic attack target. Visibility and ground-unit checks stay
    // at the call site because they require ECS state.
    return CandidatePlayerID != AIPlayerID && !IsRescuePassive && !IsAllied;
}

inline constexpr bool AIShouldConsiderRetargetPlayer(
    int AIPlayerID,
    int CandidatePlayerID,
    bool IsAllied,
    bool IsRescuePassive,
    bool HasLivingStructures)
{
    return CandidatePlayerID != AIPlayerID && !IsAllied &&
           !IsRescuePassive && HasLivingStructures;
}

inline constexpr bool AIShouldUseRememberedEnemyPosition(
    float LastSeenSeconds,
    float ElapsedSeconds,
    bool LastSeenPositionGameplayVisible,
    float TimeToLiveSeconds = 240.0f)
{
    // Once real gameplay vision has revisited the remembered point and found
    // no currently visible target, the old position is cleared by observation.
    return LastSeenSeconds >= 0.0f && ElapsedSeconds >= LastSeenSeconds &&
           ElapsedSeconds - LastSeenSeconds <= TimeToLiveSeconds &&
           !LastSeenPositionGameplayVisible;
}

inline constexpr size_t AINextEnemySearchWaypointIndex(size_t CurrentIndex,
                                                        size_t WaypointCount)
{
    return WaypointCount > 0 ? (CurrentIndex + 1u) % WaypointCount : 0u;
}

inline constexpr bool AIShouldAdvanceEnemyMapSearch(bool FinalAssault,
                                                     int DirectTargetEntityID)
{
    return FinalAssault && DirectTargetEntityID < 0;
}

inline constexpr bool AIShouldReserveForLurkerAspect(int MaxTechStage,
                                                      bool HasAdvancedBase,
                                                      bool HasHydraliskDen,
                                                      bool HasHydralisk,
                                                      bool AspectResearched)
{
    return MaxTechStage >= 2 && HasAdvancedBase && HasHydraliskDen &&
           HasHydralisk && !AspectResearched;
}

inline constexpr bool AIShouldPauseZergMacroForLurkerAspect(
    bool ReserveLurkerAspect,
    bool AspectResearchInProgress)
{
    // The reservation must cover units and buildings as well as upgrades.
    // Once research has paid its 200/200 cost, normal macro can resume.
    return ReserveLurkerAspect && !AspectResearchInProgress;
}

inline constexpr bool AIShouldSpendMineralOverflow(AIDifficulty Difficulty,
                                                    int Minerals,
                                                    int SupplyUsed,
                                                    int SupplyMax,
                                                    int UnitSupplyCost)
{
    return Difficulty == AIDifficulty::Hard && Minerals >= 800 &&
           UnitSupplyCost > 0 && SupplyUsed + UnitSupplyCost <= SupplyMax;
}

namespace Detail
{

constexpr char ToLowerAscii(char Value)
{
    return Value >= 'A' && Value <= 'Z'
        ? static_cast<char>(Value - 'A' + 'a')
        : Value;
}

constexpr bool IsAsciiWhitespace(char Value)
{
    return Value == ' ' || Value == '\t' || Value == '\r' || Value == '\n';
}

constexpr std::string_view TrimAsciiWhitespace(std::string_view Value)
{
    while (!Value.empty() && IsAsciiWhitespace(Value.front()))
        Value.remove_prefix(1);
    while (!Value.empty() && IsAsciiWhitespace(Value.back()))
        Value.remove_suffix(1);
    return Value;
}

constexpr bool EqualsIgnoreAsciiCase(std::string_view Left, std::string_view Right)
{
    if (Left.size() != Right.size())
        return false;
    for (size_t I = 0; I < Left.size(); ++I)
    {
        if (ToLowerAscii(Left[I]) != ToLowerAscii(Right[I]))
            return false;
    }
    return true;
}

} // namespace Detail

constexpr AIDifficulty ParseAIDifficulty(
    std::string_view Value,
    AIDifficulty Fallback = AIDifficulty::Easy)
{
    Value = Detail::TrimAsciiWhitespace(Value);
    if (Detail::EqualsIgnoreAsciiCase(Value, "easy"))
        return AIDifficulty::Easy;
    if (Detail::EqualsIgnoreAsciiCase(Value, "medium"))
        return AIDifficulty::Medium;
    if (Detail::EqualsIgnoreAsciiCase(Value, "hard"))
        return AIDifficulty::Hard;
    return Fallback;
}

constexpr std::string_view AIDifficultyName(AIDifficulty Difficulty)
{
    switch (Difficulty)
    {
        case AIDifficulty::Medium: return "Medium";
        case AIDifficulty::Hard: return "Hard";
        default: return "Easy";
    }
}

constexpr std::string_view DifficultyName(AIDifficulty Difficulty)
{
    return AIDifficultyName(Difficulty);
}

constexpr const AIDifficultyProfile& GetAIDifficultyProfile(AIDifficulty Difficulty)
{
    switch (Difficulty)
    {
        case AIDifficulty::Medium: return kMediumAIDifficultyProfile;
        case AIDifficulty::Hard: return kHardAIDifficultyProfile;
        default: return kEasyAIDifficultyProfile;
    }
}

constexpr int CalculateAIWorkerTarget(
    const AIDifficultyProfile& Profile,
    int OwnedBases)
{
    const long long ScaledTarget =
        static_cast<long long>(Profile.WorkersPerBase) * OwnedBases;
    if (ScaledTarget < 12)
        return 12;
    if (ScaledTarget > Profile.WorkerCap)
        return Profile.WorkerCap;
    return static_cast<int>(ScaledTarget);
}

constexpr float AILurkerAttackRangePixels(float AttackRangeTiles)
{
    return AttackRangeTiles * 32.0f > 32.0f
        ? AttackRangeTiles * 32.0f : 32.0f;
}

constexpr bool ShouldAIBurrowLurker(float EnemyDistance,
                                    float AttackRangeTiles)
{
    const float Range = AILurkerAttackRangePixels(AttackRangeTiles);
    const float EnterRange = Range > 40.0f ? Range - 8.0f : 32.0f;
    return EnemyDistance <= EnterRange;
}

constexpr bool CanAIHoldBurrowedLurker(float EnemyDistance,
                                       float AttackRangeTiles)
{
    return EnemyDistance <= AILurkerAttackRangePixels(AttackRangeTiles);
}

constexpr int AITerranTurretsPerBase(AIDifficulty Difficulty,
                                     bool HasSeenAirOrCloak)
{
    if (Difficulty == AIDifficulty::Easy)
        return HasSeenAirOrCloak ? 1 : 0;
    if (Difficulty == AIDifficulty::Hard && HasSeenAirOrCloak)
        return 2;
    return 1;
}

} // namespace Game::Systems
