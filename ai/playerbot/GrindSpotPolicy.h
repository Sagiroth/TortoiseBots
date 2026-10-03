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

    // Whether a masterless starter bot (level 1-4) is exempt from the
    // destination area-average ceiling. Its own valley is rated far above it
    // (Camp Narache and Mulgore 6, Dun Morogh 7, Durotar 8), so the ceiling
    // vetoes every local grind point and the bot parks at spawn with no
    // destination at all - the starter-valley wall the ladder window exists
    // for. The mob-level band above still vets every creature (in-cap only),
    // and the grey, critter, elite, point-danger and distance gates are
    // untouched, so the walk stays inside the safe valley. Owned/hired bots
    // keep the ceiling: their player decides where they hunt. Same scope as
    // the TravelMgr::IsLocationLevelValid beginnerGrind exemption, which this
    // mirrors at the two other gates that apply the same ceiling.
    inline bool GrindValleyExempted(std::uint32_t botLevel, bool masterlessRandom)
    {
        return masterlessRandom && botLevel <= 4;
    }

    // How far an idle starter bot looks for a fallback grind target when the
    // normal scan found nothing and it holds no travel destination. Starter
    // mobs graze past the 60 yd combat scan (Camp Narache Plainstriders sit
    // 115+ yd from the camp), so a bot with no destination idles until one
    // wanders in. 150 yd is the follow-leash scale: near enough to walk
    // without crossing zones, far enough to reach the grazing herds.
    float const GRIND_IDLE_FALLBACK_RANGE_YD = 150.0f;

    // Minimum time between fallback scans for one bot. The normal scan runs
    // every value tick; the wider grid visit is only re-tried on this
    // cadence so a stranded pool does not pay it every second.
    std::uint32_t const GRIND_IDLE_FALLBACK_INTERVAL_MS = 10000;

    // Highest bot level the fallback serves. Above this the travel layer owns
    // longer walks (destinations, route gates, death-spot avoidance); the
    // fallback is the starter safety net, not a second travel system.
    std::uint32_t const GRIND_IDLE_FALLBACK_MAX_LEVEL = 5;

    // Whether an idle starter bot may take a fallback grind target right now:
    // masterless, low level, no journey in flight, not fighting, overworld,
    // able to move, and the normal pick came back empty. Owned/hired bots
    // keep today's behaviour (their player decides), and a bot with a travel
    // destination keeps walking it - the travel layer outranks this rule.
    inline bool GrindIdleFallbackAllowed(bool masterlessRandom, std::uint32_t botLevel,
        bool travelTargetActive, bool inCombat, bool inBattleground, bool overworld,
        bool canMoveAround, bool normalPickEmpty)
    {
        return masterlessRandom && botLevel <= GRIND_IDLE_FALLBACK_MAX_LEVEL &&
            !travelTargetActive && !inCombat && !inBattleground && overworld &&
            canMoveAround && normalPickEmpty;
    }

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

    // Unit-flag bits that make a spawn unattackable for a player attacker.
    // Mirrors the core attackability read (WorldObject::IsValidAttackTarget ->
    // Unit::IsTargetable(forAttack, isAttackerPlayer = true)):
    // NOT_ATTACKABLE_1, NOT_SELECTABLE, IMMUNE_TO_PLAYER and NON_ATTACKABLE_2
    // each refuse the attack on their own. IMMUNE_TO_NPC is deliberately not
    // here: it only refuses NPC attackers, and 107 spawned entries carry it
    // (escort quest NPCs and the like) that a player can still hit. Passive
    // wildlife carries none of these bits (verified in tw_world).
    std::uint32_t const GRIND_UNATTACKABLE_UNIT_FLAGS = 0x00000080u /* UNIT_FLAG_NOT_ATTACKABLE_1 */
        | 0x02000000u /* UNIT_FLAG_NOT_SELECTABLE */
        | 0x00000100u /* UNIT_FLAG_IMMUNE_TO_PLAYER */
        | 0x00010000u /* UNIT_FLAG_NON_ATTACKABLE_2 */;

    // Whether a grind destination survives the hostility gate
    // (GrindTravelDestination::IsActive).
    //
    // Hostile entries are always prey. Neutral entries are prey only when they
    // are attackable XP-paying wildlife:
    //   (1) no reputation list (the faction's reputationListID < 0, i.e. the
    //       core IsValidAttackTarget AT_WAR check cannot refuse the attack;
    //       Steamwheedle / Cenarion / Dalaran guards and citizens fail here),
    //   (2) none of the unattackable unit-flag bits above (combat dummies,
    //       quest spirits, ambient pets fail here),
    //   (3) rank 0 (elites, rares and bosses fail here),
    //   plus the existing no-NPC-flag and non-zero-XP-multiplier reads.
    // Friendly entries are never prey.
    //
    // This mirrors the donor mod-playerbots grind filter
    // (src/Ai/Base/Value/GrindTargetValue.cpp: loot-carrying neutrals are kept,
    // only non-hostile NPCs are refused) and the core attackability verdict the
    // arrival-side target selection applies (WorldObject::IsValidAttackTarget,
    // Object.cpp ~5937), so a destination the gate admits is one the bot can
    // actually attack on arrival. The old hostile-only read excluded every
    // neutral starter beast - Thistle Boars / Nightsabers (faction 189/7 read
    // REP_NEUTRAL against a player faction template) - so a level 1-3 bot
    // with no quest destination never held a grind destination and looped
    // QuestTripNoTarget instead of walking to its wolves (issue #393).
    // All inputs are data-only (creature template + faction DBC), so the rule
    // stays safe wherever the destination gate runs.
    inline bool GrindHostilityAllowed(bool hostileToBot, bool friendlyToBot,
        std::uint32_t npcFlags, bool paysXp, bool noReputation, std::uint32_t unitFlags, std::uint32_t rank)
    {
        if (hostileToBot)
            return true;

        if (friendlyToBot)
            return false;

        if (npcFlags != 0 || !paysXp)
            return false;

        if (!noReputation)
            return false;

        if (unitFlags & GRIND_UNATTACKABLE_UNIT_FLAGS)
            return false;

        return rank == 0;
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
