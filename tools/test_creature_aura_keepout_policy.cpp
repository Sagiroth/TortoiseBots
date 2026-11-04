// Standalone check for ai/playerbot/CreatureAuraKeepoutPolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_creature_aura_keepout_policy.cpp -o /tmp/test_cak && /tmp/test_cak
#include "../ai/playerbot/policy/loot/CreatureAuraKeepoutPolicy.h"
#include "../ai/playerbot/AvoidAoePolicy.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

int main()
{
    std::cout << "Starting TortoiseBots creature aura keep-out tests...\n";

    // Table: the Creeping Sludge row, Turtle ids, only on Maraudon.
    {
        CreatureAuraKeepout const* row = FindCreatureAuraKeepout(349, 12222);
        CHECK(row != nullptr);
        CHECK(row->auraSpellId == 22638);
        CHECK(row->pulseRadius == 5.0f);
        CHECK(row->keepoutRadius == 8.0f);
        CHECK(FindCreatureAuraKeepout(0, 12222) == nullptr);   // other map
        CHECK(FindCreatureAuraKeepout(349, 12221) == nullptr); // Noxious Slime: no aura, not listed
        CHECK(FindCreatureAuraKeepout(349, 0) == nullptr);
    }
    std::cout << "  [PASS] table lookup by map and entry\n";

    // #906: Naxxramas Living Poison, listed without an aura. Explode 28433 is
    // an area spell at EffectRadiusIndex 8 (5 yd); the EventAI fires it from 4 yd.
    {
        CreatureAuraKeepout const* poison = FindCreatureAuraKeepout(533, 16027);
        CHECK(poison != nullptr);
        CHECK(poison->auraSpellId == 0);
        CHECK(poison->pulseRadius == 5.0f);
        CHECK(poison->keepoutRadius == 10.0f);
        CHECK(poison->keepoutRadius >= poison->pulseRadius + 5.0f); // Explode radius plus a 5 yd margin (the sludge gets 3)
        CHECK(poison->keepoutRadius - poison->pulseRadius > FindCreatureAuraKeepout(349, 12222)->keepoutRadius - FindCreatureAuraKeepout(349, 12222)->pulseRadius);
        CHECK(poison->keepoutRadius > 4.0f);                // clear of the 4 yd trigger range
        CHECK(FindCreatureAuraKeepout(349, 16027) == nullptr); // other map
        CHECK(FindCreatureAuraKeepout(533, 12222) == nullptr);
        CHECK(FindCreatureAuraKeepout(533, 16028) == nullptr); // Patchwerk's neighbours are not listed
        // Aura-less row: alive is enough. A row with an aura still needs it.
        CHECK(CreatureAuraKeepoutMarkedLive(*poison, false));
        CHECK(CreatureAuraKeepoutApplies(true, poison, true, CreatureAuraKeepoutMarkedLive(*poison, false), false));
        CHECK(!CreatureAuraKeepoutApplies(true, poison, false, CreatureAuraKeepoutMarkedLive(*poison, false), false)); // dead
        CHECK(!CreatureAuraKeepoutApplies(false, poison, true, CreatureAuraKeepoutMarkedLive(*poison, false), false)); // toggle off
        CHECK(!CreatureAuraKeepoutApplies(true, poison, true, CreatureAuraKeepoutMarkedLive(*poison, false), true));   // ordered
        CreatureAuraKeepout const& sludgeRow = *FindCreatureAuraKeepout(349, 12222);
        CHECK(!CreatureAuraKeepoutMarkedLive(sludgeRow, false));
        CHECK(CreatureAuraKeepoutMarkedLive(sludgeRow, true));
        // The step-out and path rings, and the snapshot gate for the map.
        CHECK(InsideCreatureAuraKeepout(*poison, 4.0f));
        CHECK(InsideCreatureAuraKeepout(*poison, 10.0f));
        CHECK(!InsideCreatureAuraKeepout(*poison, 10.1f));
        CHECK(CreatureAuraHazardRadius(*poison) == 11.0f); // keep-out + the shared path margin
        CHECK(CreatureAuraMaxHazardRadiusOnMap(533) == 11.0f);
        CHECK(CreatureAuraMaxHazardRadiusOnMap(1) == 0.0f); // no snapshot visit on a map without a row
    }
    // Ruins of Ahn'Qiraj, Sand Vortex 15428 (boss_ossirian.cpp): two summoned at
    // aggro, wandering 55 yd, carrying Sand Storm 25160, which triggers Harsh
    // Winds 25161 (-87 % speed, silence) at EffectRadiusIndex 8 (5 yd). Not
    // tank-exempt: a slowed tank cannot anchor the boss to a crystal.
    {
        CreatureAuraKeepout const* vortex = FindCreatureAuraKeepout(509, 15428);
        CHECK(vortex != nullptr);
        CHECK(vortex->auraSpellId == 25160);
        CHECK(vortex->pulseRadius == 5.0f);
        CHECK(vortex->keepoutRadius == 8.0f);
        CHECK(vortex->tankExempt == false);
        CHECK(FindCreatureAuraKeepout(509, 15429) == nullptr); // other entry
        CHECK(FindCreatureAuraKeepout(0, 15428) == nullptr);  // other map
        CHECK(FindCreatureAuraKeepout(531, 15428) == nullptr); // temple, not ruins
        // A row with an aura needs it to count as live.
        CHECK(!CreatureAuraKeepoutMarkedLive(*vortex, false));
        CHECK(CreatureAuraKeepoutMarkedLive(*vortex, true));
        CHECK(InsideCreatureAuraKeepout(*vortex, 8.0f));
        CHECK(!InsideCreatureAuraKeepout(*vortex, 8.1f));
        CHECK(CreatureAuraHazardRadius(*vortex) == 9.0f);
    }
    std::cout << "  [PASS] Sand Vortex (Ruins of Ahn'Qiraj): 5 yd Harsh Winds, 8 yd keep-out, no tank pass\n";

    // Codex review of #1038: a step-out with no landing spot. The sludge backs
    // off 2 s; the lethal row retries on the next tick and steps straight away.
    {
        CreatureAuraKeepout const& sludgeRow = *FindCreatureAuraKeepout(349, 12222);
        CreatureAuraKeepout const& poisonRow = *FindCreatureAuraKeepout(533, 16027);
        CHECK(CreatureAuraNoLandingUntilMs(sludgeRow, 1000) == 1000 + kCreatureAuraNoLandingBackoffMs);
        CHECK(CreatureAuraNoLandingUntilMs(poisonRow, 1000) == 1000); // isUseful's "now < until" never holds
        // Repeated landing failure on the lethal row: the action stays eligible
        // on every tick, the sludge's does not for 2 s.
        uint32_t poisonUntil = 0, sludgeUntil = 0;
        unsigned poisonEligible = 0, sludgeEligible = 0;
        for (uint32_t now = 1000; now < 1000 + 5000; now += 100)
        {
            if (now >= poisonUntil) { ++poisonEligible; poisonUntil = CreatureAuraNoLandingUntilMs(poisonRow, now); }
            if (now >= sludgeUntil) { ++sludgeEligible; sludgeUntil = CreatureAuraNoLandingUntilMs(sludgeRow, now); }
        }
        CHECK(poisonEligible == 50);
        CHECK(sludgeEligible == 3); // 1000, 3000, 5000
        // The straight step-away: from 3 yd away, to the 10 yd rim plus 1.5 yd,
        // along the creature -> bot line.
        float px = 0.0f, py = 0.0f;
        CreatureAuraStepAwayPoint(0.0f, 0.0f, 3.0f, 0.0f, poisonRow.keepoutRadius, 0.0f, px, py);
        CHECK(std::fabs(px - 11.5f) < 0.001f && std::fabs(py) < 0.001f);
        CreatureAuraStepAwayPoint(100.0f, 50.0f, 100.0f, 54.0f, poisonRow.keepoutRadius, 0.0f, px, py);
        CHECK(std::fabs(px - 100.0f) < 0.001f && std::fabs(py - 61.5f) < 0.001f); // any heading
        CHECK(std::hypot(px - 100.0f, py - 50.0f) > poisonRow.keepoutRadius);
        // Already outside the rim: only the margin, never back toward it.
        CreatureAuraStepAwayPoint(0.0f, 0.0f, 12.0f, 0.0f, poisonRow.keepoutRadius, 0.0f, px, py);
        CHECK(std::fabs(px - 13.5f) < 0.001f);
        // On the centre: the fallback heading.
        CreatureAuraStepAwayPoint(5.0f, 5.0f, 5.0f, 5.0f, poisonRow.keepoutRadius, 1.5707963f, px, py);
        CHECK(std::fabs(px - 5.0f) < 0.01f && std::fabs(py - 16.5f) < 0.01f);
    }
    std::cout << "  [PASS] lethal keep-out retries every tick and steps straight away on repeated landing failure\n";

    // Every row keeps out past its pulse and fits the avoid-aoe radius cap,
    // so the existing strafe-first step-out handles it.
    for (CreatureAuraKeepout const& row : kCreatureAuraKeepouts)
    {
        CHECK(row.keepoutRadius > row.pulseRadius);
        CHECK(IsAvoidableAoeRadius(row.keepoutRadius));
        CHECK(CreatureAuraHazardRadius(row) > row.keepoutRadius);
        CHECK(IsAvoidableAoeRadius(CreatureAuraHazardRadius(row)));
    }
    std::cout << "  [PASS] keep-out wider than the pulse, inside the 15 yd cap\n";

    // Applies: toggle, row, alive, aura, and no explicit order on it.
    {
        CreatureAuraKeepout const* row = FindCreatureAuraKeepout(349, 12222);
        CHECK(CreatureAuraKeepoutApplies(true, row, true, true, false));
        CHECK(!CreatureAuraKeepoutApplies(false, row, true, true, false)); // toggle off: today's behaviour
        CHECK(!CreatureAuraKeepoutApplies(true, nullptr, true, true, false));
        CHECK(!CreatureAuraKeepoutApplies(true, row, false, true, false)); // dead
        CHECK(!CreatureAuraKeepoutApplies(true, row, true, false, false)); // aura gone
        CHECK(!CreatureAuraKeepoutApplies(true, row, true, true, true));   // player ordered the attack
    }
    std::cout << "  [PASS] applies only with toggle, live aura and no explicit order\n";

    // Step-out band and hysteresis: step out inside 8, paths kept outside 9.
    {
        CreatureAuraKeepout const& row = *FindCreatureAuraKeepout(349, 12222);
        CHECK(InsideCreatureAuraKeepout(row, 4.0f));  // melee reach: inside the pulse
        CHECK(InsideCreatureAuraKeepout(row, 8.0f));
        CHECK(!InsideCreatureAuraKeepout(row, 8.1f));
        CHECK(CreatureAuraHazardRadius(row) == 9.0f);
        // The existing escape step clears the keep-out rim from inside the pulse.
        CHECK(AoeEscapeStep(row.keepoutRadius, 8.0f) == 8.0f);
        CHECK(AoeEscapeStep(row.keepoutRadius, 15.0f) == 9.0f);
        CHECK(kCreatureAuraHazardSeconds >= 2);
    }
    std::cout << "  [PASS] step out inside 8 yd, hazard keeps paths 9 yd away\n";

    // Player order: the ordered creature is never a keep-out for that bot,
    // whatever its state; any other listed creature still is.
    {
        CreatureAuraKeepout const* row = FindCreatureAuraKeepout(349, 12222);
        bool const ordered = true;
        CHECK(!CreatureAuraKeepoutApplies(true, row, true, true, ordered));
        CHECK(CreatureAuraKeepoutApplies(true, row, true, true, !ordered));
    }
    std::cout << "  [PASS] attack order exempts only the ordered creature\n";

    // Follow near a keep-out. Sludge at the origin; rings 8 / 9 / 9.5 / 10 / 10.5.
    CreatureAuraKeepout const& sludge = *FindCreatureAuraKeepout(349, 12222);
    using Step = CreatureAuraFollowStep;
    auto plan = [&](bool engaged, float bx, float by, float px, float py, float cx, float cy,
                    float& hx, float& hy) {
        return PlanCreatureAuraFollow(sludge, engaged, bx, by, px, py, cx, cy, hx, hy);
    };
    auto dist = [](float x, float y) { return std::sqrt(x * x + y * y); };
    {
        CreatureAuraFollowRings const r = CreatureAuraFollowRingsFor(sludge);
        CHECK(r.keepout == 8.0f && r.hazard == 9.0f && r.hold == 9.5f);
        CHECK(r.backoff == 10.0f && r.holdOuter == 10.5f && r.release == 10.0f);
        // Every move target sits inside the hold band with margin both ways.
        CHECK(r.hold - r.hazard >= 0.5f && r.holdOuter - r.hold >= 0.5f);
        CHECK(r.backoff - r.hazard >= 0.5f && r.holdOuter - r.backoff >= 0.5f);
        CHECK(r.keepout < r.hazard && r.release > r.hold);
    }
    std::cout << "  [PASS] follow rings nest with 0.5 yd margins\n";

    // Bot east of the sludge, master (goal) 3 yd west of it: every distance
    // from 8 to 10.5 either backs off outward or holds; none follows.
    {
        float hx = 0.0f, hy = 0.0f;
        CHECK(plan(false, 4.0f, 0.0f, -3.0f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::StepOut);
        CHECK(plan(false, 8.0f, 0.0f, -3.0f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::StepOut);
        float const backoff[] = { 8.01f, 8.25f, 8.5f, 8.75f, 8.99f };
        for (float d : backoff)
        {
            hx = hy = 0.0f;
            CHECK(plan(false, d, 0.0f, -3.0f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::BackOff);
            // Outward along the sludge-bot line, to the back-off ring.
            CHECK(std::fabs(hx - 10.0f) < 0.01f && std::fabs(hy) < 0.01f);
        }
        float const hold[] = { 9.0f, 9.25f, 9.49f, 9.5f, 9.51f, 10.0f, 10.5f };
        for (float d : hold)
        {
            CHECK(plan(false, d, 0.0f, -3.0f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::Hold);
            CHECK(plan(true, d, 0.0f, -3.0f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::Hold);
        }
        // Oblique bot positions hold by distance too, not by axis.
        CHECK(plan(false, 9.49f * 0.6f, 9.49f * 0.8f, -3.0f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::Hold);
        CHECK(plan(false, 8.5f * 0.6f, -8.5f * 0.8f, -3.0f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::BackOff);
        CHECK(std::fabs(dist(hx, hy) - 10.0f) < 0.01f && hx > 0.0f && hy < 0.0f);
    }
    std::cout << "  [PASS] 8-10.5 yd: back off below 9, hold from 9 (incl. 9.49), step-out at 8\n";

    // Approach from outside the band: stop on the hold ring on the way in,
    // and the arrival (even a hair short after the height snap) holds.
    {
        float hx = 0.0f, hy = 0.0f;
        CHECK(plan(false, 20.0f, 0.0f, 3.0f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::Approach);
        CHECK(std::fabs(hx - 9.5f) < 0.01f && std::fabs(hy) < 0.01f);
        CHECK(plan(false, 10.6f, 0.0f, 3.0f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::Approach);
        CHECK(std::fabs(hx - 9.5f) < 0.01f);
        // Arrived 0.4 yd short or long of the hold point: hold.
        CHECK(plan(true, 9.1f, 0.0f, 3.0f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::Hold);
        CHECK(plan(true, 9.9f, 0.0f, 3.0f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::Hold);
        // Oblique approach: the entry point lies on the hold ring, bot side.
        CHECK(plan(false, -20.0f, 5.0f, 0.0f, 2.0f, 0.0f, 0.0f, hx, hy) == Step::Approach);
        CHECK(std::fabs(dist(hx, hy) - 9.5f) < 0.01f && hx < 0.0f);
        // The hold point is outside the hazard and the keep-out radius.
        CHECK(!InsideCreatureAuraKeepout(sludge, 9.5f) && 9.5f > CreatureAuraHazardRadius(sludge));
    }
    std::cout << "  [PASS] approach stops on the 9.5 yd ring; arrival +-0.4 yd holds\n";

    // A sludge creeping (2 yd/s, 0.25 yd per tick) toward a bot holding at
    // 9.5 yd, the master 3 yd behind it: hold, back off along the same line
    // before the step-out radius, hold again; never follow, never step out.
    {
        float bx = 9.5f;
        float const by = 0.0f;
        float hx = 0.0f, hy = 0.0f;
        int backOffs = 0;
        float closest = 100.0f;
        for (int i = 0; i <= 40; ++i)
        {
            float const cx = 0.25f * i; // sludge moves east, toward the bot
            float const d = bx - cx;
            closest = std::min(closest, d);
            Step const s = plan(true, bx, by, cx - 3.0f, 0.0f, cx, 0.0f, hx, hy);
            CHECK(s == Step::Hold || s == Step::BackOff);
            CHECK((s == Step::Hold) == (d >= 9.0f));
            if (s == Step::BackOff)
            {
                // On the sludge-bot line, 10 yd from the sludge.
                CHECK(std::fabs(hy) < 0.01f && std::fabs((hx - cx) - 10.0f) < 0.01f);
                bx = hx; // the bot lands there before the next tick
                ++backOffs;
            }
        }
        CHECK(backOffs >= 4);
        CHECK(closest > sludge.keepoutRadius + 0.5f); // never near the step-out
    }
    std::cout << "  [PASS] creeping sludge: hold, back off on the line, hold; no step-out\n";

    // Goal leaving the circle, with hysteresis: not engaged, the goal counts
    // inside up to 9.5 yd; engaged, it must clear 10 yd before follow resumes.
    {
        float hx = 0.0f, hy = 0.0f;
        CHECK(plan(false, 9.5f, 0.0f, 9.4f, 1.0f, 0.0f, 0.0f, hx, hy) != Step::Follow); // goal 9.45
        CHECK(plan(false, 9.5f, 0.0f, -9.8f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::Follow); // goal 9.8, fresh
        CHECK(plan(true, 9.5f, 0.0f, -9.8f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::Hold);    // goal 9.8, engaged
        CHECK(plan(true, 9.5f, 0.0f, -10.0f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::Hold);   // on the release ring
        CHECK(plan(true, 9.5f, 0.0f, -10.01f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::Follow); // left the circle
        CHECK(plan(true, 4.0f, 0.0f, -12.0f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::Follow); // goal gone: the step-out alone
        // Engaged bot outside the band, goal between 9.5 and 10 on its own
        // side: walk to the goal (the straight line never meets the hold ring).
        CHECK(plan(true, 20.0f, 0.0f, 9.8f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::Approach);
        CHECK(std::fabs(hx - 9.8f) < 0.01f && std::fabs(hy) < 0.01f);
        // ...and on the far side: stop where the line meets the hold ring.
        CHECK(plan(true, 20.0f, 0.0f, -9.8f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::Approach);
        CHECK(std::fabs(hx - 9.5f) < 0.01f);
    }
    std::cout << "  [PASS] goal hysteresis: follow resumes only once the goal clears 10 yd\n";

    // Segment entry helper.
    {
        float hx = 0.0f, hy = 0.0f;
        CHECK(SegmentEntersCircle(20.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 9.5f, hx, hy));
        CHECK(std::fabs(hx - 9.5f) < 0.01f);
        CHECK(!SegmentEntersCircle(5.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 9.5f, hx, hy));    // B inside
        CHECK(!SegmentEntersCircle(20.0f, 0.0f, 12.0f, 0.0f, 0.0f, 0.0f, 9.5f, hx, hy));  // stops short
        CHECK(!SegmentEntersCircle(20.0f, 12.0f, -20.0f, 12.0f, 0.0f, 0.0f, 9.5f, hx, hy)); // misses
        CHECK(!SegmentEntersCircle(20.0f, 0.0f, 30.0f, 0.0f, 0.0f, 0.0f, 9.5f, hx, hy));  // circle behind
    }
    std::cout << "  [PASS] segment entry point\n";

    // Chase near a keep-out (live test 2026-10-13: a warrior on "reach melee"
    // chased a sludge to 2-4 yd). Sludge at the origin.
    {
        CreatureAuraKeepout const& sludge = *FindCreatureAuraKeepout(349, 12222);
        using Step = CreatureAuraFollowStep;
        float gx = 0.0f, gy = 0.0f, hx = 0.0f, hy = 0.0f;
        auto dist = [](float x, float y) { return std::sqrt(x * x + y * y); };

        // Goal: `distance` from the target toward the bot; melee (0) = target.
        CreatureAuraChaseGoal(20.0f, 0.0f, 0.0f, 0.0f, 0.0f, gx, gy);
        CHECK(gx == 0.0f && gy == 0.0f);
        CreatureAuraChaseGoal(30.0f, 0.0f, 0.0f, 0.0f, 22.0f, gx, gy);
        CHECK(std::fabs(gx - 22.0f) < 0.01f && std::fabs(gy) < 0.01f);
        CreatureAuraChaseGoal(5.0f, 0.0f, 0.0f, 0.0f, 22.0f, gx, gy); // already closer: stay
        CHECK(std::fabs(gx - 5.0f) < 0.01f);

        // Melee chasing the sludge itself from 20 yd: stop on the 9.5 ring,
        // then hold there; from 8.5 back off to 10; inside 8 the step-out.
        CreatureAuraChaseGoal(20.0f, 0.0f, 0.0f, 0.0f, 0.0f, gx, gy);
        CHECK(PlanCreatureAuraFollow(sludge, false, 20.0f, 0.0f, gx, gy, 0.0f, 0.0f, hx, hy) == Step::Approach);
        CHECK(std::fabs(dist(hx, hy) - 9.5f) < 0.01f);
        float const ax = hx, ay = hy;
        CHECK(PlanCreatureAuraFollow(sludge, true, ax, ay, gx, gy, 0.0f, 0.0f, hx, hy) == Step::Hold);
        CHECK(PlanCreatureAuraFollow(sludge, true, 8.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::BackOff);
        CHECK(std::fabs(dist(hx, hy) - 10.0f) < 0.01f);
        CHECK(PlanCreatureAuraFollow(sludge, true, 2.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::StepOut);

        // The sludge creeps after the bot it has aggro on (2 yd/s, 0.25 yd
        // per tick) while the melee keeps chasing it: never closer than 9.
        float bx = 9.5f, cx = 0.0f;
        for (int tick = 0; tick < 60; ++tick)
        {
            cx += 0.25f;
            CreatureAuraChaseGoal(bx, 0.0f, cx, 0.0f, 0.0f, gx, gy);
            Step const s = PlanCreatureAuraFollow(sludge, true, bx, 0.0f, gx, gy, cx, 0.0f, hx, hy);
            CHECK(s == Step::Hold || s == Step::BackOff);
            if (s == Step::BackOff)
                bx = hx;
            CHECK(bx - cx >= 8.74f); // a hold is re-judged every tick: at most one creep step inside 9
        }

        // A ranged chase (goal 22 yd out) and a melee chase onto a mob 12 yd
        // from the sludge are left to the normal chase.
        CreatureAuraChaseGoal(30.0f, 0.0f, 0.0f, 0.0f, 22.0f, gx, gy);
        CHECK(PlanCreatureAuraFollow(sludge, false, 30.0f, 0.0f, gx, gy, 0.0f, 0.0f, hx, hy) == Step::Follow);
        CreatureAuraChaseGoal(25.0f, 0.0f, 12.0f, 0.0f, 0.0f, gx, gy);
        CHECK(PlanCreatureAuraFollow(sludge, false, 25.0f, 0.0f, gx, gy, 0.0f, 0.0f, hx, hy) == Step::Follow);
    }
    std::cout << "  [PASS] chase: melee on a sludge stops outside the ring and kites it\n";

    // Step-out landing near other keep-outs: keep-out first, pulse relaxed.
    {
        CreatureAuraKeepout const& sludge = *FindCreatureAuraKeepout(349, 12222);
        CHECK(CreatureAuraLandingClearance(sludge, false) == sludge.keepoutRadius);
        CHECK(CreatureAuraLandingClearance(sludge, true) == sludge.pulseRadius);
        CHECK(CreatureAuraLandingClearance(sludge, true) < CreatureAuraLandingClearance(sludge, false));
    }
    std::cout << "  [PASS] step-out landing clears other keep-outs (pulse when relaxed)\n";

    // Late detection: a chase onto the sludge itself is planned from any
    // distance (the target is checked before the 15 yd list), so a melee bot
    // starting 40 yd out is sent to the 9.5 yd ring, never into the pulse.
    {
        CreatureAuraKeepout const& sludge = *FindCreatureAuraKeepout(349, 12222);
        float gx = 0.0f, gy = 0.0f, hx = 0.0f, hy = 0.0f;
        CreatureAuraChaseGoal(40.0f, 0.0f, 0.0f, 0.0f, 0.0f, gx, gy);
        CHECK(PlanCreatureAuraFollow(sludge, false, 40.0f, 0.0f, gx, gy, 0.0f, 0.0f, hx, hy) ==
            CreatureAuraFollowStep::Approach);
        CHECK(std::fabs(hx - 9.5f) < 0.01f && std::fabs(hy) < 0.01f);
    }
    std::cout << "  [PASS] chase onto a sludge from 40 yd stops on the ring\n";

    // Roles: tanks are exempt from the chase hold always, from the step-out
    // and the path hazard only in combat.
    {
        CreatureAuraKeepout const& sludgeRow = *FindCreatureAuraKeepout(349, 12222);
        CreatureAuraKeepout const& poisonRow = *FindCreatureAuraKeepout(533, 16027);
        CHECK(sludgeRow.tankExempt);
        CHECK(CreatureAuraChaseExempt(sludgeRow, true));
        CHECK(!CreatureAuraChaseExempt(sludgeRow, false));
        CHECK(CreatureAuraCombatExempt(sludgeRow, true, true));
        CHECK(!CreatureAuraCombatExempt(sludgeRow, true, false)); // a following tank keeps out
        CHECK(!CreatureAuraCombatExempt(sludgeRow, false, true));
        // #906: Explode one-shot both tanks, so Living Poison gives tanks no pass.
        CHECK(!poisonRow.tankExempt);
        CHECK(!CreatureAuraChaseExempt(poisonRow, true));
        CHECK(!CreatureAuraCombatExempt(poisonRow, true, true));
        CHECK(!CreatureAuraCombatExempt(poisonRow, true, false));
        CHECK(!CreatureAuraCombatExempt(poisonRow, false, true));
        CHECK(CreatureAuraMapHasNonTankExemptRow(533));
        CHECK(!CreatureAuraMapHasNonTankExemptRow(349)); // a tank in combat skips the lookups there
        CHECK(!CreatureAuraMapHasNonTankExemptRow(1));
        // A tank in combat ignores only the exempt entries of a snapshot.
        std::vector<CreatureAuraHazardEntry> const entries = {
            { 0xA1ull, 0.0f, 0.0f, 0.0f, 9.0f, false, true },   // sludge ring
            { 0xA2ull, 30.0f, 0.0f, 0.0f, 11.0f, false, false }, // Living Poison ring
        };
        std::vector<uint64_t> const none;
        CHECK(CreatureAuraEntriesRingAt(entries, 1.0f, 0.0f, 0.0f, 0.0f, none, false) == 0xA1ull);
        CHECK(CreatureAuraEntriesRingAt(entries, 1.0f, 0.0f, 0.0f, 0.0f, none, true) == 0);  // tank: sludge ring is a pass
        CHECK(CreatureAuraEntriesRingAt(entries, 31.0f, 0.0f, 0.0f, 0.0f, none, true) == 0xA2ull); // tank: poison ring is not
        CHECK(CreatureAuraEntriesRingAt(entries, 31.0f, 0.0f, 0.0f, 0.0f, none, false) == 0xA2ull);
    }
    std::cout << "  [PASS] tank exemption\n";

    // Bounded hold: set the target aside after 3 s on the same hostile
    // current target, never under a player's order, never a friend.
    {
        CHECK(!CreatureAuraSetAsideTarget(true, false, 0));
        CHECK(!CreatureAuraSetAsideTarget(true, false, kCreatureAuraChaseHoldMs - 1));
        CHECK(CreatureAuraSetAsideTarget(true, false, kCreatureAuraChaseHoldMs));
        CHECK(!CreatureAuraSetAsideTarget(true, true, 60000));  // ordered: hold, no retarget
        CHECK(!CreatureAuraSetAsideTarget(false, false, 60000)); // a heal reach on a friend
        CHECK(kCreatureAuraSetAsideMs > kCreatureAuraChaseHoldMs);
        // The set-aside book (args: live, isKeepoutCreature, isTank,
        // hitsBotInSight, stillRinged): refused until it runs out; a keep-out
        // creature even while it hits the bot or is not ringed; another mob
        // only while it still stands in a ring and does not hit the bot in
        // sight; tanks never hold one.
        CHECK(CreatureAuraSetAsideHolds(true, true, false, false, true));
        CHECK(CreatureAuraSetAsideHolds(true, true, false, true, true));   // sludge hitting the bot: still refused
        CHECK(CreatureAuraSetAsideHolds(true, true, false, false, false)); // sludge itself stays refused
        CHECK(CreatureAuraSetAsideHolds(true, false, false, false, true)); // ringed mob on the tank: refused
        CHECK(!CreatureAuraSetAsideHolds(true, false, false, false, false)); // mob left the ring: picked again at once
        CHECK(!CreatureAuraSetAsideHolds(true, false, false, true, true));   // ringed mob came out to the bot
        CHECK(!CreatureAuraSetAsideHolds(true, false, false, true, false));
        CHECK(!CreatureAuraSetAsideHolds(true, true, true, false, true));  // tanks pick it back up
        CHECK(!CreatureAuraSetAsideHolds(false, true, false, false, true)); // expired
        CHECK(!CreatureAuraSetAsideHolds(false, false, false, false, true));
    }
    std::cout << "  [PASS] bounded hold then retarget\n";

    // "Still ringed": the release ring of a listed creature (10 yd for the
    // sludge, centre measure) - the ring the chase hold lets go of.
    {
        CreatureAuraKeepout const* sludge = FindCreatureAuraKeepout(349, 12222);
        CHECK(sludge != nullptr);
        CHECK(CreatureAuraSetAsideRinged(*sludge, 0.0f));
        CHECK(CreatureAuraSetAsideRinged(*sludge, 9.5f));
        CHECK(CreatureAuraSetAsideRinged(*sludge, 10.0f));
        CHECK(!CreatureAuraSetAsideRinged(*sludge, 10.1f));
        CHECK(!CreatureAuraSetAsideRinged(*sludge, 40.0f));
        // The test uses only the creature-to-mob distance: a mob dragged 25 yd
        // from the sludge is unringed wherever the bot stands (the wiring is
        // pinned in tools/test_keepout_set_aside_wiring.py).
        CHECK(!CreatureAuraSetAsideRinged(*sludge, 25.0f));
        // A mob the hold lets go of is not held again: past the release ring
        // the chase plans Follow, engaged or not.
        float hx = 0.0f, hy = 0.0f;
        CHECK(PlanCreatureAuraFollow(*sludge, true, 30.0f, 0.0f, 10.1f, 0.0f, 0.0f, 0.0f, hx, hy)
            == CreatureAuraFollowStep::Follow);
    }
    std::cout << "  [PASS] still-ringed test matches the hold's release ring\n";

    // The chase-hold timer starts over for another target and after a pause
    // (combat ended mid-hold), so a later hold never sets a mob aside on its
    // first tick; steady ticks keep the timer.
    {
        CHECK(CreatureAuraChaseHoldRestarts(false, 0));
        CHECK(CreatureAuraChaseHoldRestarts(true, kCreatureAuraChaseHoldGapMs + 1));
        CHECK(!CreatureAuraChaseHoldRestarts(true, 0));
        CHECK(!CreatureAuraChaseHoldRestarts(true, 100));
        CHECK(!CreatureAuraChaseHoldRestarts(true, kCreatureAuraChaseHoldGapMs));
        CHECK(kCreatureAuraChaseHoldGapMs < kCreatureAuraChaseHoldMs);
        // Hold 2 s on mob A, combat ends, 20 s later the hold starts on A
        // again: the timer is new, so the first tick does not set it aside.
        unsigned since = 0, last = 0, now = 0;
        bool sameTarget = false;
        auto tick = [&](unsigned at) -> bool
        {
            now = at;
            if (CreatureAuraChaseHoldRestarts(sameTarget, now - last))
                since = now;
            sameTarget = true;
            last = now;
            return CreatureAuraSetAsideTarget(true, false, now - since);
        };
        for (unsigned t = 1000; t <= 3000; t += 100)
            CHECK(!tick(t)); // 2 s of hold: not yet
        CHECK(!tick(23000));  // the old start (1000) would set it aside here
        CHECK(!tick(24000));
        CHECK(!tick(25900));
        CHECK(tick(26000));   // 3 s of steady hold since 23000
    }
    std::cout << "  [PASS] chase-hold timer resets after a pause\n";

    // Regression (review round 2, 2026-10-13): the tank holds a mob inside a
    // sludge ring; a melee DPS on "dps assist" holds outside, sets it aside
    // after 3 s, and its next pick comes from DpsTargetValue: raid mark, then
    // the group tank's victim, then the tournament over the attackers list.
    // The first two bypass the attackers list (AttackersValue::IgnoreTarget),
    // so before the fix they handed the set-aside mob straight back: hold 3 s,
    // set aside, re-pick, all fight. Now every path asks the book
    // (AttackersValue::IsKeepoutSetAside -> CreatureAuraSetAsideHolds).
    {
        enum { kNone = 0, kRinged = 1, kOther = 2 };
        struct Run { int setAsides; int picks; int otherKills; int idleMs; };
        // gateBypassPaths: the fix. otherExists: a second mob outside any ring.
        // ringedIsSludge: the held mob is the keep-out creature itself.
        // markRinged: the ringed mob carries this bot's raid mark.
        unsigned ringLeftAtMs = ~0u; // the ringed mob stands out of every ring from this time on
        auto simulate = [&ringLeftAtMs](bool gateBypassPaths, bool otherExists, bool ringedIsSludge, bool markRinged,
            unsigned durationMs) -> Run
        {
            std::map<int, unsigned> book; // target -> set aside until (ms)
            auto refused = [&](int id, unsigned now) -> bool
            {
                auto it = book.find(id);
                if (it == book.end())
                    return false;
                // The tank holds the mob: it never hits the DPS bot.
                if (CreatureAuraSetAsideHolds(now < it->second, id == kRinged && ringedIsSludge, false, false,
                        now < ringLeftAtMs))
                    return true;
                book.erase(it);
                return false;
            };
            bool otherAlive = otherExists;
            unsigned otherHitMs = 0;
            auto pick = [&](unsigned now) -> int
            {
                if (markRinged && (!gateBypassPaths || !refused(kRinged, now)))
                    return kRinged;                       // RtiTargetValue branch
                if (!gateBypassPaths || !refused(kRinged, now))
                    return kRinged;                       // GetGroupTankTarget: the tank's victim
                if (!refused(kRinged, now))               // tournament: attackers list, IgnoreTarget
                    return kRinged;
                return otherAlive ? kOther : kNone;
            };
            Run run = { 0, 0, 0, 0 };
            int target = kNone;
            unsigned holdSince = 0;
            for (unsigned now = 0; now < durationMs; now += 100)
            {
                if (target == kNone)
                {
                    target = pick(now);
                    if (target != kNone)
                    {
                        ++run.picks;
                        holdSince = now;
                    }
                }
                if (target == kRinged)
                {
                    // The chase goal is inside the ring: hold, then set aside.
                    // Out of the ring (the tank dragged it out) it is just fought.
                    if ((ringedIsSludge || now < ringLeftAtMs) &&
                        CreatureAuraSetAsideTarget(true, false, now - holdSince))
                    {
                        book[kRinged] = now + kCreatureAuraSetAsideMs;
                        ++run.setAsides;
                        target = kNone;
                    }
                }
                else if (target == kOther)
                {
                    otherHitMs += 100;
                    if (otherHitMs >= 20000) // killed it
                    {
                        otherAlive = false;
                        ++run.otherKills;
                        target = kNone;
                    }
                }
                else
                    run.idleMs += 100;
            }
            return run;
        };

        // Before the fix: the tank's victim comes back every 3 s, all fight.
        Run const loop = simulate(false, true, false, false, 30000);
        CHECK(loop.setAsides >= 9);
        CHECK(loop.otherKills == 0);
        // Fixed: one set-aside, then the bot fights the other mob.
        Run const fixed = simulate(true, true, false, false, 30000);
        CHECK(fixed.setAsides == 1);
        CHECK(fixed.otherKills == 1);
        // The same through a raid mark, and with the sludge itself held.
        CHECK(simulate(false, true, false, true, 30000).setAsides >= 9);
        CHECK(simulate(true, true, false, true, 30000).setAsides == 1);
        CHECK(simulate(true, true, true, false, 30000).setAsides == 1);
        // No other target: the bot holds outside with no target (no pick, no
        // log line) until the set-aside runs out, then holds 3 s and sets it
        // aside again - one cycle per 33 s instead of one every 3 s.
        Run const alone = simulate(true, false, false, false, 30000);
        CHECK(alone.setAsides == 1);
        CHECK(alone.picks == 1);
        CHECK(alone.idleMs >= 26000);
        CHECK(simulate(true, false, false, false, 66000).setAsides == 2);
        CHECK(simulate(false, false, false, false, 30000).setAsides >= 9);

        // "Lift when it leaves the ring" (owner decision 2026-10-13): the
        // tank drags the mob out 10 s in; the bot picks it again at once
        // instead of idling the rest of the 30 s, and sets it aside no more.
        ringLeftAtMs = 10000;
        Run const dragged = simulate(true, false, false, false, 30000);
        CHECK(dragged.setAsides == 1);
        CHECK(dragged.picks == 2);
        CHECK(dragged.idleMs >= 6800 && dragged.idleMs <= 7200); // 3 s hold, set aside at 3 s, lifted at 10 s
        // Still ringed at 10 s (never leaves): stays set aside, as before.
        ringLeftAtMs = ~0u;
        CHECK(simulate(true, false, false, false, 30000).picks == 1);
        // The sludge itself stays refused after the ring "ends" (it is the ring).
        ringLeftAtMs = 10000;
        Run const sludge = simulate(true, false, true, false, 30000);
        CHECK(sludge.setAsides == 1);
        CHECK(sludge.picks == 1);
        // With another mob around, the lift costs nothing: it keeps fighting.
        CHECK(simulate(true, true, false, false, 30000).otherKills == 1);
        ringLeftAtMs = ~0u;
    }
    std::cout << "  [PASS] tank holds a ringed mob: the set-aside is not re-picked through assist or mark\n";

    // Gap closers and end points (Maraudon live run 2, 2026-10-13): a warrior
    // DPS charged a sludge from 21 yd, past every hold, and died in its pulse.
    {
        CreatureAuraKeepout const& row = *FindCreatureAuraKeepout(349, 12222);
        float const hazard = CreatureAuraHazardRadius(row); // 9 yd
        // End points: inside the hazard ring refused, on or past it allowed.
        CHECK(CreatureAuraEndPointBlocked(row, 0.0f));
        CHECK(CreatureAuraEndPointBlocked(row, 2.0f));  // rear flank on the sludge
        CHECK(CreatureAuraEndPointBlocked(row, 8.9f));
        CHECK(!CreatureAuraEndPointBlocked(row, hazard));
        CHECK(!CreatureAuraEndPointBlocked(row, 12.0f));

        // Landing: melee reach from the target on the bot's side.
        float lx = 0.0f, ly = 0.0f;
        CreatureAuraGapCloserLanding(21.0f, 0.0f, 0.0f, 0.0f, 2.0f, lx, ly);
        CHECK(std::fabs(lx - 2.0f) < 0.01f && std::fabs(ly) < 0.01f);
        // A charge onto a mob 3 yd from a sludge (sludge at (-3, 0)) lands
        // 5 yd from it: in the ring. A mob 15 yd from the sludge: clear.
        CHECK(CreatureAuraEndPointBlocked(row, std::fabs(lx - (-3.0f))));
        CHECK(!CreatureAuraEndPointBlocked(row, std::fabs(lx - (-15.0f))));

        // Veto: charge onto the sludge, non-tank, no order -> vetoed.
        CHECK(CreatureAuraGapCloserVetoed(true, true, false, false));
        CHECK(CreatureAuraGapCloserVetoed(true, false, false, false));
        // Charge onto a mob whose landing is in a sludge's ring -> vetoed.
        CHECK(CreatureAuraGapCloserVetoed(false, true, false, false));
        // Tank: allowed (it picks up mobs in the ring).
        CHECK(!CreatureAuraGapCloserVetoed(true, true, true, false));
        CHECK(!CreatureAuraGapCloserVetoed(false, true, true, false));
        // Explicit attack order on that target: today's behaviour.
        CHECK(!CreatureAuraGapCloserVetoed(true, true, false, true));
        CHECK(!CreatureAuraGapCloserVetoed(false, true, false, true));
        // Nothing listed nearby: allowed.
        CHECK(!CreatureAuraGapCloserVetoed(false, false, false, false));

        // Review #882: Charge from 20 yd onto a mob with a sludge 5 yd beyond
        // it. The sludge is 25 yd from the bot, outside the cached 15 yd list
        // (kMaxAoeAvoidRadiusYd); the bounded lookup around the target finds
        // it, and the landing (2 yd short of the mob) is in its ring.
        {
            float const bx = 0.0f, by = 0.0f, tx = 20.0f, ty = 0.0f, sx = 25.0f, sy = 0.0f;
            CHECK(std::hypot(sx - bx, sy - by) > kMaxAoeAvoidRadiusYd);
            float fx = 0.0f, fy = 0.0f;
            CreatureAuraGapCloserLanding(bx, by, tx, ty, 2.0f, fx, fy);
            float const landingToTarget = std::hypot(fx - tx, fy - ty);
            CHECK(std::fabs(landingToTarget - 2.0f) < 0.01f);
            // The shared hazard snapshot of the bot's cell holds it.
            CreatureAuraSnapshotCache cache;
            auto one = [&](float hx, float hy)
            {
                return [=](std::vector<CreatureAuraHazardEntry>& out, float, float)
                    { out.push_back({ 0x55ull, hx, hy, 0.0f, CreatureAuraHazardRadius(row) }); };
            };
            std::vector<uint64_t> const none;
            CreatureAuraSnapshotView view;
            bool const ringed = CreatureAuraSnapshotRinged(cache, CreatureAuraSnapshotKeyFor(349, 1, bx, by), 1000,
                fx, fy, 0.0f, none, one(sx, sy), &view) != 0; // 7 yd
            CHECK(view.refreshed);
            CHECK(std::hypot(sx - view.snap->cx, sy - view.snap->cy) <= CreatureAuraSnapshotRadius(CreatureAuraHazardRadius(row)));
            CHECK(ringed);
            CHECK(CreatureAuraEndPointBlocked(row, std::hypot(sx - fx, sy - fy)));
            CHECK(CreatureAuraGapCloserVetoed(false, ringed, false, false)); // vetoed
            // A sludge 15 yd beyond the mob: outside its widened ring, allowed.
            CHECK(!CreatureAuraEndPointBlocked(row, std::hypot(35.0f - fx, 0.0f - fy)));
            CreatureAuraSnapshotCache farCache;
            CHECK(CreatureAuraSnapshotRinged(farCache, CreatureAuraSnapshotKeyFor(349, 1, bx, by), 1000,
                fx, fy, 0.0f, none, one(35.0f, 0.0f)) == 0);
        }
    }
    std::cout << "  [PASS] gap closers and end points stay out of the ring (tank and order exempt)\n";

    // Review #882 round 2: isUseful() runs every tick the trigger fires, so
    // the landing lookup sits behind a readiness gate and a 1 s memo.
    {
        CreatureAuraGapCloserReadiness ready;
        ready.knowsSpell = true;
        ready.rangeKnown = true;
        ready.distance = 20.0f;
        ready.minRange = 8.0f;
        ready.maxRange = 25.0f;
        CHECK(CreatureAuraGapCloserCouldFire(ready)); // Charge from 20 yd, out of combat

        CreatureAuraGapCloserReadiness r = ready;
        r.onCooldown = true;
        CHECK(!CreatureAuraGapCloserCouldFire(r)); // on cooldown: no lookup
        r = ready;
        r.distance = 30.0f;
        CHECK(!CreatureAuraGapCloserCouldFire(r)); // past 25 yd: no lookup
        r.distance = 25.0f;
        CHECK(!CreatureAuraGapCloserCouldFire(r)); // max range is exclusive, as in isPossible
        r.distance = 7.9f;
        CHECK(!CreatureAuraGapCloserCouldFire(r)); // inside the 8 yd minimum
        r.distance = 8.0f;
        CHECK(CreatureAuraGapCloserCouldFire(r));
        r = ready;
        r.nonCombatSpell = true;
        CHECK(CreatureAuraGapCloserCouldFire(r)); // Charge out of combat
        r.inCombat = true;
        CHECK(!CreatureAuraGapCloserCouldFire(r)); // Charge in combat (Arms chase): no lookup
        r.nonCombatSpell = false;
        CHECK(CreatureAuraGapCloserCouldFire(r)); // Intercept in combat
        r.powerShort = true;
        CHECK(!CreatureAuraGapCloserCouldFire(r)); // Intercept without 10 rage
        r = ready;
        r.lostControl = true;
        CHECK(!CreatureAuraGapCloserCouldFire(r)); // stunned
        r = ready;
        r.knowsSpell = false;
        CHECK(!CreatureAuraGapCloserCouldFire(r)); // not learned
        r = ready;
        r.rangeKnown = false;
        r.distance = 40.0f;
        CHECK(CreatureAuraGapCloserCouldFire(r)); // no range data: never skip the veto on range
    }
    std::cout << "  [PASS] gap-closer veto looks only when the cast could fire\n";

    // Reviews of #995: hazard snapshots shared per (map, instance, 30 yd
    // cell), driven through CreatureAuraSnapshotRinged (the seam the game
    // code calls) with a fake grid visit over an injected creature list.
    {
        CreatureAuraKeepout const& row = kCreatureAuraKeepouts[0];
        float const hazard = CreatureAuraHazardRadius(row);
        CHECK(CreatureAuraMaxHazardRadiusOnMap(349) == hazard); // 9 yd in Maraudon
        CHECK(CreatureAuraMaxHazardRadiusOnMap(0) == 0.0f);     // no listed row: the caller never visits
        // The fake world: creatures per instance; a visit returns those within
        // the visit radius of the cell centre, like the grid visit.
        std::map<uint32_t, std::vector<CreatureAuraHazardEntry>> world;
        int visits = 0;
        auto fillFor = [&](uint32_t instance)
        {
            return [&, instance](std::vector<CreatureAuraHazardEntry>& out, float cx, float cy)
            {
                ++visits;
                for (CreatureAuraHazardEntry const& e : world[instance])
                    if (std::hypot(e.x - cx, e.y - cy) <= CreatureAuraSnapshotRadius(hazard))
                        out.push_back(e);
            };
        };
        std::vector<uint64_t> const none;
        auto key = [](uint32_t instance, float x, float y) { return CreatureAuraSnapshotKeyFor(349, instance, x, y); };

        // Coverage: a bot anywhere in its cell, a point up to 30 yd from it,
        // and any creature whose widest ring holds that point is visited.
        for (float bx : { 0.01f, 15.0f, 29.99f })
            for (float by : { 0.01f, 15.0f, 29.99f })
                for (float ang = 0.0f; ang < 6.3f; ang += 0.5f)
                {
                    float const px = bx + std::cos(ang) * kCreatureAuraSnapshotReachYd;
                    float const py = by + std::sin(ang) * kCreatureAuraSnapshotReachYd;
                    float const reach = hazard + kCreatureAuraSnapshotMaxDriftYd - 0.01f;
                    world[1] = { { 0x10ull, px + std::cos(ang) * reach, py + std::sin(ang) * reach, 0.0f, hazard } };
                    CreatureAuraSnapshotCache c;
                    CreatureAuraSnapshotView v;
                    CreatureAuraSnapshotRinged(c, key(1, bx, by), 100, px, py, 0.0f, none, fillFor(1), &v);
                    CHECK(v.snap && v.snap->cx == 15.0f && v.snap->cy == 15.0f);
                    CHECK(CreatureAuraSnapshotCovers(*v.snap, px, py));
                    CHECK(v.snap->entries.size() == 1);
                }

        // Count: five bots of a party in one cell, each running rear flank
        // (2 points), set behind (2 tries), move out of collision (9 tries)
        // and Charge + Intercept within 500 ms: one visit.
        world[7] = { { 0x77ull, 24.0f, 0.0f, 0.0f, hazard } };
        world[8] = {};
        CreatureAuraSnapshotCache cache;
        visits = 0;
        float const party[5][2] = { { 1.0f, 1.0f }, { 4.0f, 2.0f }, { 2.0f, 6.0f }, { 8.0f, 3.0f }, { 5.0f, 9.0f } };
        for (int bot = 0; bot < 5; ++bot)
        {
            CreatureAuraSnapshotBeginTick(cache); // one world update per bot here
            for (int check = 0; check < 2 + 2 + 9 + 2; ++check)
                CreatureAuraSnapshotRinged(cache, key(7, party[bot][0], party[bot][1]), 10000 + uint32_t(bot) * 90,
                    17.0f, 0.0f, 0.0f, none, fillFor(7));
        }
        CHECK(visits == 1);
        CHECK(cache.entries.size() == 1);
        // Another instance of the same map: its own entry and visit, and it
        // never answers with the other instance's creatures.
        CHECK(CreatureAuraSnapshotRinged(cache, key(8, 1.0f, 1.0f), 10400, 17.0f, 0.0f, 0.0f, none, fillFor(8)) == 0);
        CHECK(visits == 2);
        CHECK(cache.entries.size() == 2);
        CHECK(CreatureAuraSnapshotRinged(cache, key(7, 3.0f, 3.0f), 10450, 17.0f, 0.0f, 0.0f, none, fillFor(7)) == 0x77ull);
        CHECK(visits == 2); // same instance and cell inside the window: no visit
        // The next window refreshes once.
        CreatureAuraSnapshotBeginTick(cache);
        CreatureAuraSnapshotRinged(cache, key(7, 3.0f, 3.0f), 10000 + kCreatureAuraSnapshotMs, 17.0f, 0.0f, 0.0f, none, fillFor(7));
        CreatureAuraSnapshotRinged(cache, key(7, 4.0f, 3.0f), 10000 + kCreatureAuraSnapshotMs, 17.0f, 0.0f, 0.0f, none, fillFor(7));
        CHECK(visits == 3);
        // Entries not refreshed for 5 s are pruned.
        CreatureAuraSnapshotBeginTick(cache);
        CreatureAuraSnapshotRinged(cache, key(7, 3.0f, 3.0f), 10000 + kCreatureAuraSnapshotMs + kCreatureAuraSnapshotPruneMs,
            17.0f, 0.0f, 0.0f, none, fillFor(7));
        CHECK(cache.entries.size() == 1);

        // Guid skip: a creature in the bot's live list (or the target, or the
        // explicit attack target) is judged live, never by the snapshot.
        CreatureAuraSnapshotCache skipCache;
        CHECK(CreatureAuraSnapshotRinged(skipCache, key(7, 0.0f, 0.0f), 20000, 17.0f, 0.0f, 0.0f, { 0x77ull }, fillFor(7)) == 0);
        CHECK(CreatureAuraSnapshotRinged(skipCache, key(7, 0.0f, 0.0f), 20000, 17.0f, 0.0f, 0.0f, none, fillFor(7)) == 0x77ull);

        // Review round 3 (#882 distances unchanged): a target 11 yd from a
        // sludge in the bot's live list. The live check (exact 9 yd ring)
        // allows the flank; the snapshot skips the listed sludge, so no
        // widened ring refuses it.
        float const sludgeX = 5.0f, sludgeY = 0.0f, flankX = 16.0f, flankY = 0.0f;
        CHECK(!CreatureAuraEndPointBlocked(row, std::hypot(flankX - sludgeX, flankY - sludgeY))); // live: allowed
        world[9] = { { 0x99ull, sludgeX, sludgeY, 0.0f, hazard } };
        CreatureAuraSnapshotCache liveCache;
        CHECK(CreatureAuraSnapshotRinged(liveCache, key(9, 0.0f, 0.0f), 20000, flankX, flankY, 0.0f, { 0x99ull }, fillFor(9)) == 0);
        // Review round 4: drift by the snapshot's actual age. The same
        // sludge as a snapshot-only creature, standing still, in a snapshot
        // taken this tick: no widening, the same 9 yd decision as the live
        // check (allowed at 11 yd, refused inside 9 yd).
        CHECK(CreatureAuraSnapshotRinged(liveCache, key(9, 0.0f, 0.0f), 20000, flankX, flankY, 0.0f, none, fillFor(9)) == 0);
        CHECK(CreatureAuraSnapshotRinged(liveCache, key(9, 0.0f, 0.0f), 20000, sludgeX + 8.9f, sludgeY, 0.0f, none, fillFor(9)) == 0x99ull);
        // 400 ms later the snapshot widens by 2.8 yd: 11 yd now counts.
        CHECK(CreatureAuraSnapshotRinged(liveCache, key(9, 0.0f, 0.0f), 20400, flankX, flankY, 0.0f, none, fillFor(9)) == 0x99ull);

        // Height: a sludge 10 yd above the point (another floor) never rings it.
        world[10] = { { 0xA0ull, 24.0f, 0.0f, 10.0f, hazard } };
        CreatureAuraSnapshotCache heightCache;
        CHECK(CreatureAuraSnapshotRinged(heightCache, key(10, 0.0f, 0.0f), 20000, 17.0f, 0.0f, 0.0f, none, fillFor(10)) == 0);
        CHECK(CreatureAuraSnapshotRinged(heightCache, key(10, 0.0f, 0.0f), 20000, 17.0f, 0.0f,
            10.0f - kCreatureAuraSnapshotHeightYd, none, fillFor(10)) == 0xA0ull);

        // Uncovered: a point outside the snapshot's cover is not called
        // ringed by it (the live list alone decides, as in #882).
        world[11] = { { 0xB0ull, 68.0f, 15.0f, 0.0f, hazard } };
        CreatureAuraSnapshotCache coverCache;
        CreatureAuraSnapshotView cv;
        CHECK(CreatureAuraSnapshotRinged(coverCache, key(11, 15.0f, 15.0f), 20000, 70.0f, 15.0f, 0.0f, none, fillFor(11), &cv) == 0);
        CHECK(cv.snap && cv.snap->entries.size() == 1 && !CreatureAuraSnapshotCovers(*cv.snap, 70.0f, 15.0f));

        // Budget: ten occupied cells in one world update: 4 refreshes; the
        // other six get no snapshot (live list only) and count as misses.
        world[12] = {};
        CreatureAuraSnapshotCache budget;
        CreatureAuraSnapshotBeginTick(budget);
        visits = 0;
        int misses = 0, liveOnly = 0;
        for (int cell = 0; cell < 10; ++cell)
        {
            CreatureAuraSnapshotView v;
            CreatureAuraSnapshotRinged(budget, key(12, 30.0f * float(cell) + 1.0f, 1.0f), 30000,
                30.0f * float(cell) + 2.0f, 1.0f, 0.0f, none, fillFor(12), &v);
            misses += v.budgetMiss ? 1 : 0;
            liveOnly += v.snap ? 0 : 1;
        }
        CHECK(visits == int(kKeepoutRefreshesPerTick));
        CHECK(misses == 10 - int(kKeepoutRefreshesPerTick));
        CHECK(liveOnly == 10 - int(kKeepoutRefreshesPerTick));
        // A later world update whose budget four other cells spend first: a
        // stale entry younger than 1500 ms is reused, its drift scaled by age.
        CreatureAuraSnapshotBeginTick(budget);
        visits = 0;
        for (int cell = 4; cell < 8; ++cell) // spend the budget on four fresh cells
            CreatureAuraSnapshotRinged(budget, key(12, 30.0f * float(cell) + 1.0f, 1.0f), 31000,
                30.0f * float(cell) + 2.0f, 1.0f, 0.0f, none, fillFor(12));
        CHECK(visits == 4);
        CreatureAuraSnapshotView sv;
        // Cell 0's entry is 1000 ms old (taken at 30000, empty): reused, drift 7 yd.
        CHECK(CreatureAuraSnapshotRinged(budget, key(12, 1.0f, 1.0f), 31000, 1.0f, 1.0f, 0.0f, none, fillFor(12), &sv) == 0);
        CHECK(visits == 4 && sv.budgetMiss && sv.snap);
        CHECK(std::fabs(sv.driftYd - 2.0f * kCreatureAuraSnapshotDriftYd) < 0.01f);
        // Past 1500 ms it is not reused: live list only.
        CreatureAuraSnapshotBeginTick(budget);
        for (int cell = 4; cell < 8; ++cell)
            CreatureAuraSnapshotRinged(budget, key(12, 30.0f * float(cell) + 1.0f, 1.0f), 31600,
                30.0f * float(cell) + 2.0f, 1.0f, 0.0f, none, fillFor(12));
        CHECK(CreatureAuraSnapshotRinged(budget, key(12, 1.0f, 1.0f), 31600, 1.0f, 1.0f, 0.0f, none, fillFor(12), &sv) == 0);
        CHECK(sv.budgetMiss && !sv.snap);
        // Drift by actual age: 0 this tick, 3.5 yd per 500 ms, capped at 10.5 yd.
        CHECK(CreatureAuraSnapshotDriftFor(0) == 0.0f);
        CHECK(std::fabs(CreatureAuraSnapshotDriftFor(250) - 1.75f) < 0.001f);
        CHECK(std::fabs(CreatureAuraSnapshotDriftFor(500) - kCreatureAuraSnapshotDriftYd) < 0.001f);
        CHECK(std::fabs(CreatureAuraSnapshotDriftFor(1499) - 10.493f) < 0.01f);
        CHECK(CreatureAuraSnapshotDriftFor(3000) == kCreatureAuraSnapshotMaxDriftYd);
        CHECK(kCreatureAuraSnapshotMaxDriftYd == 3.0f * kCreatureAuraSnapshotDriftYd);

        // Review of 0cbfa6a8: only a creature moving at the visit gets the age
        // drift. A still sludge in a 1 s old snapshot (reused on a busy tick)
        // keeps its exact 9 yd ring: a landing 9.5 yd away is allowed; the
        // same sludge moving at the visit counts out to 9 + 7 yd.
        {
            CreatureAuraSnapshotCache still;
            CreatureAuraHazardEntry sludge{ 0xE0ull, 109.5f, 1.0f, 0.0f, hazard };
            sludge.moving = false;
            world[16] = { sludge };
            CreatureAuraSnapshotBeginTick(still);
            CreatureAuraSnapshotRinged(still, key(16, 91.0f, 1.0f), 70000, 100.0f, 1.0f, 0.0f, none, fillFor(16));
            // One second later, budget spent by four other cells.
            CreatureAuraSnapshotBeginTick(still);
            for (int cell = 0; cell < 4; ++cell)
                CreatureAuraSnapshotRinged(still, key(16, 30.0f * float(cell) + 200.0f, 1.0f), 71000,
                    30.0f * float(cell) + 201.0f, 1.0f, 0.0f, none, fillFor(16));
            CreatureAuraSnapshotView sv1;
            CHECK(CreatureAuraSnapshotRinged(still, key(16, 91.0f, 1.0f), 71000, 100.0f, 1.0f, 0.0f, none, fillFor(16), &sv1) == 0);
            CHECK(sv1.budgetMiss && sv1.snap && std::fabs(sv1.driftYd - 7.0f) < 0.01f); // 1 s old, 7 yd drift unused
            CHECK(CreatureAuraSnapshotRinged(still, key(16, 91.0f, 1.0f), 71000, 101.0f, 1.0f, 0.0f, none, fillFor(16)) == 0xE0ull); // 8.5 yd: in the ring
            CHECK(CreatureAuraLandingRinged(still, key(16, 91.0f, 1.0f), 71000, 100.0f, 1.0f, 0.0f, none, fillFor(16),
                [](float, float) { return std::vector<CreatureAuraHazardEntry>(); }) == 0); // gap closer allowed
            // The same sludge, moving at the visit.
            CreatureAuraSnapshotCache mover;
            sludge.moving = true;
            world[16] = { sludge };
            CreatureAuraSnapshotRinged(mover, key(16, 91.0f, 1.0f), 70000, 100.0f, 1.0f, 0.0f, none, fillFor(16));
            CHECK(CreatureAuraSnapshotRinged(mover, key(16, 91.0f, 1.0f), 70400, 100.0f, 1.0f, 0.0f, none, fillFor(16)) == 0xE0ull);
        }

        // Clock wrap: the entry goes stale on time, it never sticks.
        CreatureAuraSnapshotCache wrap;
        CreatureAuraSnapshotView wv;
        CreatureAuraSnapshotRinged(wrap, key(7, 0.0f, 0.0f), 0xFFFFFF00u, 0.0f, 0.0f, 0.0f, none, fillFor(7), &wv);
        CHECK(!CreatureAuraSnapshotStale(*wv.snap, 0x00000010u)); // 272 ms later
        CHECK(CreatureAuraSnapshotStale(*wv.snap, 0x00000200u));  // 768 ms later
        CHECK(!CreatureAuraSnapshotCovers(CreatureAuraHazardSnapshot(), 0.0f, 0.0f));

        // An end point 24 yd away: bot at the origin, target 15 yd east,
        // set-behind end point 2 yd past it, sludge 7 yd further (24 yd from
        // the bot, outside the 15 yd list). The snapshot sees it.
        CHECK(std::hypot(24.0f, 0.0f) > kMaxAoeAvoidRadiusYd);
        CreatureAuraSnapshotCache endCache;
        CHECK(CreatureAuraSnapshotRinged(endCache, key(7, 0.0f, 0.0f), 40000, 17.0f, 0.0f, 0.0f, none, fillFor(7)) == 0x77ull);

        // A hazard that walks into the landing ring inside the window: the
        // snapshot put it 11 yd from the landing (outside the 9 yd ring, and
        // allowed while the snapshot is this tick's); 400 ms later it may
        // have walked 2.8 yd, to 8.2 yd. The snapshot is still current, and
        // the drift for its age catches it.
        float const lx = 50.0f, ly = 0.0f;
        world[13] = { { 0x88ull, lx + 11.0f, ly, 0.0f, hazard } };
        CreatureAuraSnapshotCache driftCache;
        CHECK(!CreatureAuraEndPointBlocked(row, 11.0f));           // outside the ring at the visit
        CHECK(CreatureAuraSnapshotRinged(driftCache, key(13, 40.0f, 0.0f), 50000, lx, ly, 0.0f, none, fillFor(13)) == 0);
        float const walked = 11.0f - CreatureAuraSnapshotDriftFor(400); // 8.2 yd 400 ms later
        CHECK(CreatureAuraEndPointBlocked(row, walked));            // inside the ring now
        CHECK(CreatureAuraSnapshotRinged(driftCache, key(13, 40.0f, 0.0f), 50400, lx, ly, 0.0f, none, fillFor(13)) == 0x88ull);
        // Past ring + drift the creature cannot have reached the ring yet.
        world[14] = { { 0x89ull, lx + hazard + kCreatureAuraSnapshotDriftYd, ly, 0.0f, hazard } };
        CreatureAuraSnapshotCache clearCache;
        CHECK(CreatureAuraSnapshotRinged(clearCache, key(14, 40.0f, 0.0f), 50000, lx, ly, 0.0f, none, fillFor(14)) == 0);
        CHECK(CreatureAuraSnapshotRinged(clearCache, key(14, 40.0f, 0.0f), 50499, lx, ly, 0.0f, none, fillFor(14)) == 0);

        // Review round 4: a gap closer never fails open on a busy tick. Four
        // new cells spend the tick's budget; the fifth new cell has no
        // snapshot, so its landing gets #882's one bounded lookup around it,
        // and a sludge 7 yd from the landing still vetoes the charge.
        world[15] = {};
        CreatureAuraSnapshotCache busy;
        CreatureAuraSnapshotBeginTick(busy);
        visits = 0;
        for (int cell = 0; cell < 4; ++cell)
            CreatureAuraSnapshotRinged(busy, key(15, 30.0f * float(cell) + 1.0f, 1.0f), 60000,
                30.0f * float(cell) + 2.0f, 1.0f, 0.0f, none, fillFor(15));
        CHECK(visits == 4);
        float const cx5 = 30.0f * 4.0f + 1.0f, landX = cx5 + 18.0f, landY = 1.0f;
        int lookups = 0;
        auto lookup = [&](float x, float y)
        {
            ++lookups; // the one bounded grid visit around the landing (counted on keepoutScans)
            std::vector<CreatureAuraHazardEntry> found;
            CreatureAuraHazardEntry const sludge{ 0xD0ull, landX + 7.0f, landY, 0.0f, hazard };
            if (std::hypot(sludge.x - x, sludge.y - y) <= hazard)
                found.push_back(sludge);
            return found;
        };
        CreatureAuraSnapshotView lv;
        bool lookedUp = false;
        uint64_t const vetoBy = CreatureAuraLandingRinged(busy, key(15, cx5, 1.0f), 60000, landX, landY, 0.0f, none,
            fillFor(15), lookup, &lv, &lookedUp);
        CHECK(visits == 4 && lv.budgetMiss && !lv.snap); // no snapshot for the fifth cell
        CHECK(lookedUp && lookups == 1);
        CHECK(vetoBy == 0xD0ull);
        CHECK(CreatureAuraGapCloserVetoed(false, vetoBy != 0, false, false));
        // The live lookup keeps #882's exact ring: a sludge 9.5 yd away passes,
        // and the explicit attack target is skipped.
        auto lookupAt = [&](float dist)
        {
            return [=](float, float)
                { return std::vector<CreatureAuraHazardEntry>{ { 0xD1ull, landX + dist, landY, 0.0f, hazard } }; };
        };
        CHECK(CreatureAuraLandingRinged(busy, key(15, cx5, 1.0f), 60000, landX, landY, 0.0f, none, fillFor(15), lookupAt(9.5f)) == 0);
        CHECK(CreatureAuraLandingRinged(busy, key(15, cx5, 1.0f), 60000, landX, landY, 0.0f, { 0xD1ull }, fillFor(15), lookupAt(5.0f)) == 0);
        // Same height filter as the snapshot: a sludge 10 yd above the landing
        // (another floor) does not veto, one 4 yd above does.
        auto lookupAbove = [=](float dz)
        {
            return [=](float, float)
                { return std::vector<CreatureAuraHazardEntry>{ { 0xD2ull, landX + 5.0f, landY, dz, hazard } }; };
        };
        CHECK(CreatureAuraLandingRinged(busy, key(15, cx5, 1.0f), 60000, landX, landY, 0.0f, none, fillFor(15), lookupAbove(10.0f)) == 0);
        CHECK(CreatureAuraLandingRinged(busy, key(15, cx5, 1.0f), 60000, landX, landY, 0.0f, none, fillFor(15), lookupAbove(4.0f)) == 0xD2ull);
        // With a usable snapshot the landing never looks up.
        lookups = 0;
        CHECK(CreatureAuraLandingRinged(busy, key(15, 1.0f, 1.0f), 60000, 2.0f, 1.0f, 0.0f, none, fillFor(15), lookup,
            nullptr, &lookedUp) == 0);
        CHECK(!lookedUp && lookups == 0);
    }
    std::cout << "  [PASS] shared hazard snapshot: per cell and instance, budget, live skip, height, cover, drift\n";


    std::cout << "All creature aura keep-out tests passed.\n";
    return 0;
}
