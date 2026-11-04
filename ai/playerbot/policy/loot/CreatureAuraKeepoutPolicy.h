#pragma once

// Pure rules for the creature aura keep-out (AiPlayerbot.AvoidCreatureAuras).
// Some creatures carry a permanent damaging aura that pulses around them
// whether or not they are in combat. A bot must not stand, path or chase
// into that circle. The table lists each such creature by map and entry with
// the aura that marks it live, the aura's pulse radius and the keep-out
// radius the bot steps out from (pulse plus a drift margin).
// A row whose auraSpellId is 0 lists a creature that is itself the hazard and
// carries no marking aura (Naxxramas' Living Poison): it counts whenever it is
// alive, see CreatureAuraKeepoutMarkedLive.
//
// Behaviour reimplemented from the T-imothy tortoise-wow registry
// (modules/mod-dungeon-clear/.../Data/DcHazardRegistry.cpp, 0fb4d7c): every
// role stays out of the Creeping Sludge, and the keep-out is the 5 yd pulse
// plus 3 yd. No code copied. Turtle data: creature_template 12222 auras
// '22638'; spell 22638 is a 2000 ms periodic trigger of 22595, school damage
// at EffectRadiusIndex 8 (5 yd).
// Callers own game state (aura check, distances, the explicit order); this
// header owns the table and the decisions so tools/test_creature_aura_keepout_policy.cpp
// can test them. No core includes.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

namespace ai
{
    struct CreatureAuraKeepout
    {
        uint32_t mapId;
        uint32_t creatureEntry;
        uint32_t auraSpellId;
        float pulseRadius;   // radius of the damaging pulse (yd)
        float keepoutRadius; // bot steps out when this close (yd)
        // A tank may stand in this creature's ring while it fights (and its
        // chase is never held at it): true where the creature only chips
        // (sludge, 200 per tick), false where one hit is lethal (Explode).
        bool tankExempt;
    };

    // Extra yards the movement hazard reaches past the keep-out radius, so a
    // bot that just stepped out is not chased or followed back to the rim.
    constexpr float kCreatureAuraHazardMargin = 1.0f;

    // Seconds a registered movement hazard lives; re-added every trigger tick
    // while the creature is near, so it lapses soon after the creature dies,
    // leaves or the bot moves on.
    constexpr unsigned kCreatureAuraHazardSeconds = 3;

    constexpr CreatureAuraKeepout kCreatureAuraKeepouts[] = {
        // Maraudon, Creeping Sludge, Poison Shock (180-220 nature every 2 s, 5 yd).
        { 349, 12222, 22638, 5.0f, 8.0f, true },
        // Naxxramas, Living Poison (#906). Listed without an aura: its
        // creature_template (16027) carries none and its EventAI (script
        // 1602701, out-of-combat LOS event, range 4 yd) casts Explode 28433
        // (about 30k) on a hostile that comes close. Explode is an area
        // school-damage spell at EffectRadiusIndex 8 (5 yd, the index the
        // sludge's 22638 pulse uses), so the pulse is 5 yd. The keep-out is the
        // pulse plus 5 yd (the sludge gets 3): it drifts at only about 0.9 yd/s
        // (speed 0.375) but one hit is lethal, so it gets the wider margin. The
        // path ring is keepout + kCreatureAuraHazardMargin, as for every row.
        // Not tank-exempt: Explode does about 30k, which is how #906 lost both tanks.
        // The creature is also NOT_SELECTABLE (unit_flags 0x2000040, 28296 HP),
        // so no bot can target it: nobody kills it or sets it aside, bots only
        // keep their distance.
        { 533, 16027, 0, 5.0f, 10.0f, false },
        // Ruins of Ahn'Qiraj, Sand Vortex 15428 (boss_ossirian.cpp:56-60,179-195):
        // two summoned at aggro, wandering 55 yd, carrying Sand Storm 25160,
        // which triggers Harsh Winds 25161 (-87 % speed, silence) at
        // EffectRadiusIndex 8 (5 yd, the index the sludge's pulse uses), so the
        // pulse is 5 yd and the keep-out matches the sludge's 8 yd. Not
        // tank-exempt: a slowed tank cannot anchor the boss to a crystal.
        { 509, 15428, 25160, 5.0f, 8.0f, false },
    };

    inline CreatureAuraKeepout const* FindCreatureAuraKeepout(uint32_t mapId, uint32_t creatureEntry)
    {
        for (CreatureAuraKeepout const& row : kCreatureAuraKeepouts)
            if (row.mapId == mapId && row.creatureEntry == creatureEntry)
                return &row;
        return nullptr;
    }

    // The marker that makes a live listed creature a keep-out: its aura, or
    // nothing for an aura-less row (auraSpellId 0), where the creature is the
    // hazard whenever it is alive.
    inline bool CreatureAuraKeepoutMarkedLive(CreatureAuraKeepout const& row, bool hasAura)
    {
        return row.auraSpellId == 0 || hasAura;
    }

    // A listed creature is a keep-out for this bot when the toggle is on, the
    // creature is alive and carries the listed aura, and the player has not
    // ordered this bot to attack exactly that creature (the order wins).
    inline bool CreatureAuraKeepoutApplies(bool toggleOn, CreatureAuraKeepout const* row,
        bool alive, bool hasAura, bool isExplicitAttackTarget)
    {
        return toggleOn && row && alive && hasAura && !isExplicitAttackTarget;
    }

    // Step out while inside the keep-out radius. Both this and the movement
    // hazard radius are measured from the creature's centre (no bounding
    // radius), the way MovementAction's hazard checks measure, so an end
    // point the hazard accepts can never trigger a step-out.
    inline bool InsideCreatureAuraKeepout(CreatureAuraKeepout const& row, float distance)
    {
        return distance <= row.keepoutRadius;
    }

    // Radius registered as a movement hazard (end points and paths stay out).
    inline float CreatureAuraHazardRadius(CreatureAuraKeepout const& row)
    {
        return row.keepoutRadius + kCreatureAuraHazardMargin;
    }

    // ---- Follow near a keep-out (HoldFollowOutsideKeepout) ----------------
    //
    // While the follow goal lies inside a keep-out, the follower must never be
    // sent toward it across the hold ring. Rings around the creature, centre
    // measure, Creeping Sludge values in brackets:
    //   keepout   [8.0]  inside: the avoid-aoe step-out owns the bot
    //   hazard    [9.0]  8..9: back off along the creature-bot line to `backoff`
    //   hold      [9.5]  where an approaching follower stops (and the goal test)
    //   backoff   [10.0] where a back-off lands
    //   holdOuter [10.5] 9..10.5: hold (stop a running follow, issue nothing)
    //   release   [10.0] once engaged, the goal must clear this to follow again
    // Every move target lands inside the hold band with at least 0.5 yd to
    // either edge, so a height snap, a short arrival or the creature creeping
    // at 2 yd/s cannot flip hold/follow/step-out from one tick to the next.
    constexpr float kCreatureAuraFollowHoldMargin = 0.5f;    // hold ring past the hazard
    constexpr float kCreatureAuraFollowBackoffMargin = 1.0f; // back-off ring past the hazard
    constexpr float kCreatureAuraFollowOuterMargin = 1.5f;   // hold band edge past the hazard
    constexpr float kCreatureAuraFollowReleaseMargin = 1.0f; // engaged goal release past the hazard

    struct CreatureAuraFollowRings
    {
        float keepout, hazard, hold, backoff, holdOuter, release;
    };

    inline CreatureAuraFollowRings CreatureAuraFollowRingsFor(CreatureAuraKeepout const& row)
    {
        float const hazard = CreatureAuraHazardRadius(row);
        return { row.keepoutRadius, hazard, hazard + kCreatureAuraFollowHoldMargin,
            hazard + kCreatureAuraFollowBackoffMargin, hazard + kCreatureAuraFollowOuterMargin,
            hazard + kCreatureAuraFollowReleaseMargin };
    }

    // First point where the segment from B (outside the circle) to P enters
    // the circle (centre C, radius R). False when B is not outside, or the
    // segment never reaches the circle. 2D.
    inline bool SegmentEntersCircle(float bx, float by, float px, float py,
        float cx, float cy, float R, float& hx, float& hy)
    {
        float const fx = bx - cx, fy = by - cy;
        float const c = fx * fx + fy * fy - R * R;
        if (c <= 0.0f)
            return false; // B not outside
        float const dx = px - bx, dy = py - by;
        float const a = dx * dx + dy * dy;
        if (a <= 0.0f)
            return false;
        float const b = 2.0f * (fx * dx + fy * dy);
        float const disc = b * b - 4.0f * a * c;
        if (disc < 0.0f)
            return false; // the line misses the circle
        float const t = (-b - std::sqrt(disc)) / (2.0f * a);
        if (t < 0.0f || t > 1.0f)
            return false; // circle behind B, or beyond P
        hx = bx + dx * t;
        hy = by + dy * t;
        return true;
    }

    enum class CreatureAuraFollowStep
    {
        Follow,   // goal clear of the keep-out: native follow
        StepOut,  // bot inside the keep-out: stop following, the step-out moves it
        BackOff,  // bot inside the hazard: move to (hx, hy), outward on the creature-bot line
        Hold,     // bot in the hold band: stop a running follow, issue no move
        Approach, // bot outside the band: move to (hx, hy), never inside the hold ring
    };

    // One follow decision against one keep-out creature (centre C) for a bot
    // at B whose follow goal is P. `engaged`: the previous decision against
    // this creature was not Follow (goal hysteresis: an engaged follower
    // resumes only once the goal is past the release ring).
    inline CreatureAuraFollowStep PlanCreatureAuraFollow(CreatureAuraKeepout const& row, bool engaged,
        float bx, float by, float px, float py, float cx, float cy, float& hx, float& hy)
    {
        CreatureAuraFollowRings const rings = CreatureAuraFollowRingsFor(row);
        float const gdx = px - cx, gdy = py - cy;
        float const goalLimit = engaged ? rings.release : rings.hold;
        if (gdx * gdx + gdy * gdy > goalLimit * goalLimit)
            return CreatureAuraFollowStep::Follow;

        float const fx = bx - cx, fy = by - cy;
        float const d = std::sqrt(fx * fx + fy * fy);
        if (d <= rings.keepout)
            return CreatureAuraFollowStep::StepOut;
        if (d < rings.hazard)
        {
            // d > keepout > 0, so the direction is defined.
            hx = cx + fx / d * rings.backoff;
            hy = cy + fy / d * rings.backoff;
            return CreatureAuraFollowStep::BackOff;
        }
        if (d <= rings.holdOuter)
            return CreatureAuraFollowStep::Hold;
        // Outside the band: walk to where the straight line to the goal meets
        // the hold ring. An engaged goal between the hold and release rings
        // may be reachable without touching the ring; then walk to it.
        if (!SegmentEntersCircle(bx, by, px, py, cx, cy, rings.hold, hx, hy))
        {
            hx = px;
            hy = py;
        }
        return CreatureAuraFollowStep::Approach;
    }

    // ---- Chase near a keep-out (HoldChaseOutsideKeepout) ------------------
    //
    // A chase (reach melee, reach spell, ...) is planned with the same bands,
    // the goal being the point the chase wants the bot at: `distance` from the
    // target toward the bot, i.e. the target itself for a melee chase
    // (distance 0). A melee bot whose target is a keep-out creature, or stands
    // within the hold ring of one, so stops outside the ring instead of
    // chasing into the pulse; a ranged chase whose goal lies well outside the
    // ring is left alone.
    inline void CreatureAuraChaseGoal(float bx, float by, float tx, float ty, float distance,
        float& gx, float& gy)
    {
        float const dx = bx - tx, dy = by - ty;
        float const d = std::sqrt(dx * dx + dy * dy);
        if (distance <= 0.0f || d <= 0.0f)
        {
            gx = tx;
            gy = ty;
            return;
        }
        float const k = (distance < d ? distance : d) / d;
        gx = tx + dx * k;
        gy = ty + dy * k;
    }

    // ---- Roles and the bounded chase hold (owner decision 2026-10-13) ------
    //
    // Tanks pick up and hold mobs, so a tank's chase is never held at the
    // ring, and in combat a tank neither steps out of a keep-out nor has it
    // registered as a path hazard. Out of combat (following, walking past an
    // idle creature) a tank keeps out like everyone else.
    // Both exemptions are per row (CreatureAuraKeepout::tankExempt): a tank
    // gets no pass at a creature whose hit is lethal (Living Poison).
    inline bool CreatureAuraChaseExempt(CreatureAuraKeepout const& row, bool isTank)
    {
        return isTank && row.tankExempt;
    }

    inline bool CreatureAuraCombatExempt(CreatureAuraKeepout const& row, bool isTank, bool inCombat)
    {
        return isTank && inCombat && row.tankExempt;
    }

    // ---- No landing spot (Codex review of #1038) --------------------------
    //
    // A step-out that finds no landing spot backs off for a moment so the
    // action does not win the reaction pick every tick without moving. That
    // is fine for a row that only chips (the sludge); a lethal row (not
    // tank-exempt: Living Poison, one Explode kills) retries on the next
    // tick, and when the landings still fail it steps straight away from the
    // creature (CreatureAuraStepAwayPoint).
    constexpr uint32_t kCreatureAuraNoLandingBackoffMs = 2000;
    // Yards past the keep-out rim the straight step-away aims for.
    constexpr float kCreatureAuraStepAwayMargin = 1.5f;

    inline uint32_t CreatureAuraNoLandingUntilMs(CreatureAuraKeepout const& row, uint32_t nowMs)
    {
        return row.tankExempt ? nowMs + kCreatureAuraNoLandingBackoffMs : nowMs;
    }

    // The point straight away from the creature centre (hx, hy) through the
    // bot (bx, by): the remaining distance to the keep-out rim plus the margin
    // from where the bot stands (it never aims closer than the margin, so a
    // bot already outside the rim is not sent back in). A bot standing on the
    // centre heads along `fallbackAngle`.
    inline void CreatureAuraStepAwayPoint(float hx, float hy, float bx, float by, float keepoutRadius,
        float fallbackAngle, float& px, float& py)
    {
        float dx = bx - hx, dy = by - hy;
        float const d = std::sqrt(dx * dx + dy * dy);
        if (d > 0.001f)
        {
            dx /= d;
            dy /= d;
        }
        else
        {
            dx = std::cos(fallbackAngle);
            dy = std::sin(fallbackAngle);
        }
        float const remaining = keepoutRadius - d;
        float const dist = (remaining > 0.0f ? remaining : 0.0f) + kCreatureAuraStepAwayMargin;
        px = bx + dx * dist;
        py = by + dy * dist;
    }

    // True when some row of the map gives tanks no pass; on a map without one
    // a tank in combat skips the keep-out lookups altogether.
    inline bool CreatureAuraMapHasNonTankExemptRow(uint32_t mapId)
    {
        for (CreatureAuraKeepout const& row : kCreatureAuraKeepouts)
            if (row.mapId == mapId && !row.tankExempt)
                return true;
        return false;
    }

    // A chase held outside a ring because its goal is inside is no headway:
    // after this long on the same hostile current target the bot sets the
    // target aside and picks another, also in group dungeons where the reach
    // give-up is off. A player's attack order on that target keeps the hold
    // (and an ordered keep-out creature is not held at all).
    constexpr unsigned kCreatureAuraChaseHoldMs = 3000;
    // How long a set-aside target is left alone, even while it hits the bot.
    constexpr unsigned kCreatureAuraSetAsideMs = 30000;

    inline bool CreatureAuraSetAsideTarget(bool hostileCurrentTarget, bool explicitOrder, unsigned heldMs)
    {
        return hostileCurrentTarget && !explicitOrder && heldMs >= kCreatureAuraChaseHoldMs;
    }

    // A chase hold is timed per target. The timer starts over when the hold
    // is on another target, or when the last hold tick was longer ago than
    // this (combat ended mid-hold, or some other action ran in between), so a
    // later hold on the same mob never inherits an old start time and sets it
    // aside on its first tick.
    constexpr unsigned kCreatureAuraChaseHoldGapMs = 2000;

    inline bool CreatureAuraChaseHoldRestarts(bool sameTarget, unsigned sinceLastHoldMs)
    {
        return !sameTarget || sinceLastHoldMs > kCreatureAuraChaseHoldGapMs;
    }

    // A set-aside mob counts as still ringed while it stands within the
    // release ring of a listed creature (centre measure): the ring the chase
    // hold lets go of only past (CreatureAuraFollowRingsFor().release), so a
    // mob just outside it is chased without being held again.
    inline bool CreatureAuraSetAsideRinged(CreatureAuraKeepout const& row, float distance)
    {
        return distance <= CreatureAuraFollowRingsFor(row).release;
    }

    // Target selection (AttackersValue::IsKeepoutSetAside, the bot's own
    // "keepout set aside" book): a set-aside target stays refused until its
    // entry runs out. Every pick path asks this, also the ones that bypass the
    // attackers list (the group tank's victim, raid marks): otherwise the
    // tank's victim came straight back after the set-aside and the bot held,
    // set aside and re-picked the same mob all fight. A keep-out creature
    // stays refused while it is set aside (reachable, but only inside its
    // pulse). Any other mob is refused only while it still stands inside the
    // ring of a listed creature (`stillRinged`): the set-aside ends the moment
    // the tank drags it out, or it walks out to someone else (owner decision
    // 2026-10-13, "lift when it leaves the ring"), and also when it hits the
    // bot in sight. The 30 s entry stays the upper bound. Tanks never hold a
    // set-aside.
    inline bool CreatureAuraSetAsideHolds(bool live, bool isKeepoutCreature, bool isTank, bool hitsBotInSight,
        bool stillRinged)
    {
        return live && !isTank && (isKeepoutCreature || (stillRinged && !hitsBotInSight));
    }

    // ---- Step-out landing near other keep-outs -----------------------------
    //
    // Radius a step-out landing must clear around a listed creature other than
    // the one the bot steps away from (sludges stand in groups): its keep-out
    // on the first pass, only its pulse on the relaxed second pass, so a bot
    // in a cluster with no landing outside every keep-out still never steps
    // into another pulse.
    inline float CreatureAuraLandingClearance(CreatureAuraKeepout const& row, bool relaxed)
    {
        return relaxed ? row.pulseRadius : row.keepoutRadius;
    }

    // ---- End points and gap closers (Maraudon live run 2, 2026-10-13) -----
    //
    // The hazard registry only bends a path; a move whose end point lies in a
    // ring still walks the bot in. An end point (rear flank, set behind, move
    // out of collision, a gap closer's landing) is refused while it lies
    // within the hazard radius of a listed creature, the ring a chase or
    // follow end point already keeps out of.
    inline bool CreatureAuraEndPointBlocked(CreatureAuraKeepout const& row, float creatureToPointDistance)
    {
        return creatureToPointDistance < CreatureAuraHazardRadius(row);
    }

    // Where a gap closer (Charge, Intercept, Feral Charge) puts the bot:
    // `reach` from the target on the bot's side (the chase goal at that
    // distance).
    inline void CreatureAuraGapCloserLanding(float bx, float by, float tx, float ty, float reach,
        float& lx, float& ly)
    {
        CreatureAuraChaseGoal(bx, by, tx, ty, reach, lx, ly);
    }

    // A gap closer jumps the bot past every hold: it never goes through the
    // chase, so neither the ring nor the 3 s set-aside sees it (live: a
    // warrior DPS charged a sludge from 21 yd and died in its pulse). A
    // non-tank bot does not use one onto a listed creature, or onto a target
    // whose landing lies in a ring. Tanks pick up mobs in the ring
    // (CreatureAuraChaseExempt), and a player's attack order on that target
    // keeps today's behaviour.
    // A gap closer fires from 8-25 yd, and set behind or rear flank end next
    // to a target up to 15 yd away, beyond the bot's cached 15 yd list, so
    // those points are also checked against the shared hazard snapshot below.
    inline float CreatureAuraMaxHazardRadiusOnMap(uint32_t mapId)
    {
        float radius = 0.0f;
        for (CreatureAuraKeepout const& row : kCreatureAuraKeepouts)
            if (row.mapId == mapId && CreatureAuraHazardRadius(row) > radius)
                radius = CreatureAuraHazardRadius(row);
        return radius;
    }

    inline bool CreatureAuraGapCloserVetoed(bool targetIsKeepout, bool landingInRing, bool isTank,
        bool explicitOrder)
    {
        return !isTank && !explicitOrder && (targetIsKeepout || landingInRing);
    }

    // Review #882 round 2: the engine asks isUseful() before the stance
    // prerequisite and before isPossible(), so a gap closer's veto ran on
    // every tick its trigger fired ("enemy out of melee" pushes Charge every
    // tick of a chase, and Fury falls through to Intercept). The landing
    // lookup therefore runs only when the cast could fire this tick. Each
    // "not ready" reason below is one isPossible() also refuses
    // (CastSpellAction::isPossible -> PlayerbotAI::CanCastSpell ->
    // Spell::CheckCast), so skipping the veto then never lets an unvetoed
    // cast through: the engine sees what it sees with the option off (the
    // cast is impossible and its alternatives are pushed).
    // The stance is deliberately not a reason: a wrong stance is fixed by the
    // stance prerequisite, which runs only after isUseful(). Gating on it
    // would switch stance first and veto the charge a tick later.
    struct CreatureAuraGapCloserReadiness
    {
        bool knowsSpell = false;     // "spell id" resolved and the spell entry exists
        bool lostControl = false;    // UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL
        bool onCooldown = false;     // the spell's own cooldown (Player::HasSpellCooldown)
        bool powerShort = false;     // rage below the cost (Intercept 10, Feral Charge 5)
        bool nonCombatSpell = false; // SPELL_ATTR_CANT_USED_IN_COMBAT (Charge)
        bool inCombat = false;
        bool rangeKnown = false;     // isPossible() tests min/max range for this spell
        float distance = 0.0f;       // the distance isPossible() measures
        float minRange = 0.0f;
        float maxRange = 0.0f;       // 25 yd for Charge and Intercept
    };

    inline bool CreatureAuraGapCloserCouldFire(CreatureAuraGapCloserReadiness const& r)
    {
        if (!r.knowsSpell || r.lostControl || r.onCooldown || r.powerShort)
            return false;
        if (r.nonCombatSpell && r.inCombat)
            return false; // Charge never fires in combat on 1.12
        if (r.rangeKnown && !(r.distance < r.maxRange && r.distance >= r.minRange))
            return false; // the same test as CastSpellAction::isPossible
        return true;
    }

    // ---- Shared hazard snapshot (reviews of #995) --------------------------
    //
    // End points and gap-closer landings can lie past the bot's cached 15 yd
    // "possible triggers" list, and nothing per bot tick may scan the world
    // (AGENTS.md). The listed creatures carrying their aura are kept in
    // snapshots shared by every bot, one per (map, instance, 30 yd cell of the
    // bot's position): at most one bounded grid visit per cell every
    // kCreatureAuraSnapshotMs, and at most kKeepoutRefreshesPerTick visits per
    // world update over all cells, so a party standing together pays one
    // visit, not five, and none happen on a map without a listed row.
    //
    // A snapshot only adds what the live checks cannot see: creatures in the
    // bot's live list (and the target and the explicit attack target) are
    // skipped, because the live check already judged them at their exact
    // ring and current position. A snapshot-only creature can have moved
    // since the visit if it was moving then, so its ring is widened by the
    // drift: 3.5 yd per 500 ms of the snapshot's age (run speed), up to
    // 10.5 yd; a creature that stood still keeps its exact ring. It
    // only counts when it stands within kCreatureAuraSnapshotHeightYd of the
    // point's height (a creature on another floor does not ring it).
    // No snapshot (the refresh budget is spent and there is no entry younger
    // than kCreatureAuraSnapshotMaxAgeMs), or a point outside the snapshot's
    // cover: an end point is left to the live checks, as in #882; a gap-closer
    // landing gets #882's one bounded lookup around it instead
    // (CreatureAuraLandingRinged), so a busy tick never lets a charge through.
    constexpr uint32_t kCreatureAuraSnapshotMs = 500;
    constexpr uint32_t kCreatureAuraSnapshotMaxAgeMs = 1500; // a stale entry reused while the budget is spent
    constexpr float kCreatureAuraSnapshotDriftYd = 3.5f;     // per 500 ms of age: 7 yd/s run speed
    constexpr float kCreatureAuraSnapshotMaxDriftYd =
        kCreatureAuraSnapshotDriftYd * (float(kCreatureAuraSnapshotMaxAgeMs) / float(kCreatureAuraSnapshotMs));
    constexpr float kCreatureAuraSnapshotReachYd = 30.0f;  // 15 yd list + 15 yd end point / landing
    constexpr float kCreatureAuraSnapshotCellYd = 30.0f;
    constexpr float kCreatureAuraSnapshotHeightYd = 5.0f;  // the ring checks are 2D; one floor
    constexpr uint32_t kCreatureAuraSnapshotPruneMs = 10 * kCreatureAuraSnapshotMs;
    constexpr uint32_t kKeepoutRefreshesPerTick = 4;

    struct CreatureAuraHazardEntry
    {
        uint64_t guidRaw;
        float x, y, z;
        float hazardRadius; // CreatureAuraHazardRadius(row)
        // Moving at the visit (moving now, or any movement generator but
        // idle). Only a moving creature gets the age drift; a still one keeps
        // its exact ring at any age. Defaults to moving (the safe side).
        bool moving = true;
        // CreatureAuraKeepout::tankExempt of the row (defaults to exempt, the
        // sludge's setting; the game code always sets it).
        bool tankExempt = true;
    };

    struct CreatureAuraSnapshotKey
    {
        uint32_t mapId;
        uint32_t instanceId;
        int32_t cellX, cellY;
        bool operator<(CreatureAuraSnapshotKey const& o) const
        {
            return std::tie(mapId, instanceId, cellX, cellY) < std::tie(o.mapId, o.instanceId, o.cellX, o.cellY);
        }
    };

    inline CreatureAuraSnapshotKey CreatureAuraSnapshotKeyFor(uint32_t mapId, uint32_t instanceId, float x, float y)
    {
        return { mapId, instanceId, int32_t(std::floor(x / kCreatureAuraSnapshotCellYd)),
            int32_t(std::floor(y / kCreatureAuraSnapshotCellYd)) };
    }

    inline void CreatureAuraSnapshotCellCentre(CreatureAuraSnapshotKey const& key, float& cx, float& cy)
    {
        cx = (float(key.cellX) + 0.5f) * kCreatureAuraSnapshotCellYd;
        cy = (float(key.cellY) + 0.5f) * kCreatureAuraSnapshotCellYd;
    }

    // Points a bot anywhere in the cell may check: within the reach of any
    // spot in the cell, i.e. half the cell diagonal plus the reach from the
    // cell centre.
    inline float CreatureAuraSnapshotCoverYd()
    {
        return kCreatureAuraSnapshotCellYd * 0.70710678f + kCreatureAuraSnapshotReachYd;
    }

    // Grid-visit radius around the cell centre: every creature whose ring,
    // widened by the largest drift, can hold a covered point.
    inline float CreatureAuraSnapshotRadius(float maxHazardRadius)
    {
        return CreatureAuraSnapshotCoverYd() + maxHazardRadius + kCreatureAuraSnapshotMaxDriftYd;
    }

    // How far a creature may have moved since a visit `ageMs` ago: run speed
    // times the age, capped at the largest drift the visit radius allows. A
    // snapshot taken this tick gets no widening, so a creature standing still
    // gets the same 9 yd decision on both sides of the live-list boundary.
    inline float CreatureAuraSnapshotDriftFor(uint32_t ageMs)
    {
        float const drift = kCreatureAuraSnapshotDriftYd * float(ageMs) / float(kCreatureAuraSnapshotMs);
        return drift < kCreatureAuraSnapshotMaxDriftYd ? drift : kCreatureAuraSnapshotMaxDriftYd;
    }

    struct CreatureAuraHazardSnapshot
    {
        bool valid = false;
        uint32_t atMs = 0;
        float cx = 0.0f, cy = 0.0f; // cell centre
        std::vector<CreatureAuraHazardEntry> entries;
    };

    // Unsigned difference, so the wrap of the millisecond clock does not keep
    // a snapshot alive.
    inline bool CreatureAuraSnapshotStale(CreatureAuraHazardSnapshot const& snap, uint32_t nowMs)
    {
        return !snap.valid || uint32_t(nowMs - snap.atMs) >= kCreatureAuraSnapshotMs;
    }

    inline bool CreatureAuraSnapshotCovers(CreatureAuraHazardSnapshot const& snap, float x, float y)
    {
        float const dx = x - snap.cx, dy = y - snap.cy;
        float const cover = CreatureAuraSnapshotCoverYd();
        return snap.valid && dx * dx + dy * dy <= cover * cover;
    }

    // The shared cache (world thread only: every bot AI update runs in the
    // module's world-thread pass, which calls CreatureAuraSnapshotBeginTick
    // once per world update).
    struct CreatureAuraSnapshotCache
    {
        std::map<CreatureAuraSnapshotKey, CreatureAuraHazardSnapshot> entries;
        uint32_t lastPruneMs = 0;
        uint32_t refreshesThisTick = 0;
    };

    inline void CreatureAuraSnapshotBeginTick(CreatureAuraSnapshotCache& cache)
    {
        cache.refreshesThisTick = 0;
    }

    // What a lookup may use: the snapshot (nullptr: live checks only) and
    // the drift its age allows.
    struct CreatureAuraSnapshotView
    {
        CreatureAuraHazardSnapshot const* snap = nullptr;
        float driftYd = 0.0f;
        bool refreshed = false;  // this lookup made the grid visit
        bool budgetMiss = false; // it was due a refresh, the tick's budget was spent
    };

    // The snapshot for `key`. A fresh entry is used as is. A stale or missing
    // one is refreshed through `fill(entries, cx, cy)` (the one grid visit
    // around the cell centre) while this world update's budget lasts;
    // otherwise a stale entry younger than kCreatureAuraSnapshotMaxAgeMs is
    // reused with its drift scaled by age, and anything older gives no
    // snapshot. Entries not refreshed for kCreatureAuraSnapshotPruneMs are
    // dropped, so the cache stays as small as the cells bots stand in.
    template <class Fill>
    inline CreatureAuraSnapshotView CreatureAuraSnapshotCacheGet(CreatureAuraSnapshotCache& cache,
        CreatureAuraSnapshotKey const& key, uint32_t nowMs, Fill&& fill)
    {
        if (uint32_t(nowMs - cache.lastPruneMs) >= kCreatureAuraSnapshotPruneMs)
        {
            for (auto it = cache.entries.begin(); it != cache.entries.end();)
            {
                if (uint32_t(nowMs - it->second.atMs) >= kCreatureAuraSnapshotPruneMs)
                    it = cache.entries.erase(it);
                else
                    ++it;
            }
            cache.lastPruneMs = nowMs;
        }
        CreatureAuraSnapshotView view;
        auto it = cache.entries.find(key);
        if (it != cache.entries.end() && !CreatureAuraSnapshotStale(it->second, nowMs))
        {
            view.snap = &it->second;
            view.driftYd = CreatureAuraSnapshotDriftFor(uint32_t(nowMs - it->second.atMs));
            return view;
        }
        if (cache.refreshesThisTick < kKeepoutRefreshesPerTick)
        {
            CreatureAuraHazardSnapshot& snap = cache.entries[key];
            CreatureAuraSnapshotCellCentre(key, snap.cx, snap.cy);
            snap.entries.clear();
            fill(snap.entries, snap.cx, snap.cy);
            snap.valid = true;
            snap.atMs = nowMs;
            ++cache.refreshesThisTick;
            view.snap = &snap;
            view.driftYd = 0.0f; // taken this tick
            view.refreshed = true;
            return view;
        }
        view.budgetMiss = true;
        if (it != cache.entries.end() && it->second.valid &&
            uint32_t(nowMs - it->second.atMs) < kCreatureAuraSnapshotMaxAgeMs)
        {
            view.snap = &it->second;
            view.driftYd = CreatureAuraSnapshotDriftFor(uint32_t(nowMs - it->second.atMs));
        }
        return view;
    }

    // The first creature in `entries` (not in `skip`), within
    // kCreatureAuraSnapshotHeightYd of `z`, whose ring holds (x, y): widened
    // by `driftYd` when it was moving at the visit, its exact ring when it
    // stood still. 0 when none.
    inline uint64_t CreatureAuraEntriesRingAt(std::vector<CreatureAuraHazardEntry> const& entries, float x, float y,
        float z, float driftYd, std::vector<uint64_t> const& skip, bool ignoreTankExempt = false)
    {
        for (CreatureAuraHazardEntry const& e : entries)
        {
            if (e.guidRaw == 0 || std::find(skip.begin(), skip.end(), e.guidRaw) != skip.end())
                continue;
            if (ignoreTankExempt && e.tankExempt)
                continue; // the asking tank may stand in this ring
            if (std::fabs(e.z - z) > kCreatureAuraSnapshotHeightYd)
                continue;
            float const dx = e.x - x, dy = e.y - y;
            if (std::sqrt(dx * dx + dy * dy) < e.hazardRadius + (e.moving ? driftYd : 0.0f))
                return e.guidRaw;
        }
        return 0;
    }

    // A view the point can be judged by: a snapshot that covers it.
    inline bool CreatureAuraSnapshotUsable(CreatureAuraSnapshotView const& view, float x, float y)
    {
        return view.snap && CreatureAuraSnapshotCovers(*view.snap, x, y);
    }

    // The snapshot-only creature whose ring, widened by the view's drift,
    // holds (x, y) on the point's floor. `skip` lists the creatures the live
    // checks already judged (the bot's live list, the target, the explicit
    // attack target). 0 when none, when there is no snapshot, or when the
    // point lies outside its cover.
    inline uint64_t CreatureAuraSnapshotRingAt(CreatureAuraSnapshotView const& view, float x, float y, float z,
        std::vector<uint64_t> const& skip, bool ignoreTankExempt = false)
    {
        if (!CreatureAuraSnapshotUsable(view, x, y))
            return 0;
        return CreatureAuraEntriesRingAt(view.snap->entries, x, y, z, view.driftYd, skip, ignoreTankExempt);
    }

    // The whole snapshot check, the seam the game code and the tests share:
    // look up the cell's snapshot (refreshing through `fill` within budget)
    // and test the point. `view` reports what the lookup did.
    template <class Fill>
    inline uint64_t CreatureAuraSnapshotRinged(CreatureAuraSnapshotCache& cache, CreatureAuraSnapshotKey const& key,
        uint32_t nowMs, float x, float y, float z, std::vector<uint64_t> const& skip, Fill&& fill,
        CreatureAuraSnapshotView* view = nullptr, bool ignoreTankExempt = false)
    {
        CreatureAuraSnapshotView const v = CreatureAuraSnapshotCacheGet(cache, key, nowMs, std::forward<Fill>(fill));
        if (view)
            *view = v;
        return CreatureAuraSnapshotRingAt(v, x, y, z, skip, ignoreTankExempt);
    }

    // A gap-closer landing (review round 4 of #995): the snapshot when it is
    // usable; otherwise (the tick's refresh budget spent with no entry young
    // enough, or the landing outside its cover) #882's one bounded lookup
    // around the landing: `lookup(x, y)` returns the listed creatures within
    // the largest hazard radius of it, judged at their exact ring (live) and
    // with the same height filter as the snapshot. A charge never fails
    // open on a busy tick. `lookedUp` reports whether the lookup ran.
    template <class Fill, class Lookup>
    inline uint64_t CreatureAuraLandingRinged(CreatureAuraSnapshotCache& cache, CreatureAuraSnapshotKey const& key,
        uint32_t nowMs, float x, float y, float z, std::vector<uint64_t> const& skip, Fill&& fill, Lookup&& lookup,
        CreatureAuraSnapshotView* view = nullptr, bool* lookedUp = nullptr, bool ignoreTankExempt = false)
    {
        CreatureAuraSnapshotView const v = CreatureAuraSnapshotCacheGet(cache, key, nowMs, std::forward<Fill>(fill));
        if (view)
            *view = v;
        if (lookedUp)
            *lookedUp = false;
        if (CreatureAuraSnapshotUsable(v, x, y))
            return CreatureAuraSnapshotRingAt(v, x, y, z, skip, ignoreTankExempt);
        if (lookedUp)
            *lookedUp = true;
        std::vector<CreatureAuraHazardEntry> const live = lookup(x, y);
        return CreatureAuraEntriesRingAt(live, x, y, z, 0.0f, skip, ignoreTankExempt);
    }
} // namespace ai
