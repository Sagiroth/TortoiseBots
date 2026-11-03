#pragma once

// Derived from jrad7/mod-dungeon-clear
// src/Ai/Dungeon/DungeonClear/Util/DungeonClearMath.cpp:EstimateAggroCount
// @ da0bdfbc (AGPL-3.0-or-later; Copyright (C) 2026+ Jared Wright and
// contributors), rewritten for the vanilla 1.12 core and TortoiseBots'
// plain-struct style. Not a verbatim copy: the donor's pack identity, floor
// tolerance, elite weights and lone-patroller pass are replaced by what this
// core really does (see the rules below).
//
// Pure policy: how many hostile units would come if the tank pulls this one?
//
// The run leader pulls a pack with `pullback <guid>` and finds out only
// afterwards that a neighbouring pack came too (related to turtle-agent#265,
// "the tank does not hold threat": a second pack joins while the first is
// still being picked up; this does not fix the threat order itself). This
// estimates it BEFORE the pull from the rules the core uses to wake a creature
// (src/game/Objects/Creature.cpp, CreatureGroups.cpp, Group/CreatureLinkingMgr.cpp):
//
//   1. The target itself comes, and with it its whole pack: creature_groups
//      members flagged "aggro together" share a groupId, and creature_linking
//      rows are directed edges (a master's aggro wakes a slave, and with the
//      "to" flag a slave's aggro wakes its master).
//   2. Proximity (Creature::GetAttackDistance): a creature wakes when a party
//      member stands inside its aggro radius (a member of the target's pack
//      too, if it stands in reach). The radius is its detection range
//      (20 yd by default) minus the level difference (party level minus creature
//      level, the difference floored at -25), never below min(detection, 5 yd),
//      times Rate.Creature.Aggro. The LOWEST party level gives the widest
//      radius, so that is the one used. The distance is the plain 3D distance
//      to where the party stands when the pull starts (the engage point),
//      widened by `spreadYd` for players drifting during the fight. A creature
//      with no line of sight between it and the engage point (`partySees`), or
//      flagged "no aggro", does not wake this way.
//   3. One family-assistance hop (Creature::CallAssistance): every member of
//      the pull's pack (the target, its pack, and each proximity waker's pack;
//      NOT the creatures an assist drags in) calls creatures of the SAME
//      faction template within the family-assist ring that are not in a fight,
//      not "no assist" and not "no aggro". The ring is the server's
//      CreatureFamilyAssistanceRadius, never under kAggroFamilyAssistMinYd
//      (20 yd: the observed cross-pack wakes). The assisted do not call in
//      turn.
//   4. Pack closure once more: an assisted creature that belongs to a pack
//      drags its pack in (it does not start another assist hop).
//
// APPROXIMATE: the estimate can be high or low.
//   It can be HIGH because: the 3 yd of drift widens every radius, and assist
//   needs the caller to see the assisted creature but only the pulled
//   creature's sight of it (`seen`) is read, not each caller's.
//   It can be LOW because: the call-for-help chain a dragged creature makes
//   along the walk back, a patroller that is somewhere else when the pull
//   starts, auras that change the aggro radius, friendly-but-different faction
//   templates (only an equal template assists) and pack members outside the
//   glue's scan are not modelled.
//
// A lone patroller is the one thing a human waits out: `bodiesIfPatrolsPass`
// is the estimate with every patroller that is in no pack (and not the target)
// left out, so the caller can tell "a patrol is the whole reason".
//
// Core-free, so tools/test_pull_aggro_estimate_policy.cpp includes it directly.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ai
{
    // CreatureFamilyAssistanceRadius in the core's mangosd.conf (default 10).
    inline constexpr float kAggroFamilyAssistYd = 10.0f;
    // The ring the estimate never goes under. The cross-pack wakes on the Zul'Gurub terrace are spawn pairs 11.0-19.5
    // yd apart, and the pack they took woke from its own spawn edge, not from the pulled creature: the server's 10 yd
    // assistance radius underestimates what the 20 yd aggro ring takes.
    inline constexpr float kAggroFamilyAssistMinYd = 20.0f;
    // Creature::m_detectionDistance default when the template has none.
    inline constexpr float kAggroDetectionYd = 20.0f;
    // MAX_LEVEL_DIFF_FOR_AGGRO_RANGE.
    inline constexpr int kAggroMaxLevelDiff = 25;
    // Smallest aggro radius, as the core floors it: min(detection, 5 yd).
    inline constexpr float kAggroMinReachYd = 5.0f;

    // One living, hostile creature near the pull.
    struct AggroMob
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        std::uint32_t level = 0;
        float detectionYd = kAggroDetectionYd; // Creature::GetDetectionRange (0 = never wakes by proximity)
        std::uint32_t faction = 0;             // faction template id (family assistance needs an equal one)
        std::uint32_t groupId = 0;             // creature_groups "aggro together": equal non-zero ids pull as one
        bool noAggro = false;                  // never wakes by proximity or assist (CREATURE_FLAG_EXTRA_NO_AGGRO)
        bool noAssist = false;                 // never joins by assist (CREATURE_FLAG_EXTRA_NO_ASSIST)
        bool partySees = true;                 // line of sight between it and the engage point (proximity needs it)
        bool seen = true;                      // line of sight between it and the pulled creature (stands in for the
                                               // caller's sight of it, which assist needs)
        bool patroller = false;                // a waypoint mover
    };

    // A directed creature_linking edge: when `from` is aggroed, `to` is too.
    struct AggroLink
    {
        std::size_t from = 0, to = 0;
    };

    struct AggroEstimateParams
    {
        float engageX = 0.0f, engageY = 0.0f, engageZ = 0.0f; // where the party stands as the pull starts
        std::uint32_t lowestPartyLevel = 60;                  // the widest aggro radius wins
        float spreadYd = 3.0f;                                // players drifting during the fight
        float aggroRate = 1.0f;                               // Rate.Creature.Aggro
        float familyAssistYd = kAggroFamilyAssistYd;
        std::vector<AggroLink> links;
    };

    struct AggroEstimate
    {
        std::uint32_t bodies = 0;            // all creatures that come, the target included
        std::uint32_t joiners = 0;           // bodies - 1
        std::uint32_t bodiesIfPatrolsPass = 0; // the same with the lone patrollers left out
        bool patrolIsTheCause = false;       // a patrol alone makes the difference (see JoinersOverCeiling)
    };

    // Creature::GetAttackDistance for one creature against a party member of
    // `partyLevel`, the aggro rate included.
    inline float AggroRadius(AggroMob const& mob, std::uint32_t partyLevel, float aggroRate)
    {
        if (mob.noAggro || mob.detectionYd < 1.0f)
            return 0.0f;
        int diff = static_cast<int>(partyLevel) - static_cast<int>(mob.level);
        if (diff < -kAggroMaxLevelDiff)
            diff = -kAggroMaxLevelDiff;
        float reach = mob.detectionYd - static_cast<float>(diff);
        float const floorYd = std::min(mob.detectionYd, kAggroMinReachYd);
        if (reach < floorYd)
            reach = floorYd;
        return reach * aggroRate;
    }

    namespace detail
    {
        inline float Dist3(float ax, float ay, float az, float bx, float by, float bz)
        {
            float const dx = ax - bx, dy = ay - by, dz = az - bz;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }

        // Closes `in` over the packs: equal non-zero group ids, and the directed
        // links, until nothing is added (a chain of links is one authored pack).
        inline void PackClosure(std::vector<AggroMob> const& mobs, std::vector<AggroLink> const& links,
            std::vector<char>& in)
        {
            bool grew = true;
            while (grew)
            {
                grew = false;
                for (std::size_t i = 0; i < mobs.size(); ++i)
                {
                    if (!in[i])
                        continue;
                    if (mobs[i].groupId != 0)
                        for (std::size_t k = 0; k < mobs.size(); ++k)
                            if (!in[k] && mobs[k].groupId == mobs[i].groupId)
                            {
                                in[k] = 1;
                                grew = true;
                            }
                }
                for (AggroLink const& l : links)
                    if (in[l.from] && !in[l.to])
                    {
                        in[l.to] = 1;
                        grew = true;
                    }
            }
        }

        inline std::uint32_t Count(std::vector<AggroMob> const& mobs, std::size_t target,
            AggroEstimateParams const& p, bool leavePatrolsOut)
        {
            std::size_t const n = mobs.size();
            if (target >= n)
                return 0;
            // A lone patroller (no pack, not the target) is treated as walked off.
            auto absent = [&](std::size_t i)
            {
                return leavePatrolsOut && i != target && mobs[i].patroller && mobs[i].groupId == 0;
            };
            std::vector<AggroLink> links;
            for (AggroLink const& l : p.links)
                if (l.from < n && l.to < n && !absent(l.from) && !absent(l.to))
                    links.push_back(l);
            std::vector<char> in(n, 0);
            in[target] = 1;
            // 1. Proximity wakers: the callers of the assist hop, with the target.
            //    A member of the target's pack that stands in reach wakes by
            //    itself too, and calls.
            for (std::size_t i = 0; i < n; ++i)
            {
                if (absent(i) || !mobs[i].partySees)
                    continue;
                float const reach = AggroRadius(mobs[i], p.lowestPartyLevel, p.aggroRate);
                if (reach > 0.0f &&
                    Dist3(mobs[i].x, mobs[i].y, mobs[i].z, p.engageX, p.engageY, p.engageZ) <= reach + p.spreadYd)
                    in[i] = 1;
            }
            // 1b. The pull's pack (the target's and each waker's) is known before the assist hop: every member of it
            //     calls for help, not only the pulled creature. The creatures the call takes do not call in turn.
            PackClosure(mobs, links, in);
            std::vector<char> const caller = in;
            // 2. One assist hop from the callers only (the assisted do not call).
            float const assistYd = std::max(p.familyAssistYd, kAggroFamilyAssistMinYd);
            for (std::size_t j = 0; j < n; ++j)
            {
                if (in[j] || absent(j) || !mobs[j].seen || mobs[j].noAssist || mobs[j].noAggro)
                    continue;
                for (std::size_t i = 0; i < n; ++i)
                {
                    if (!caller[i] || mobs[i].faction != mobs[j].faction)
                        continue;
                    if (Dist3(mobs[i].x, mobs[i].y, mobs[i].z, mobs[j].x, mobs[j].y, mobs[j].z) <= assistYd)
                    {
                        in[j] = 1;
                        break;
                    }
                }
            }
            // 3. The pack of everything that woke comes along (the target's, a waker's,
            //    an assisted creature's), without another hop.
            PackClosure(mobs, links, in);
            std::uint32_t count = 0;
            for (std::size_t i = 0; i < n; ++i)
                if (in[i])
                    ++count;
            return count;
        }
    }

    // `mobs` holds the target and every hostile near it (all the living, not
    // fighting ones); `target` indexes the one the tank pulls. An out-of-range
    // target gives an all-zero estimate.
    inline AggroEstimate EstimateAggro(std::vector<AggroMob> const& mobs, std::size_t target,
        AggroEstimateParams const& params)
    {
        AggroEstimate e;
        e.bodies = detail::Count(mobs, target, params, false);
        e.joiners = e.bodies > 0 ? e.bodies - 1 : 0;
        e.bodiesIfPatrolsPass = detail::Count(mobs, target, params, true);
        return e;
    }

    // The run leader's question: do more joiners than `ceiling` come? 0 for
    // `ceiling` is off. `patrolIsTheCause` of the estimate is set when the same
    // pull would stay at or under the ceiling once the lone patrols are gone.
    inline bool JoinersOverCeiling(AggroEstimate& e, std::uint32_t ceiling)
    {
        if (ceiling == 0 || e.joiners <= ceiling)
        {
            e.patrolIsTheCause = false;
            return false;
        }
        std::uint32_t const ifPass = e.bodiesIfPatrolsPass > 0 ? e.bodiesIfPatrolsPass - 1 : 0;
        e.patrolIsTheCause = ifPass <= ceiling;
        return true;
    }
}
