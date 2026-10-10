#pragma once

// Even distribution of new random bots across the six racial starting zones.
//
// After a pool reset the level-1 pool piles up in Valley of Trials/Durotar
// (orc + troll + relocated goblins), Elwynn (human + relocated high elves)
// and Dun Morogh (dwarf + gnome) while Tirisfal, Mulgore and Teldrassil sit
// nearly empty, so the crowded valleys run out of mobs. When
// AiPlayerbot.RandomBotEvenStartZones=1 the auto-create race pick goes to a
// valid race from the least-populated start zone. Goblins and high elves
// count toward Durotar and Elwynn, and the isolated-custom-zone normalization
// sends them to Valley of Trials and Northshire in both modes, so the zone a
// bot was counted for is the zone it plays in. 0 keeps the old uniform random
// race.
//
// Header-only and free of core/database types on purpose (same precedent as
// PoolResetPolicy.h/HunterPetPolicy.h): spawn coordinates are the core's own
// playercreateinfo rows (tw_world_playercreateinfo.sql), not guesses.
// Callers supply the per-zone pool counts; the pick itself is pure.

#include <cstdint>

namespace TortoiseBots
{

struct StartZoneSpawn
{
    uint32_t map = 0;
    uint32_t area = 0; // zone id; doubles as the characters.zone/homebind area value
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float o = 0.0f;
    char const* name = "";
};

inline constexpr uint32_t kStartZoneCount = 6;

// 0 Valley of Trials (Durotar), 1 Camp Narache (Mulgore), 2 Deathknell
// (Tirisfal), 3 Northshire (Elwynn), 4 Coldridge (Dun Morogh), 5 Shadowglen
// (Teldrassil). Horde = first three, Alliance = last three.
inline constexpr StartZoneSpawn kStartZoneSpawns[kStartZoneCount] =
{
    { 1, 14, -618.518f, -4251.67f, 38.718f, 0.0f, "Valley of Trials" },
    { 1, 215, -2917.58f, -257.98f, 52.9968f, 0.0f, "Camp Narache" },
    { 0, 85, 1676.35f, 1677.45f, 121.67f, 2.70526f, "Deathknell" },
    { 0, 12, -8949.95f, -132.493f, 83.5312f, 0.0f, "Northshire" },
    { 0, 1, -6240.32f, 331.033f, 382.758f, 6.17716f, "Coldridge" },
    { 1, 141, 10311.3f, 831.463f, 1326.41f, 5.48033f, "Shadowglen" },
};

// Race (SharedDefines Races enum values, kept numeric to stay core-free) to
// start-zone index. -1 for unknown races; callers must range-check.
inline int StartZoneIndexForRace(uint32_t race)
{
    switch (race)
    {
        case 2: case 8: case 9:  return 0; // orc, troll, goblin -> Durotar
        case 6:                  return 1; // tauren -> Mulgore
        case 5:                  return 2; // undead -> Tirisfal
        case 1: case 10:         return 3; // human, high elf -> Elwynn
        case 3: case 7:          return 4; // dwarf, gnome -> Dun Morogh
        case 4:                  return 5; // night elf -> Teldrassil
        default:                 return -1;
    }
}

// A pool character counts toward its start zone while it is still inside the
// starting zones' level 1-9 band. A fresh pool reaches level 2-3 within the
// ~10 minutes creation takes, so a level-1-only count read the zones whose bots
// level fastest as empty and kept filling them (live 2026-10-09: Dun Morogh 640
// against 256 in Teldrassil and 334 in Elwynn of 1230 Alliance bots).
inline constexpr uint32_t kStartZoneMaxLevel = 9;

inline bool CountsTowardStartZone(uint32_t level)
{
    return level <= kStartZoneMaxLevel;
}

} // namespace TortoiseBots
