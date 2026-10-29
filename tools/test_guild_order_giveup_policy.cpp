// Standalone test for runtime/GuildOrderGiveUpPolicy.h (guild order zone moves
// and the player-chosen farm time). No core, no DB: feeds the policy plain
// counters.
#include "../runtime/GuildOrderGiveUpPolicy.h"

#include <cstdio>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    CHECK(kZoneMoveWorkedSeconds == 3600);
    CHECK(kZoneMoveDeaths == 3);
    CHECK(kMinOrderHours == 1);
    CHECK(kMaxOrderHours == 24);
    CHECK(kDefaultOrderHours == 1);

    // Below both thresholds: keep working in this zone.
    CHECK(ClassifyZoneMove(0, 0) == ZoneMoveTrigger::None);
    CHECK(ClassifyZoneMove(3599, 2) == ZoneMoveTrigger::None);

    // Three deaths in the zone move, even with fresh progress.
    CHECK(ClassifyZoneMove(0, 3) == ZoneMoveTrigger::Deaths);
    CHECK(ClassifyZoneMove(3599, 3) == ZoneMoveTrigger::Deaths);
    // The pick wraps around the plan: 0 -> 1 -> 2 -> 0.
    CHECK(NextZonePick(false, 0, 0, 3) == 0);
    CHECK(NextZonePick(false, 0, 1, 3) == 1);
    CHECK(NextZonePick(false, 0, 2, 3) == 2);
    CHECK(NextZonePick(false, 0, 3, 3) == 0);
    CHECK(NextZonePick(false, 0, 4, 3) == 1);
    // A listed zone moves to the one after it; a single-zone plan stays.
    CHECK(NextZonePick(true, 0, 0, 3) == 1);
    CHECK(NextZonePick(true, 2, 0, 3) == 0);
    CHECK(NextZonePick(false, 0, 7, 1) == 0);
    // An empty plan picks nothing: the caller ends the order as no source.
    CHECK(NextZonePick(false, 0, 0, 0) == 0);
    CHECK(ClassifyZoneMove(3600, 0) == ZoneMoveTrigger::NoProgress);
    CHECK(ClassifyZoneMove(7200, 2) == ZoneMoveTrigger::NoProgress);

    // Deaths win when both thresholds hit together.
    CHECK(ClassifyZoneMove(3600, 5) == ZoneMoveTrigger::Deaths);

    // The order ends once the bot worked its chosen hours in total.
    CHECK(!IsOrderTimeout(3599, 1));
    CHECK(IsOrderTimeout(3600, 1));
    CHECK(!IsOrderTimeout(3600, 2));
    CHECK(IsOrderTimeout(2 * 3600, 2));
    CHECK(IsOrderTimeout(24 * 3600, 24));
    CHECK(!IsOrderTimeout(23 * 3600 + 3599, 24));
    // Out-of-range hours fall back to one hour.
    CHECK(IsOrderTimeout(3600, 0));
    CHECK(IsOrderTimeout(3600, 99));

    // Reported progress never drops.
    CHECK(ReportedHave(0, 5, true, 0) == 5);
    CHECK(ReportedHave(0, 3, true, 5) == 5);
    CHECK(ReportedHave(4, 0, false, 7) == 7);
    CHECK(ReportedHave(4, 0, false, 2) == 4);
    CHECK(ReportedHave(4, 9, true, 7) == 13);

    // Only a real online rise resets the zone-move clock.
    CHECK(IsNewItemRise(true, true, 6, 5));
    CHECK(!IsNewItemRise(true, true, 5, 5));
    CHECK(!IsNewItemRise(true, true, 4, 5));
    CHECK(!IsNewItemRise(true, false, 6, 0));
    CHECK(!IsNewItemRise(false, true, 6, 5));
    if (failures == 0)
        std::printf("test_guild_order_giveup_policy ok\n");
    return failures ? 1 : 0;
}
