#include "../ai/playerbot/RaidBombRunoutPolicy.h"
#include <cassert>
#include <iostream>

int main()
{
    using namespace ai;
    RaidBombRunoutPlan plan;
    assert(!plan.Contains({ 0, 0 }, 40));
    assert(plan.Begin(20475, 100, 409, 1, { 100, 100 }));
    RaidRunoutPoint const first = plan.Candidate(32, 0);
    assert(first.x == 132 && first.y == 100);
    // Following raid members and repeated reactions cannot shift the origin.
    for (int tick = 0; tick < 100; ++tick)
    {
        assert(!plan.Begin(20475, 100, 409, 1, { 132.0f + tick, 100 }));
        assert(plan.Candidate(32, 0).x == 132);
        assert(plan.Contains(first, 40));
        assert(!plan.Contains({ 141, 100 }, 40));
    }
    assert(plan.Contains({ 140, 100 }, 40));
    // Check every member, not just the tank/master or a centroid.
    assert(RaidBombClearOfGroup(first, {{ 100, 100 }}, 30));
    assert(!RaidBombClearOfGroup(first, {{ 100, 100 }, { 135, 100 }}, 30));
    assert(!RaidBombClearOfGroup(first, {first}, 30));
    assert(RaidBombClearOfGroup(first, {}, 30));
    assert(RaidBombClearOfGroup({ 30, 0 }, {{ 0, 0 }}, 30));
    assert(!RaidBombClearOfGroup({ 29.99f, 0 }, {{ 0, 0 }}, 30));
    // A dispersed raid can leave no safe candidate; never expand the budget.
    for (float radius : {32.0f, 36.0f, 40.0f})
        for (int heading = 0; heading < 16; ++heading)
        {
            auto point = plan.Candidate(radius, heading * 6.2831853f / 16);
            assert(plan.Contains(point, 40.01f));
            assert(!RaidBombClearOfGroup(point, {point}, 30));
        }
    // New application, instance, spell and explicit reset start new plans.
    assert(plan.Begin(20475, 101, 409, 1, { 200, 200 }));
    assert(plan.Candidate(32, 0).x == 232);
    assert(plan.Begin(20475, 101, 409, 2, { 300, 300 }));
    assert(plan.Begin(18173, 101, 469, 2, { 400, 400 }));
    plan.Reset();
    assert(!plan.Contains({ 400, 400 }, 40));
    assert(plan.Begin(18173, 101, 469, 2, { 500, 500 }));
    // Safe endpoints across a boss must not hide an unsafe path segment.
    assert(RaidRunoutSegmentDistanceSquared({-30, 0}, {30, 0}, {0, 0}) == 0);
    assert(RaidRunoutSegmentDistanceSquared({10, 0}, {30, 0}, {0, 0}) == 100);
    assert(RaidRunoutSegmentDistanceSquared({-30, 30}, {30, 30}, {0, 0}) == 900);
    assert(RaidRunoutSegmentDistanceSquared({10, 0}, {10, 0}, {0, 0}) == 100);
    std::cout << "Raid bomb runout policy: passed\n";
}
