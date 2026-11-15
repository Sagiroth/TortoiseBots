// Standalone test for the abandoned-instance bot evacuation policy.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_abandoned_instance_policy.cpp -o /tmp/test_abandoned_instance_policy
//   /tmp/test_abandoned_instance_policy

#include "../runtime/AbandonedInstancePolicy.h"

#include <cstdio>
#include <cstdlib>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static AbandonedInstanceFacts MakeAbandonedBase()
{
    AbandonedInstanceFacts f;
    f.isHeadless = true;
    f.inWorld = true;
    f.beingTeleported = false;
    f.alive = true;
    f.inBattleground = false;
    f.onInstanceMap = true;
    f.randomOrAutonomous = true;
    f.inWorldLifecycle = true;
    f.hired = false;
    f.pinned = false;
    f.dungeonCrew = false;
    f.groupHasRealPlayer = false;
    f.hasMaster = false;
    f.masterInSameInstance = false;
    return f;
}

int main()
{
    // Base abandoned random/autonomous bot inside an instance is eligible.
    CHECK(IsAbandonedInInstance(MakeAbandonedBase()));

    // Master set, but master is offline or in another map/instance -> eligible.
    {
        AbandonedInstanceFacts f = MakeAbandonedBase();
        f.hasMaster = true;
        f.masterInSameInstance = false;
        CHECK(IsAbandonedInInstance(f));
    }

    // Master is in the same instance -> stays.
    {
        AbandonedInstanceFacts f = MakeAbandonedBase();
        f.hasMaster = true;
        f.masterInSameInstance = true;
        CHECK(!IsAbandonedInInstance(f));
    }

    // Re-invited to a group with a real player -> stays.
    {
        AbandonedInstanceFacts f = MakeAbandonedBase();
        f.groupHasRealPlayer = true;
        CHECK(!IsAbandonedInInstance(f));
    }

    // Hired companions, owned non-autonomous alts, pinned bots, and LFG dungeon crews are excluded.
    {
        AbandonedInstanceFacts f = MakeAbandonedBase();
        f.hired = true;
        CHECK(!IsAbandonedInInstance(f));
    }
    {
        AbandonedInstanceFacts f = MakeAbandonedBase();
        f.randomOrAutonomous = false;
        CHECK(!IsAbandonedInInstance(f));
    }
    {
        AbandonedInstanceFacts f = MakeAbandonedBase();
        f.pinned = true;
        CHECK(!IsAbandonedInInstance(f));
    }
    {
        AbandonedInstanceFacts f = MakeAbandonedBase();
        f.dungeonCrew = true;
        CHECK(!IsAbandonedInInstance(f));
    }

    // Overworld, battleground, dead, teleporting, or non-headless/offline states are excluded.
    {
        AbandonedInstanceFacts f = MakeAbandonedBase();
        f.onInstanceMap = false;
        CHECK(!IsAbandonedInInstance(f));
    }
    {
        AbandonedInstanceFacts f = MakeAbandonedBase();
        f.inBattleground = true;
        CHECK(!IsAbandonedInInstance(f));
    }
    {
        AbandonedInstanceFacts f = MakeAbandonedBase();
        f.alive = false;
        CHECK(!IsAbandonedInInstance(f));
    }
    {
        AbandonedInstanceFacts f = MakeAbandonedBase();
        f.beingTeleported = true;
        CHECK(!IsAbandonedInInstance(f));
    }
    {
        AbandonedInstanceFacts f = MakeAbandonedBase();
        f.inWorld = false;
        CHECK(!IsAbandonedInInstance(f));
    }
    {
        AbandonedInstanceFacts f = MakeAbandonedBase();
        f.isHeadless = false;
        CHECK(!IsAbandonedInInstance(f));
    }
    {
        AbandonedInstanceFacts f = MakeAbandonedBase();
        f.inWorldLifecycle = false;
        CHECK(!IsAbandonedInInstance(f));
    }

    // Grace period boundaries.
    CHECK(!AbandonedInstanceGraceElapsed(1000, 1089, 90));
    CHECK(AbandonedInstanceGraceElapsed(1000, 1090, 90));
    CHECK(AbandonedInstanceGraceElapsed(1000, 1200, 90));
    CHECK(!AbandonedInstanceGraceElapsed(1000, 5000, 0));
    CHECK(!AbandonedInstanceGraceElapsed(2000, 1000, 90));

    // Orphaned bot-only group policy (e.g. LFT fillers after player leaves).
    auto makeOrphanedGroupBase = []() {
        OrphanedBotGroupFacts f;
        f.isHeadless = true;
        f.inWorld = true;
        f.beingTeleported = false;
        f.inBattleground = false;
        f.randomBot = true;
        f.inWorldLifecycle = true;
        f.hasMaster = false;
        f.hired = false;
        f.pinned = false;
        f.dungeonCrew = false;
        f.commandedGroup = false;
        f.inGroup = true;
        f.groupHasRealPlayer = false;
        return f;
    };

    CHECK(IsOrphanedInBotGroup(makeOrphanedGroupBase()));

    {
        OrphanedBotGroupFacts f = makeOrphanedGroupBase();
        f.groupHasRealPlayer = true;
        CHECK(!IsOrphanedInBotGroup(f));
    }
    {
        OrphanedBotGroupFacts f = makeOrphanedGroupBase();
        f.inGroup = false;
        CHECK(!IsOrphanedInBotGroup(f));
    }
    {
        OrphanedBotGroupFacts f = makeOrphanedGroupBase();
        f.dungeonCrew = true;
        CHECK(!IsOrphanedInBotGroup(f));
    }
    {
        OrphanedBotGroupFacts f = makeOrphanedGroupBase();
        f.commandedGroup = true;
        CHECK(!IsOrphanedInBotGroup(f));
    }
    {
        OrphanedBotGroupFacts f = makeOrphanedGroupBase();
        f.hired = true;
        CHECK(!IsOrphanedInBotGroup(f));
    }
    {
        OrphanedBotGroupFacts f = makeOrphanedGroupBase();
        f.randomBot = false; // owned alt
        CHECK(!IsOrphanedInBotGroup(f));
    }
    {
        OrphanedBotGroupFacts f = makeOrphanedGroupBase();
        f.hasMaster = true;
        CHECK(!IsOrphanedInBotGroup(f));
    }
    {
        OrphanedBotGroupFacts f = makeOrphanedGroupBase();
        f.pinned = true;
        CHECK(!IsOrphanedInBotGroup(f));
    }
    {
        OrphanedBotGroupFacts f = makeOrphanedGroupBase();
        f.inBattleground = true;
        CHECK(!IsOrphanedInBotGroup(f));
    }

    std::printf("test_abandoned_instance_policy: %d checks passed\n", checks);
    return 0;
}
