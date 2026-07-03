#include "Main/GameLoop/GameSaveState.h"

#include <cstring>

namespace Game::Client::GameLoop
{

bool WriteGameSave(FILE* File, const GameSaveSnapshot& Snapshot)
{
    if (!File)
        return false;

    std::fprintf(File, "SCVS 1\n");
    std::fprintf(File, "map %s\n", Snapshot.MapPath.c_str());
    std::fprintf(File, "paused %d\n", Snapshot.Paused ? 1 : 0);
    std::fprintf(File, "minerals %d\n", Snapshot.Resources.MineralCount);
    std::fprintf(File, "gas %d\n", Snapshot.Resources.GasCount);
    std::fprintf(File, "supply_used %d\n", Snapshot.Resources.SupplyUsed);
    std::fprintf(File, "supply_max %d\n", Snapshot.Resources.SupplyMax);
    return true;
}

bool ReadGameSave(FILE* File, GameSaveSnapshot& Snapshot)
{
    if (!File)
        return false;

    char Line[512] = {};
    if (!std::fgets(Line, sizeof(Line), File) || std::strncmp(Line, "SCVS 1", 6) != 0)
        return false;

    int Paused = Snapshot.Paused ? 1 : 0;
    int Minerals = Snapshot.Resources.MineralCount;
    int Gas = Snapshot.Resources.GasCount;
    int SupplyUsed = Snapshot.Resources.SupplyUsed;
    int SupplyMax = Snapshot.Resources.SupplyMax;

    while (std::fgets(Line, sizeof(Line), File))
    {
        if (std::sscanf(Line, "paused %d", &Paused) == 1) continue;
        if (std::sscanf(Line, "minerals %d", &Minerals) == 1) continue;
        if (std::sscanf(Line, "gas %d", &Gas) == 1) continue;
        if (std::sscanf(Line, "supply_used %d", &SupplyUsed) == 1) continue;
        if (std::sscanf(Line, "supply_max %d", &SupplyMax) == 1) continue;
    }

    Snapshot.Paused = Paused != 0;
    Snapshot.Resources.MineralCount = Minerals;
    Snapshot.Resources.GasCount = Gas;
    Snapshot.Resources.SupplyUsed = SupplyUsed;
    Snapshot.Resources.SupplyMax = SupplyMax;
    return true;
}

} // namespace Game::Client::GameLoop