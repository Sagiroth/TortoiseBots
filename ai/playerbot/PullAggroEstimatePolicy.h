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
// THE RING ESTIMATE (EstimateAggroRing, AiPlayerbot.DungeonRunAggroWalkRing) models
// what the walk to the pull wakes, not only where the party stands:
//   5. Proximity to every sample of the party's walk to the pull, and to the stand
//      and the fight point, each widened by its own spread (3 yd party, 10 yd raid
//      for the stand and the fight point, 1.5 yd for a camp pull's tank-only walk).
//   6. Call for help: every creature already in the set calls every same-faction
//      creature within max(CreatureFamilyAssistanceRadius, ringYd) of ITS OWN
//      position, in line of sight and within kAggroFloorDz vertically, for up to
//      kAggroRingHops hops (hop 2's callers are what hop 1 added).
//   7. The drag sweep of a camp pull: the engaged set is moved along the drag
//      samples and every creature within ringYd of a sample is called once.
// The measured reason for the 20 yd ring is the 11.0-19.5 yd spawn pairs of the
// Arlokk terrace that wake each other while the core's assist radius is 10.
//
// Core-free, so tools/test_pull_aggro_estimate_policy.cpp includes it directly.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
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
    // The ring estimate: how far a call for help carries (AiPlayerbot.DungeonRunAggroRingYd; the
    // measured wakes were 11.0-19.5 yd while the core's own assist radius is 10), how many hops
    // (a constant: three or more over-counts the whole terrace), and how far vertically a call
    // reaches (the one Arlokk hop that came crossed dz 11).
    inline constexpr float kAggroRingYd = 20.0f;
    inline constexpr std::uint32_t kAggroRingHops = 2;
    inline constexpr float kAggroFloorDz = 12.0f;
    // How close to the pulled creature the tank gets before it pulls back (the glue's engage point).
    inline constexpr float kAggroEngageYd = 30.0f;
    // The most samples of a party walk an estimate reads (the spacing grows with the path so a long
    // walk still fits, and its end is always kept).
    inline constexpr std::size_t kAggroWalkSamples = 64;
    // The sight budget of one estimate (one estimate per row, not per tick): past it the
    // remaining pairs count as seen, so a big room goes high, never slow.
    inline constexpr std::uint32_t kAggroRayBudget = 400;

    // One living, hostile creature near the pull.
    struct AggroMob
    {
        std::uint64_t guid = 0;                // carried, not used by the rules
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
        bool normalRank = false;               // CREATURE_ELITE_NORMAL: no body of the elite count
    };

    // A directed creature_linking edge: when `from` is aggroed, `to` is too.
    struct AggroLink
    {
        std::size_t from = 0, to = 0;
    };

    struct AggroPoint
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };

    // Where the party and the tank go around a pull (the glue samples the walk and
    // the drag; the pure rules only read the points).
    struct AggroGeometry
    {
        std::vector<AggroPoint> walk;     // the party's walk to the pull, every 2 yd
        float walkSpreadYd = 3.0f;
        std::vector<AggroPoint> tankWalk; // a camp pull's walk-out and tag spot (tank only)
        float tankSpreadYd = 1.5f;
        AggroPoint stand;                 // where the pull goes out from
        float standSpreadYd = 3.0f;       // 10 for a raid (ai::kSpreadSettledDistance)
        bool hasFight = false;
        AggroPoint fight;                 // the camp of a camp pull, else the stand
        std::vector<AggroPoint> drag;     // a camp pull's drag polyline, every 4 yd
    };

    // Where the tank stands as it pulls back: the party's spot, or kAggroEngageYd short of the creature
    // along the line creature -> party. The pull row and the look-ahead share it, so both read one geometry.
    inline AggroPoint EngagePoint(float fromX, float fromY, float fromZ, AggroPoint const& target, float awayYd)
    {
        float const k = awayYd > kAggroEngageYd ? kAggroEngageYd / awayYd : 1.0f;
        return AggroPoint{target.x + (fromX - target.x) * k, target.y + (fromY - target.y) * k,
            target.z + (fromZ - target.z) * k};
    }

    namespace detail
    {
        inline float PathDist(AggroPoint const& a, AggroPoint const& b)
        {
            float const dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }
    }

    // The party's walk, sampled every `stepYd` of its polyline (the first point is the party's spot): the
    // spacing grows with the path so the whole walk fits `maxSamples`, and the last point is always kept.
    // A walk that would not fit at 2 yd is what read past its end before (a 150 yd walk stopped at 126).
    inline std::vector<AggroPoint> SampleAggroWalk(std::vector<AggroPoint> const& path,
        std::size_t maxSamples = kAggroWalkSamples, float stepYd = 2.0f)
    {
        std::vector<AggroPoint> out;
        if (path.empty() || maxSamples == 0)
            return out;
        out.push_back(path.front());
        float total = 0.0f;
        for (std::size_t i = 1; i < path.size(); ++i)
            total += detail::PathDist(path[i - 1], path[i]);
        if (total < 0.0001f || maxSamples < 2)
            return out;
        float const spacing = std::max(stepYd, total / static_cast<float>(maxSamples - 1));
        float travelled = 0.0f;
        float next = spacing;
        for (std::size_t i = 1; i < path.size(); ++i)
        {
            AggroPoint const& from = path[i - 1];
            AggroPoint const& to = path[i];
            float const seg = detail::PathDist(from, to);
            if (seg < 0.0001f)
                continue;
            float const end = travelled + seg;
            while (next < end - 0.0001f && out.size() + 1 < maxSamples)
            {
                float const t = (next - travelled) / seg;
                out.push_back(AggroPoint{from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t,
                    from.z + (to.z - from.z) * t});
                next += spacing;
            }
            travelled = end;
        }
        out.push_back(path.back());
        return out;
    }

    // Why one creature comes (the member list's reason).
    enum class AggroWhy : std::uint8_t { Target, Pack, Walk, TankWalk, Stand, Fight, Ring1, Ring2, Drag };

    // The reason's word, for the detail log line (the estimate's note carries guids only).
    inline const char* AggroWhyName(AggroWhy why)
    {
        switch (why)
        {
            case AggroWhy::Target: return "target";
            case AggroWhy::Pack: return "pack";
            case AggroWhy::Walk: return "walk";
            case AggroWhy::TankWalk: return "tank walk";
            case AggroWhy::Stand: return "stand";
            case AggroWhy::Fight: return "fight point";
            case AggroWhy::Ring1: return "ring 1";
            case AggroWhy::Ring2: return "ring 2";
            case AggroWhy::Drag: return "drag";
        }
        return "?";
    }

    struct AggroMember
    {
        std::size_t index = 0; // into the mobs list
        AggroWhy why = AggroWhy::Target;
    };

    struct AggroEstimateParams
    {
        float engageX = 0.0f, engageY = 0.0f, engageZ = 0.0f; // where the party stands as the pull starts
        std::uint32_t lowestPartyLevel = 60;                  // the widest aggro radius wins
        float spreadYd = 3.0f;                                // players drifting during the fight
        float aggroRate = 1.0f;                               // Rate.Creature.Aggro
        float familyAssistYd = kAggroFamilyAssistYd;
        std::vector<AggroLink> links;
        // The ring estimate: a call for help carries max(familyAssistYd, ringYd), for
        // ringHops hops, and only within floorDz of the caller vertically.
        float ringYd = kAggroRingYd;
        std::uint32_t ringHops = kAggroRingHops;
        float floorDz = kAggroFloorDz;
        // Line of sight, when the caller can read it (a null callback: always seen).
        // mobSees: from the creature to a point of the geometry; mobsSee: from one
        // creature to another (the caller to the called).
        std::function<bool(std::size_t mob, AggroPoint const&)> mobSees;
        std::function<bool(std::size_t from, std::size_t to)> mobsSee;
    };

    struct AggroEstimate
    {
        std::uint32_t bodies = 0;            // all creatures that come, the target included
        std::uint32_t bodiesElite = 0;       // ... the ones not CREATURE_ELITE_NORMAL (the automatic cc's bodies)
        std::uint32_t joiners = 0;           // bodies - 1
        std::uint32_t bodiesIfPatrolsPass = 0; // the same with the lone patrollers left out
        bool patrolIsTheCause = false;       // a patrol alone makes the difference (see JoinersOverCeiling)
        std::vector<AggroMember> members;    // EstimateAggroRing only: every body with its reason, insertion order
        bool losBudgetSpent = false;         // ... the sight budget ran out: the count is high side
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
            AggroEstimateParams const& p, bool leavePatrolsOut, std::uint32_t* elite = nullptr)
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
                {
                    ++count;
                    if (elite && !mobs[i].normalRank)
                        ++*elite;
                }
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
        e.bodies = detail::Count(mobs, target, params, false, &e.bodiesElite);
        e.joiners = e.bodies > 0 ? e.bodies - 1 : 0;
        e.bodiesIfPatrolsPass = detail::Count(mobs, target, params, true);
        return e;
    }

    namespace detail
    {
        // The sight budget of one estimate: kAggroRayBudget rays, then every pair
        // counts as seen (high side) and the estimate says the budget was spent.
        struct SightBudget
        {
            std::uint32_t rays = 0;
            bool spent = false;

            bool Out()
            {
                if (rays >= kAggroRayBudget)
                {
                    spent = true;
                    return true;
                }
                ++rays;
                return false;
            }
        };

        // A null callback reads no ray at all: only a real sight call spends the budget.
        inline bool MobSees(SightBudget& budget, AggroEstimateParams const& p, std::size_t mob, AggroPoint const& point)
        {
            if (!p.mobSees)
                return true;
            return budget.Out() || p.mobSees(mob, point);
        }

        inline bool MobsSee(SightBudget& budget, AggroEstimateParams const& p, std::size_t from, std::size_t to)
        {
            if (!p.mobsSee)
                return true;
            return budget.Out() || p.mobsSee(from, to);
        }

        inline float PointDist(AggroMob const& m, AggroPoint const& p)
        {
            return Dist3(m.x, m.y, m.z, p.x, p.y, p.z);
        }

        // The pack closure, marking what it adds as Pack (`added`, when given, gets every index it added).
        inline void PackClosureMark(std::vector<AggroMob> const& mobs, std::vector<AggroLink> const& links,
            std::vector<char>& in, std::vector<AggroMember>* members, std::vector<std::size_t>* added = nullptr)
        {
            auto add = [&](std::size_t i)
            {
                in[i] = 1;
                if (members)
                    members->push_back(AggroMember{i, AggroWhy::Pack});
                if (added)
                    added->push_back(i);
            };
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
                                add(k);
                                grew = true;
                            }
                }
                for (AggroLink const& l : links)
                    if (in[l.from] && !in[l.to])
                    {
                        add(l.to);
                        grew = true;
                    }
            }
        }

        inline std::uint32_t RingCount(std::vector<AggroMob> const& mobs, std::size_t target,
            AggroGeometry const& g, AggroEstimateParams const& p, SightBudget& budget,
            std::vector<AggroMember>* members, std::uint32_t* elite = nullptr)
        {
            std::size_t const n = mobs.size();
            if (target >= n)
                return 0;
            std::vector<AggroLink> links;
            for (AggroLink const& l : p.links)
                if (l.from < n && l.to < n)
                    links.push_back(l);
            std::vector<char> in(n, 0);
            auto add = [&](std::size_t i, AggroWhy why)
            {
                if (in[i])
                    return false;
                in[i] = 1;
                if (members)
                    members->push_back(AggroMember{i, why});
                return true;
            };
            add(target, AggroWhy::Target);
            PackClosureMark(mobs, links, in, members);

            // 5. Proximity to the walk, the tank's walk, the stand and the fight
            //    point. Only the nearest sample inside reach (plus its spread) is
            //    ray-tested: one ray per creature, not one a sample.
            struct Spot
            {
                AggroPoint point;
                float spread;
                AggroWhy why;
            };
            std::vector<Spot> spots;
            spots.reserve(g.walk.size() + g.tankWalk.size() + 2);
            for (AggroPoint const& w : g.walk)
                spots.push_back(Spot{w, g.walkSpreadYd, AggroWhy::Walk});
            for (AggroPoint const& w : g.tankWalk)
                spots.push_back(Spot{w, g.tankSpreadYd, AggroWhy::TankWalk});
            spots.push_back(Spot{g.stand, g.standSpreadYd, AggroWhy::Stand});
            if (g.hasFight)
                spots.push_back(Spot{g.fight, g.standSpreadYd, AggroWhy::Fight});
            for (std::size_t i = 0; i < n; ++i)
            {
                if (in[i] || mobs[i].noAggro)
                    continue;
                float const reach = AggroRadius(mobs[i], p.lowestPartyLevel, p.aggroRate);
                if (reach <= 0.0f)
                    continue;
                Spot const* best = nullptr;
                float bestDist = 0.0f;
                for (Spot const& s : spots)
                {
                    float const d = PointDist(mobs[i], s.point);
                    if (d <= reach + s.spread && (!best || d < bestDist))
                    {
                        best = &s;
                        bestDist = d;
                    }
                }
                if (best && MobSees(budget, p, i, best->point))
                    add(i, best->why);
            }
            PackClosureMark(mobs, links, in, members);

            // 6. Call for help: every member calls its same-faction neighbours, one
            //    hop at a time, and each hop's calls come with their packs.
            float const ring = std::max(p.familyAssistYd, p.ringYd);
            std::vector<std::size_t> callers;
            for (std::size_t i = 0; i < n; ++i)
                if (in[i])
                    callers.push_back(i);
            for (std::uint32_t hop = 1; hop <= p.ringHops && !callers.empty(); ++hop)
            {
                AggroWhy const reason = hop == 1 ? AggroWhy::Ring1 : AggroWhy::Ring2;
                std::vector<std::size_t> called;
                for (std::size_t from : callers)
                    for (std::size_t i = 0; i < n; ++i)
                    {
                        if (in[i] || mobs[i].noAssist || mobs[i].noAggro ||
                            mobs[i].faction != mobs[from].faction)
                            continue;
                        if (std::fabs(mobs[i].z - mobs[from].z) > p.floorDz)
                            continue;
                        if (Dist3(mobs[i].x, mobs[i].y, mobs[i].z, mobs[from].x, mobs[from].y, mobs[from].z) > ring)
                            continue;
                        if (!MobsSee(budget, p, from, i))
                            continue;
                        if (add(i, reason))
                            called.push_back(i);
                    }
                // Hop 2's callers are what hop 1 added, the pack members it dragged in
                // included (Arlokk: G12 calls G15, G15's pack mate is the only bridge on).
                std::vector<std::size_t> packed;
                PackClosureMark(mobs, links, in, members, &packed);
                called.insert(called.end(), packed.begin(), packed.end());
                callers.swap(called);
            }

            // 7. The drag sweep: every drag sample calls what stands within ringYd of
            //    it (the call a dragged creature makes along the way back).
            for (AggroPoint const& sample : g.drag)
            {
                for (std::size_t i = 0; i < n; ++i)
                {
                    // The dragged pack calls its own faction template on, as a ring hop does.
                    if (in[i] || mobs[i].noAssist || mobs[i].noAggro || mobs[i].faction != mobs[target].faction)
                        continue;
                    if (PointDist(mobs[i], sample) > ring || std::fabs(mobs[i].z - sample.z) > p.floorDz)
                        continue;
                    if (!MobSees(budget, p, i, sample))
                        continue;
                    add(i, AggroWhy::Drag);
                }
                PackClosureMark(mobs, links, in, members);
            }

            std::uint32_t count = 0;
            for (std::size_t i = 0; i < n; ++i)
                if (in[i])
                {
                    ++count;
                    if (elite && !mobs[i].normalRank)
                        ++*elite;
                }
            return count;
        }
    }

    // The ring estimate: the same question as EstimateAggro, with the walk, the
    // stand and the fight point, the calls for help of every member and the drag
    // sweep. `members` lists every body with its reason, in insertion order.
    inline AggroEstimate EstimateAggroRing(std::vector<AggroMob> const& mobs, std::size_t target,
        AggroGeometry const& geometry, AggroEstimateParams const& params)
    {
        AggroEstimate e;
        detail::SightBudget budget;
        e.bodies = detail::RingCount(mobs, target, geometry, params, budget, &e.members, &e.bodiesElite);
        e.joiners = e.bodies > 0 ? e.bodies - 1 : 0;
        // No second pass with the patrols left out: the ring path never waits a patrol out (a
        // patroller counts at its position), so its body count and the sight budget it spent are
        // the one read. `bodiesIfPatrolsPass` stays 0.
        e.losBudgetSpent = budget.spent;
        return e;
    }

    // The row's ceiling in bodies: the plan's own (the pull row's `item`, a
    // deliberately sized pull) wins, else the config ceiling in joiners + 1.
    // 0: no ceiling at all.
    inline std::uint32_t PullCeilingBodies(std::uint32_t planBodies, std::uint32_t configJoiners)
    {
        return planBodies ? planBodies : (configJoiners ? configJoiners + 1 : 0);
    }

    // One creature the look-ahead's pull row lists.
    struct AheadPick
    {
        std::uint32_t entry = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };

    // The look-ahead's pick: among the pull row's own entries only, the best kill_first rank
    // first, then the nearest to the walk's end point. npos (no listed creature): the estimate
    // is not read; never an unrelated hostile.
    inline std::size_t PickAheadTarget(std::vector<AheadPick> const& mobs, std::vector<std::uint32_t> const& entries,
        std::vector<std::uint32_t> const& killFirst, float x, float y, float z)
    {
        std::size_t const npos = static_cast<std::size_t>(-1);
        std::size_t chosen = npos;
        int bestRank = -1;
        float bestDist = 0.0f;
        for (std::size_t i = 0; i < mobs.size(); ++i)
        {
            if (std::find(entries.begin(), entries.end(), mobs[i].entry) == entries.end())
                continue;
            int rank = -1;
            auto const it = std::find(killFirst.begin(), killFirst.end(), mobs[i].entry);
            if (it != killFirst.end())
                rank = static_cast<int>(it - killFirst.begin());
            float const d = detail::Dist3(mobs[i].x, mobs[i].y, mobs[i].z, x, y, z);
            bool better = chosen == npos;
            if (!better)
            {
                bool const listed = rank >= 0;
                bool const bestListed = bestRank >= 0;
                if (listed != bestListed)
                    better = listed;
                else if (listed && rank != bestRank)
                    better = rank < bestRank;
                else
                    better = d < bestDist;
            }
            if (better)
            {
                chosen = i;
                bestRank = rank;
                bestDist = d;
            }
        }
        return chosen;
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
