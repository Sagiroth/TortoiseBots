#pragma once

#include <algorithm>
#include <cstdint>

namespace ai
{
    // How far below its own level a grind *destination's* creatures may sit for an
    // autonomous bot, and how far above. The destination search checks the spawn
    // entry's level_max against this window (GrindTravelDestination::IsPossible).
    // The ceiling below level 10 is the grinder's own order cap
    // (GrindTargetValue::MaxGrindLevelOverBot); from level 10 the combat cap is +4
    // but autonomous bots run on weak, mostly starter gear, so the *destination*
    // ceiling stays one step above the bot and lets the bot pick what it dares
    // fight once it is there.
    std::int32_t const GRIND_LEVEL_UNDER = 2;
    std::int32_t const GRIND_LEVEL_OVER_LOW = 1;
    std::int32_t const GRIND_LEVEL_OVER_HIGH = 2;

    // How far above its own level an autonomous bot's grind destination may sit
    // by *area rating* (the zone/area level the travel gate compares, not the mob
    // level the band above bounds). Two gates apply it and they must agree:
    // TravelMgr::IsLocationLevelValid votes on the scanned point, and
    // GrindTravelDestination::IsPossible on the destination's closest point - when
    // the coarser area vote is looser it lets points through that the finer one
    // then rejects. Measured on the live cycle-3 pool: deaths per 1,000 kills rise
    // from 46 in areas at or below the bot's level to 117-150 in areas rated
    // bot+4..bot+5, while the XP per kill is flat (49.5 -> 50-52) - the far tail
    // buys no extra XP and about three times the deaths, so the margin stops at
    // +3. Levels 1-4 keep the wider margin: their start-valley sub-areas are rated
    // far above them (Camp Narache and Mulgore 6, Dun Morogh 7, Durotar 8), and the
    // tight margin would strip a level-1 bot of every destination it has. Owned
    // and hired bots keep it too: their player decides where they hunt.
    std::int32_t const GRIND_AREA_MARGIN = 3;

    // The margin owned/hired bots get, and the one the RPG errand and quest gates
    // still use (TravelMgr::IsLocationLevelValid).
    std::int32_t const GRIND_AREA_MARGIN_OWNED = 5;

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
    //     [botLevel - GRIND_LEVEL_UNDER, botLevel + (level < 10 ? LOW : HIGH)]
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
            std::int32_t const over = botLevel < 10 ? GRIND_LEVEL_OVER_LOW : GRIND_LEVEL_OVER_HIGH;
            maxLevel = std::max(maxLevel, (std::int32_t)botLevel + over);
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

    // Whether a creature is worth being a grind *destination* at all.
    //
    // Copper-bearing creatures always were. Coinless ones were excluded because
    // they carry no coin loot - but wildlife (wolves, boars, spiders, scorpids,
    // bears) is the open-field hunt of every level: it pays XP, grey vendor loot
    // and skins, and it is what keeps a bot out of the crowded humanoid camps.
    // So autonomous bots take it at every level; owned/hired bots keep the
    // copper-only rule their player's errands were built around, plus the
    // beginner allowance they always had. Critters pay no XP at all and are
    // never worth a walk.
    inline bool GrindPreyAllowed(std::int32_t goldMin, bool critter, bool autonomous, bool beginner)
    {
        if (goldMin > 0)
            return true;

        if (critter)
            return false;

        return autonomous || beginner;
    }

    // Whether a grind destination survives the hostility gate
    // (GrindTravelDestination::IsActive).
    //
    // Hostile entries are always prey. Neutral entries are prey when they are
    // XP-paying wildlife: no NPC flag (vendors, trainers and other service
    // mobs are never prey) and a non-zero XP multiplier (critters, Deer and
    // Toads pay nothing). Friendly entries are never prey.
    //
    // This mirrors the donor mod-playerbots grind filter
    // (src/Ai/Base/Value/GrindTargetValue.cpp: loot-carrying neutrals are kept,
    // only non-hostile NPCs are refused). The old hostile-only read excluded
    // every neutral starter beast - Thistle Boars / Nightsabers (faction 189/7
    // read REP_NEUTRAL against a player faction template) - so a level 1-3 bot
    // with no quest destination never held a grind destination and looped
    // QuestTripNoTarget instead of walking to its wolves (issue #393).
    inline bool GrindHostilityAllowed(bool hostileToBot, bool friendlyToBot, std::uint32_t npcFlags, bool paysXp)
    {
        if (hostileToBot)
            return true;

        if (friendlyToBot)
            return false;

        return npcFlags == 0 && paysXp;
    }

    // How many bots one grind destination may hold before the picker sends the next
    // bot elsewhere: a third of its spawn points, never below two (a bot may always
    // join a spot a single other bot is working). Without this every bot in a zone
    // walks to the same nearest spot - the destination shuffle only spreads them
    // inside the nearest distance range.
    inline std::uint32_t GrindSpotCapacity(std::uint32_t spawnPoints)
    {
        return std::max<std::uint32_t>(2, spawnPoints / 3);
    }
}
