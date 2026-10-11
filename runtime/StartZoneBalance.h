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

// Short region names for the six start zones, same order as kStartZoneSpawns:
// Durotar, Mulgore, Tirisfal, Elwynn, Dun Morogh, Teldrassil. Used to parse
// AiPlayerbot.RandomBotStartZoneWeights ("Durotar=100,Elwynn=50"); spawn
// names ("Northshire", ...) work as aliases.
inline constexpr char const* kStartZoneRegionNames[kStartZoneCount] =
{
    "Durotar", "Mulgore", "Tirisfal", "Elwynn", "Dun Morogh", "Teldrassil",
};

// Horde = first three zones, Alliance = last three. Matches TeamForRace for
// every playable race, goblin and high elf included.
inline constexpr bool StartZoneIsAlliance(uint32_t zone)
{
    return zone >= kStartZoneCount / 2;
}

// Cap per zone so the six-zone total always fits uint32: the service rolls
// the weighted pick with urand(0, total - 1) and nothing can overflow.
inline constexpr uint32_t kStartZoneMaxWeight = 1000000;

// True when the already-normalized token matches a zone's region or spawn
// name (normalization: lowercase, spaces/tabs/underscores/dashes dropped, so
// "Dun Morogh", "dunmorogh" and "DUN_MOROGH" all match).
inline bool StartZoneNameMatches(char const* token, char const* name)
{
    while (*name)
    {
        unsigned char c = static_cast<unsigned char>(*name++);
        if (c == ' ' || c == '\t' || c == '_' || c == '-')
            continue;
        char lowered = char((c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c);
        if (lowered != *token++)
            return false;
    }
    return *token == '\0';
}

// Zone index for a weight-entry name, or -1 when unknown.
inline int StartZoneIndexByName(char const* begin, char const* end)
{
    char token[64];
    uint32_t len = 0;
    for (char const* p = begin; p != end; ++p)
    {
        unsigned char c = static_cast<unsigned char>(*p);
        if (c == ' ' || c == '\t' || c == '_' || c == '-')
            continue;
        if (len + 1 >= sizeof(token))
            return -1;
        token[len++] = char((c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c);
    }
    token[len] = '\0';
    if (!len)
        return -1;
    for (uint32_t zone = 0; zone < kStartZoneCount; ++zone)
    {
        char const* names[2] = { kStartZoneRegionNames[zone], kStartZoneSpawns[zone].name };
        for (char const* name : names)
            if (StartZoneNameMatches(token, name))
                return int(zone);
    }
    return -1;
}

// Parse "Durotar=100,Elwynn=50" into per-zone weights (out is zeroed first,
// a repeated zone keeps its last weight, each weight capped). Empty/blank
// text is not an error: out stays all-zero and the caller keeps today's even
// spread. Returns false on any malformed entry (missing '=', unknown zone,
// non-numeric weight); the caller must then log once and fall back to
// today's behaviour.
inline bool ParseStartZoneWeights(char const* text, uint32_t (&out)[kStartZoneCount])
{
    for (uint32_t& weight : out)
        weight = 0;
    if (!text)
        return true;
    char const* cursor = text;
    while (*cursor)
    {
        char const* comma = cursor;
        while (*comma && *comma != ',')
            ++comma;
        char const* nameBegin = cursor;
        char const* nameEnd = comma;
        while (nameBegin != nameEnd && (*nameBegin == ' ' || *nameBegin == '\t'))
            ++nameBegin;
        while (nameEnd != nameBegin && (nameEnd[-1] == ' ' || nameEnd[-1] == '\t'))
            --nameEnd;
        if (nameBegin != nameEnd)
        {
            char const* equals = nameBegin;
            while (equals != nameEnd && *equals != '=')
                ++equals;
            char const* valueBegin = equals;
            if (valueBegin != nameEnd && *valueBegin == '=')
                ++valueBegin;
            while (valueBegin != nameEnd && (*valueBegin == ' ' || *valueBegin == '\t'))
                ++valueBegin;
            char const* valueEnd = nameEnd;
            while (valueEnd != valueBegin && (valueEnd[-1] == ' ' || valueEnd[-1] == '\t'))
                --valueEnd;
            if (equals == nameEnd || valueBegin == valueEnd)
                return false;
            int zone = StartZoneIndexByName(nameBegin, equals);
            if (zone < 0)
                return false;
            uint32_t value = 0;
            for (char const* p = valueBegin; p != valueEnd; ++p)
            {
                if (*p < '0' || *p > '9')
                    return false;
                value = value * 10 + uint32_t(*p - '0');
                if (value > kStartZoneMaxWeight)
                    value = kStartZoneMaxWeight;
            }
            out[zone] = value;
        }
        cursor = *comma ? comma + 1 : comma;
    }
    return true;
}

// Total selectable weight over the zones that still have a valid race in the
// candidate list. Zero means "nothing selectable": caller falls back.
inline uint32_t StartZoneWeightedTotal(uint32_t const (&weights)[kStartZoneCount], bool const (&hasRace)[kStartZoneCount])
{
    uint32_t total = 0;
    for (uint32_t zone = 0; zone < kStartZoneCount; ++zone)
        if (hasRace[zone])
            total += weights[zone];
    return total;
}

// Map a pre-rolled value in [0, total) to a zone, skipping zones without an
// eligible race or with zero weight. Returns -1 past the end (caller falls
// back); the caller owns the RNG.
inline int StartZoneWeightedPick(uint32_t const (&weights)[kStartZoneCount], bool const (&hasRace)[kStartZoneCount], uint32_t roll)
{
    for (uint32_t zone = 0; zone < kStartZoneCount; ++zone)
    {
        if (!hasRace[zone] || !weights[zone])
            continue;
        if (roll < weights[zone])
            return int(zone);
        roll -= weights[zone];
    }
    return -1;
}

} // namespace TortoiseBots
