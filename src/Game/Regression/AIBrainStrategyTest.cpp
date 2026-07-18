#include "Game/AI/AIDifficulty.h"

#include <array>
#include <cstdio>
#include <cstdlib>

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

void TestDifficultyParsing()
{
    using Game::Systems::AIDifficulty;
    using Game::Systems::AIDifficultyName;
    using Game::Systems::DifficultyName;
    using Game::Systems::ParseAIDifficulty;

    Require(ParseAIDifficulty("easy", AIDifficulty::Hard) == AIDifficulty::Easy,
        "easy parses independently of fallback");
    Require(ParseAIDifficulty("MEDIUM", AIDifficulty::Easy) == AIDifficulty::Medium,
        "difficulty parsing is case insensitive");
    Require(ParseAIDifficulty("  Hard\t", AIDifficulty::Easy) == AIDifficulty::Hard,
        "difficulty parsing trims ASCII whitespace");
    Require(ParseAIDifficulty("unknown", AIDifficulty::Medium) == AIDifficulty::Medium,
        "unknown difficulty uses the requested fallback");
    Require(ParseAIDifficulty("", AIDifficulty::Hard) == AIDifficulty::Hard,
        "empty difficulty uses the requested fallback");

    Require(AIDifficultyName(AIDifficulty::Easy) == "Easy",
        "easy difficulty has a stable display name");
    Require(AIDifficultyName(AIDifficulty::Medium) == "Medium",
        "medium difficulty has a stable display name");
    Require(AIDifficultyName(AIDifficulty::Hard) == "Hard",
        "hard difficulty has a stable display name");
    Require(DifficultyName(AIDifficulty::Medium) == AIDifficultyName(AIDifficulty::Medium),
        "compatibility difficulty name delegates to the canonical API");
}

void TestProfileProgression()
{
    using Game::Systems::AIDifficulty;
    using Game::Systems::GetAIDifficultyProfile;

    const auto& Easy = GetAIDifficultyProfile(AIDifficulty::Easy);
    const auto& Medium = GetAIDifficultyProfile(AIDifficulty::Medium);
    const auto& Hard = GetAIDifficultyProfile(AIDifficulty::Hard);

    Require(Easy.Name == "Easy" && Medium.Name == "Medium" && Hard.Name == "Hard",
        "profiles retain their difficulty names");
    Require(Easy.DecisionIntervalSeconds > Medium.DecisionIntervalSeconds &&
            Medium.DecisionIntervalSeconds > Hard.DecisionIntervalSeconds,
        "higher difficulty makes decisions more frequently");
    Require(Easy.AttackCooldownSeconds > Medium.AttackCooldownSeconds &&
            Medium.AttackCooldownSeconds > Hard.AttackCooldownSeconds,
        "higher difficulty attacks on a shorter cooldown");
    Require(Easy.AttackGroupMaxAgeSeconds > Medium.AttackGroupMaxAgeSeconds &&
            Medium.AttackGroupMaxAgeSeconds > Hard.AttackGroupMaxAgeSeconds,
        "higher difficulty recycles stalled attack groups sooner");
    Require(Game::Systems::AIShouldStopAfterElimination(false, 10.0f, false) &&
            !Game::Systems::AIShouldStopAfterElimination(true, 10.0f, false) &&
            !Game::Systems::AIShouldStopAfterElimination(false, 9.0f, false) &&
            !Game::Systems::AIShouldStopAfterElimination(false, 10.0f, true),
        "melee brain stops only after an established player loses every structure");
    Require(Game::Systems::AIShouldConsiderRetargetPlayer(
                1, 2, false, false, true) &&
            !Game::Systems::AIShouldConsiderRetargetPlayer(
                1, 1, false, false, true) &&
            !Game::Systems::AIShouldConsiderRetargetPlayer(
                1, 2, true, false, true) &&
            !Game::Systems::AIShouldConsiderRetargetPlayer(
                1, 2, false, true, true) &&
            !Game::Systems::AIShouldConsiderRetargetPlayer(
                1, 2, false, false, false),
        "retargeting ignores self, allies, rescue-passive slots, and eliminated players");
    Require(Game::Systems::AIActiveAttackGroupAcceptsWorker(true, true) &&
            !Game::Systems::AIActiveAttackGroupAcceptsWorker(false, true) &&
            Game::Systems::AIActiveAttackGroupAcceptsWorker(false, false),
        "final-assault workers remain active instead of being reissued each cooldown");
    Require(Game::Systems::AIShouldBuildSpawningPool(0, 6, 200) &&
            !Game::Systems::AIShouldBuildSpawningPool(1, 20, 2000) &&
            !Game::Systems::AIShouldBuildSpawningPool(0, 5, 200) &&
            !Game::Systems::AIShouldBuildSpawningPool(0, 6, 199),
        "Zerg foundational Pool rebuild is stage-independent but still resource-gated");
    Require(Game::Systems::AIShouldMorphLurker(1, 1, 3, true, 50, 100) &&
            !Game::Systems::AIShouldMorphLurker(3, 0, 3, true, 500, 500) &&
            !Game::Systems::AIShouldMorphLurker(0, 0, 3, false, 500, 500) &&
            !Game::Systems::AIShouldMorphLurker(0, 0, 3, true, 49, 100),
        "researched Lurker morph remains an AI option without live tech buildings");
    Require(Game::Systems::AIShouldReserveForLurkerAspect(
                2, true, true, true, false) &&
            Game::Systems::AIShouldReserveForLurkerAspect(
                3, true, true, true, false) &&
            !Game::Systems::AIShouldReserveForLurkerAspect(
                1, true, true, true, false) &&
            !Game::Systems::AIShouldReserveForLurkerAspect(
                2, false, true, true, false) &&
            !Game::Systems::AIShouldReserveForLurkerAspect(
                2, true, true, true, true),
        "Medium and Hard Zerg reserve the Lurker Aspect only after its branch is ready");
    Require(Game::Systems::AIShouldPauseZergMacroForLurkerAspect(true, false) &&
            !Game::Systems::AIShouldPauseZergMacroForLurkerAspect(false, false) &&
            !Game::Systems::AIShouldPauseZergMacroForLurkerAspect(true, true),
        "Lurker Aspect reservation pauses all Zerg spending only until research starts");
    Require(Game::Systems::AIShouldSpendMineralOverflow(
                AIDifficulty::Hard, 800, 120, 200, 4) &&
            !Game::Systems::AIShouldSpendMineralOverflow(
                AIDifficulty::Medium, 5000, 120, 200, 4) &&
            !Game::Systems::AIShouldSpendMineralOverflow(
                AIDifficulty::Hard, 799, 120, 200, 4) &&
            !Game::Systems::AIShouldSpendMineralOverflow(
                AIDifficulty::Hard, 800, 198, 200, 4),
        "Hard Protoss spends excess minerals only when a Zealot fits its supply cap");
    Require(Easy.ExpansionStartSeconds > Medium.ExpansionStartSeconds &&
            Medium.ExpansionStartSeconds > Hard.ExpansionStartSeconds,
        "higher difficulty expands earlier");
    Require(Easy.ExpansionIntervalSeconds > Medium.ExpansionIntervalSeconds &&
            Medium.ExpansionIntervalSeconds > Hard.ExpansionIntervalSeconds,
        "higher difficulty expands more frequently");
    Require(Easy.ScoutIntervalSeconds == 0.0f &&
            Medium.ScoutIntervalSeconds > Hard.ScoutIntervalSeconds &&
            Hard.ScoutIntervalSeconds > 0.0f,
        "easy disables repeat scouting while harder profiles scout periodically");
    Require(Easy.RepairHealthThreshold < Medium.RepairHealthThreshold &&
            Medium.RepairHealthThreshold < Hard.RepairHealthThreshold,
        "higher difficulty starts repairs at more health");
    Require(Easy.WorkerCap < Medium.WorkerCap && Medium.WorkerCap < Hard.WorkerCap,
        "worker capacity rises with difficulty");
    Require(Easy.SupplyBuffer < Medium.SupplyBuffer &&
            Medium.SupplyBuffer < Hard.SupplyBuffer,
        "higher difficulty reserves more supply headroom");
    Require(Easy.MaxTechStage < Medium.MaxTechStage &&
            Medium.MaxTechStage < Hard.MaxTechStage,
        "higher difficulty unlocks more technology stages");
    Require(Easy.MaxRepairers < Medium.MaxRepairers &&
            Medium.MaxRepairers < Hard.MaxRepairers,
        "higher difficulty assigns more repairers");
    Require(Easy.DefenderReservePercent < Medium.DefenderReservePercent &&
            Medium.DefenderReservePercent < Hard.DefenderReservePercent,
        "higher difficulty keeps a larger defense reserve");
    Require(Easy.ReinforcementSize == 0 && Medium.ReinforcementSize == 0 &&
            Hard.ReinforcementSize == 4,
        "only hard difficulty sends continuous reinforcements");
    Require(!Easy.AdaptiveComposition && !Medium.AdaptiveComposition &&
            Hard.AdaptiveComposition,
        "adaptive composition is a hard difficulty feature");
    Require(!Easy.ContinuousReinforcement && !Medium.ContinuousReinforcement &&
            Hard.ContinuousReinforcement,
        "continuous reinforcement is a hard difficulty feature");
}

void TestRaceAttackWaves()
{
    using Game::Systems::AIDifficulty;
    using Game::Systems::GetAIDifficultyProfile;

    Require(GetAIDifficultyProfile(AIDifficulty::Easy).AttackWaveSize ==
            std::array<int, 3>{8, 12, 8},
        "easy Terran, Zerg, and Protoss wave sizes are exact");
    Require(GetAIDifficultyProfile(AIDifficulty::Medium).AttackWaveSize ==
            std::array<int, 3>{15, 18, 12},
        "medium Terran, Zerg, and Protoss wave sizes are exact");
    Require(GetAIDifficultyProfile(AIDifficulty::Hard).AttackWaveSize ==
            std::array<int, 3>{20, 24, 16},
        "hard Terran, Zerg, and Protoss wave sizes are exact");
}

void TestWorkerScaling()
{
    using Game::Systems::AIDifficulty;
    using Game::Systems::CalculateAIWorkerTarget;
    using Game::Systems::GetAIDifficultyProfile;

    const auto& Easy = GetAIDifficultyProfile(AIDifficulty::Easy);
    const auto& Medium = GetAIDifficultyProfile(AIDifficulty::Medium);
    const auto& Hard = GetAIDifficultyProfile(AIDifficulty::Hard);

    Require(CalculateAIWorkerTarget(Easy, 0) == 12,
        "zero bases clamps to the minimum worker target");
    Require(CalculateAIWorkerTarget(Easy, 1) == 20 &&
            CalculateAIWorkerTarget(Easy, 2) == 20,
        "easy worker production caps at twenty");
    Require(CalculateAIWorkerTarget(Medium, 1) == 18 &&
            CalculateAIWorkerTarget(Medium, 2) == 36 &&
            CalculateAIWorkerTarget(Medium, 3) == 40,
        "medium worker target scales per base and respects its cap");
    Require(CalculateAIWorkerTarget(Hard, 1) == 20 &&
            CalculateAIWorkerTarget(Hard, 2) == 40 &&
            CalculateAIWorkerTarget(Hard, 3) == 60 &&
            CalculateAIWorkerTarget(Hard, 8) == 60,
        "hard worker target scales through three bases and respects its cap");
    Require(CalculateAIWorkerTarget(Hard, -1) == 12,
        "negative base counts clamp safely to the minimum");
}

void TestLurkerRangeDecisions()
{
    using Game::Systems::AILurkerAttackRangePixels;
    using Game::Systems::AIShouldConsiderLurkerThreatPlayer;
    using Game::Systems::CanAIHoldBurrowedLurker;
    using Game::Systems::ShouldAIBurrowLurker;

    Require(AIShouldConsiderLurkerThreatPlayer(1, 0, false, false) &&
            AIShouldConsiderLurkerThreatPlayer(1, 2, false, false) &&
            !AIShouldConsiderLurkerThreatPlayer(1, 1, false, false) &&
            !AIShouldConsiderLurkerThreatPlayer(1, 0, true, false) &&
            !AIShouldConsiderLurkerThreatPlayer(1, 0, false, true),
        "Lurker local defense considers every hostile player but ignores self, rescue, and allies");
    Require(AILurkerAttackRangePixels(6.0f) == 192.0f,
        "Lurker attack range converts from tiles to pixels exactly");
    Require(ShouldAIBurrowLurker(184.0f, 6.0f) &&
            !ShouldAIBurrowLurker(192.0f, 6.0f),
        "AI enters burrow slightly inside weapon range to avoid threshold churn");
    Require(CanAIHoldBurrowedLurker(192.0f, 6.0f) &&
            !CanAIHoldBurrowedLurker(250.0f, 6.0f),
        "burrowed Lurker cannot be held in the former 192-256 pixel dead zone");
}

void TestEnemyMapSearchProgression()
{
    using Game::Systems::AINextEnemySearchWaypointIndex;
    using Game::Systems::AIShouldAdvanceEnemyMapSearch;
    using Game::Systems::AIShouldUseRememberedEnemyPosition;

    Require(AINextEnemySearchWaypointIndex(0, 3) == 1 &&
            AINextEnemySearchWaypointIndex(1, 3) == 2 &&
            AINextEnemySearchWaypointIndex(2, 3) == 0 &&
            AINextEnemySearchWaypointIndex(7, 0) == 0,
        "final attack groups advance and wrap an independent map-search cursor");
    Require(AIShouldAdvanceEnemyMapSearch(true, -1) &&
            !AIShouldAdvanceEnemyMapSearch(false, -1) &&
            !AIShouldAdvanceEnemyMapSearch(true, 42),
        "only a final assault without a visible direct target advances map search");
    Require(AIShouldUseRememberedEnemyPosition(100.0f, 200.0f, false) &&
            !AIShouldUseRememberedEnemyPosition(100.0f, 200.0f, true) &&
            !AIShouldUseRememberedEnemyPosition(100.0f, 341.0f, false) &&
            !AIShouldUseRememberedEnemyPosition(-1.0f, 200.0f, false),
        "enemy position memory expires when revisited by gameplay vision or its TTL elapses");
}

void TestTerranDetectorBaseline()
{
    using Game::Systems::AIDifficulty;
    using Game::Systems::AITerranTurretsPerBase;

    Require(AITerranTurretsPerBase(AIDifficulty::Easy, false) == 0 &&
            AITerranTurretsPerBase(AIDifficulty::Easy, true) == 1,
        "easy Terran reacts to observed air or cloak without a detector baseline");
    Require(AITerranTurretsPerBase(AIDifficulty::Medium, false) == 1 &&
            AITerranTurretsPerBase(AIDifficulty::Medium, true) == 1,
        "medium Terran keeps one detector per base before invisible units are detected");
    Require(AITerranTurretsPerBase(AIDifficulty::Hard, false) == 1 &&
            AITerranTurretsPerBase(AIDifficulty::Hard, true) == 2,
        "hard Terran doubles per-base detection after learning an air or cloak threat");
}

} // namespace

int main()
{
    TestDifficultyParsing();
    TestProfileProgression();
    TestRaceAttackWaves();
    TestWorkerScaling();
    TestLurkerRangeDecisions();
    TestEnemyMapSearchProgression();
    TestTerranDetectorBaseline();
    std::printf("AIBrainStrategyTest passed\n");
    return 0;
}
