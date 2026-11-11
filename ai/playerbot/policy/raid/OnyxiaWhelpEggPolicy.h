#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

// Pure decision rules for the optional Onyxia whelp and egg tactics
// (AiPlayerbot.OnyxiaWhelpsAndEggs, on by default).
// Donor behaviour: mod-playerbots @ 79bd428, src/Ai/Raid/Ony/OnyTriggers.cpp
// (RaidOnyxiaWhelpsSpawnTrigger: boss airborne, non-healers go for whelps;
// OnyxiaAvoidEggsTrigger: bot inside the egg cave) and OnyActions.cpp
// (RaidOnyxiaKillWhelpsAction: attack a whelp unless already on one;
// OnyxiaAvoidEggsAction: walk back out of the cave).
// Adapted to Turtle 1.18.1 data (tw_world): Onyxian Whelp 11262, the
// trap game object "Onyxia Egg" 176511 (type 6, radius 10, spell 17647)
// and the whelp spawn points of core boss_onyxia.cpp. No core includes:
// callers translate game state into plain inputs so the rules stay
// testable in tools/test_onyxia_whelp_egg_policy.cpp.

namespace ai
{
    constexpr std::uint32_t kOnyxiaMapId = 249;
    constexpr std::uint32_t kOnyxianWhelpEntry = 11262;
    constexpr std::uint32_t kOnyxiaEggEntry = 176511;

    // Egg trap radius (gameobject_template data2 of 176511) plus a margin
    // so a bot walking past is never inside the trigger circle.
    constexpr float kOnyxiaEggTriggerRadius = 10.0f;
    constexpr float kOnyxiaEggMargin = 2.0f;
    constexpr float kOnyxiaEggDangerRadius = kOnyxiaEggTriggerRadius + kOnyxiaEggMargin;

    // A whelp further than this from the bot is somebody else's problem.
    constexpr float kOnyxiaWhelpEngageRange = 40.0f;

    // The egg fields lie west of the arena (spawn rows of 176511 span
    // x -115.2..-32.0, y -279.3..-151.4). Padded by the danger radius:
    // outside this box a bot cannot be inside any egg's circle, so no
    // game object search is needed there.
    constexpr float kOnyxiaEggZoneMinX = -128.0f;
    constexpr float kOnyxiaEggZoneMaxX = -20.0f;
    constexpr float kOnyxiaEggZoneMinY = -292.0f;
    constexpr float kOnyxiaEggZoneMaxY = -139.0f;

    inline bool IsInOnyxiaEggZone(float x, float y)
    {
        return x >= kOnyxiaEggZoneMinX && x <= kOnyxiaEggZoneMaxX &&
               y >= kOnyxiaEggZoneMinY && y <= kOnyxiaEggZoneMaxY;
    }

    // Distance 2D between two points (egg traps check a circle).
    inline float OnyxiaDist2d(float x1, float y1, float x2, float y2)
    {
        const float dx = x1 - x2;
        const float dy = y1 - y2;
        return std::sqrt(dx * dx + dy * dy);
    }

    inline bool IsInsideOnyxiaEggDanger(float distance2d)
    {
        return distance2d < kOnyxiaEggDangerRadius;
    }

    // Exit points on the arena side of the two egg caves (donor
    // OnyxiaAvoidEggsAction targets, Onyxia's lair map coordinates).
    struct OnyxiaEggExit { float x, y, z; };
    constexpr OnyxiaEggExit kOnyxiaEggExits[2] =
    {
        { -10.0f, -180.0f, -87.0f }, // north cave mouth
        { -16.0f, -250.0f, -88.0f }, // south cave mouth
    };

    // Index of the exit nearest to the bot.
    inline int NearestOnyxiaEggExit(float x, float y)
    {
        const float d0 = OnyxiaDist2d(x, y, kOnyxiaEggExits[0].x, kOnyxiaEggExits[0].y);
        const float d1 = OnyxiaDist2d(x, y, kOnyxiaEggExits[1].x, kOnyxiaEggExits[1].y);
        return d0 <= d1 ? 0 : 1;
    }

    // Egg retreat decision. The action never fires for a bot that is not
    // inside the danger circle of an unbroken egg.
    inline bool ShouldAvoidOnyxiaEggs(bool featureOn, bool onOnyxiaMap, bool inEggZone, bool eggInDanger)
    {
        return featureOn && onOnyxiaMap && inEggZone && eggInDanger;
    }

    // Whelp duty: the AoE-capable ranged classes. Tanks and healers
    // keep their own duty and melee cannot do much against a flying boss
    // anyway but keeps its generic target choice.
    // classId values are the 1.12 Classes enum (Hunter 3, Mage 8, Warlock 9,
    // Shaman 7, Druid 11).
    enum OnyxiaWhelpClass : std::uint8_t
    {
        kWhelpClassShaman = 7,
        kWhelpClassHunter = 3,
        kWhelpClassMage = 8,
        kWhelpClassWarlock = 9,
        kWhelpClassDruid = 11,
    };

    inline bool IsOnyxiaWhelpHunterClass(std::uint8_t classId)
    {
        switch (classId)
        {
            case kWhelpClassHunter:  // Multi-Shot
            case kWhelpClassMage:    // Arcane Explosion, Blizzard
            case kWhelpClassWarlock: // Rain of Fire, Hellfire
            case kWhelpClassShaman:  // Chain Lightning (ranged specs)
            case kWhelpClassDruid:   // Hurricane (ranged specs)
                return true;
            default:
                return false;
        }
    }

    inline bool IsOnyxiaWhelpHunter(std::uint8_t classId, bool isTank, bool isHealer, bool isRanged)
    {
        return !isTank && !isHealer && isRanged && IsOnyxiaWhelpHunterClass(classId);
    }

    // AiPlayerbot.DungeonRunAddCrew: the whelp duty once the row's switch unit is dead (state from the "add crew"
    // value). Waiting keeps today's duty; Crew takes it; NotCrew leaves it. So the whelp trigger, the kill action and
    // the whelp hold follow the crew, and the melee and tanks that were never hunters join it.
    inline bool OnyxiaWhelpDutyFor(bool todayDuty, std::uint8_t crewState)
    {
        return crewState == 1 ? true : crewState == 2 ? false : todayDuty;
    }

    // A ranged damage dealer off the crew on a whelp while she hovers goes back to her (not on an explicit order). The
    // crew has the whelps, so a bot past it that stays would leave her to the melee, who cannot reach her in the air.
    inline bool ShouldReturnToOnyxia(bool keyOn, std::uint8_t crewState, bool rangedDps, bool bossAirborne,
                                     bool onWhelp, bool explicitOrder, bool bossValid)
    {
        return keyOn && crewState == 2 && rangedDps && bossAirborne && onWhelp && !explicitOrder && bossValid;
    }

    struct OnyxiaPoint { float x, y; };

    // True when (x, y) lies inside the trap circle (10yd, no margin) of any
    // listed egg. The egg list is the bot's own cached egg search, never the
    // zone box. The whelp spawn points sit 11.5 and 11.9 yd from the nearest
    // egg (tw_world rows of 176511), so a margin here would hide them.
    inline bool IsNearAnyOnyxiaEgg(float x, float y, OnyxiaPoint const* eggs, std::size_t count)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            if (OnyxiaDist2d(x, y, eggs[i].x, eggs[i].y) < kOnyxiaEggTriggerRadius)
                return true;
        }
        return false;
    }

    // Whether a whelp is a legal pick: alive, in range, and not standing
    // within the trap circle of an unbroken egg (chasing it would walk the
    // bot over the eggs). Whelps at the cave mouths, in the arena and at the
    // phase landing spots are 11+ yd from any egg and stay pickable.
    inline bool IsOnyxiaWhelpPickable(bool alive, float distance, bool whelpNearEgg)
    {
        return alive && distance <= kOnyxiaWhelpEngageRange && !whelpNearEgg;
    }

    // Whelp hunt decision: phase 2 only (boss airborne), feature on, the
    // bot is on whelp duty, no whelp is its target yet, and a whelp is
    // available. An explicit order from the master always wins.
    inline bool ShouldHuntOnyxiaWhelps(bool featureOn, bool onOnyxiaMap, bool bossAirborne, bool assigned,
                                       bool hasExplicitOrder, bool alreadyOnWhelp, bool whelpAvailable)
    {
        return featureOn && onOnyxiaMap && bossAirborne && assigned && !hasExplicitOrder &&
               !alreadyOnWhelp && whelpAvailable;
    }

    // Keep the whelp target against the generic assist re-pick: true when
    // the bot is on whelp duty during the airborne phase and its target is
    // a live whelp.
    inline bool ShouldHoldOnyxiaWhelpTarget(bool featureOn, bool onOnyxiaMap, bool bossAirborne, bool assigned,
                                            bool hasExplicitOrder, bool targetIsLiveWhelp)
    {
        return featureOn && onOnyxiaMap && bossAirborne && assigned && !hasExplicitOrder && targetIsLiveWhelp;
    }
}
