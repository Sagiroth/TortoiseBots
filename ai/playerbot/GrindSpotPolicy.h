#pragma once

#include <algorithm>
#include <cstdint>

namespace ai
{
    // How far below its own level a grind *destination's* creatures may sit for an
    // autonomous bot, and how far above. The destination search checks the spawn
    // entry's level_max against this window (GrindTravelDestination::IsPossible).
    std::int32_t const GRIND_LEVEL_UNDER = 2;
    std::int32_t const GRIND_LEVEL_OVER = 1;

    // Creature-level window a grind destination must sit in.
    //
    // The long-standing window (roughly half the bot's level, narrowed further by
    // gear condition) always sat two to ten levels below the bot, so bots ground
    // mobs that had long gone green and never had a reason to walk on. At level 5
    // it collapsed to exactly level-3 creatures - the measured starter-zone wall
    // (157/500 fresh bots parked at level 5, 5->6 never completing) - and the zone
    // floor built on the same numbers then rejected every point in their own
    // starting valley, leaving them no grind destination at all.
    //
    // Autonomous (masterless random) bots get a level-appropriate window instead:
    // the destination's creature level_max must land inside
    //     [botLevel - GRIND_LEVEL_UNDER, botLevel + GRIND_LEVEL_OVER]
    // A mob two levels down still pays ~83% of the kill's base XP and ten levels
    // down pays nothing at all (MaNGOS::XP::GetGrayLevel / BaseGainLevelFactor);
    // the grinder itself refuses orders more than one level above a sub-10 bot
    // (four above from level 10), so a destination outside the band would only
    // ever have been walked to for nothing. Because the window travels with the
    // bot, outgrowing a spot invalidates the travel target that led there and the
    // next request picks a spot in the next fitting field or zone - the walk stays
    // organic (still the travel graph, no teleports).
    //
    // Owned/hired bots keep the conservative window: their real player picks the
    // hunting ground. The beginner clamp (levels 1-4 see their own level) stays
    // for them, and for autonomous bots it is subsumed by the ladder window.
    struct GrindLevelBand
    {
        std::int32_t minLevel; // lowest creature level_max a destination may have
        std::int32_t maxLevel; // highest creature level_max a destination may have
    };

    inline GrindLevelBand GetGrindLevelBand(std::uint32_t botLevel, std::uint8_t powerLevel, bool levelAppropriate)
    {
        float const levelMod = powerLevel / 500.0f;   // 0 .. 0.2
        float const levelBoost = powerLevel / 50.0f;  // 0 .. 2

        std::int32_t maxLevel = std::max((std::int32_t)(botLevel * (0.5f + levelMod)), (std::int32_t)(botLevel - 5.0f + levelBoost));
        std::int32_t minLevel = std::max((std::int32_t)(botLevel * (0.4f + levelMod)), (std::int32_t)(botLevel - 12.0f + levelBoost));

        if (levelAppropriate)
        {
            minLevel = std::max(minLevel, (std::int32_t)botLevel - GRIND_LEVEL_UNDER);
            maxLevel = std::max(maxLevel, (std::int32_t)botLevel + GRIND_LEVEL_OVER);
        }
        else if (botLevel <= 4)
        {
            // The window above truncates to 0 at level 1 (and to 1-2 at levels 2-4),
            // which leaves a fresh bot in an enclosed starting valley without a
            // single destination. Let beginners fight their own level.
            maxLevel = std::max(maxLevel, (std::int32_t)botLevel);
        }

        return GrindLevelBand{ minLevel, maxLevel };
    }

    inline bool GrindLevelFits(const GrindLevelBand& band, std::int32_t creatureLevelMax)
    {
        return creatureLevelMax >= band.minLevel && creatureLevelMax <= band.maxLevel;
    }
}
