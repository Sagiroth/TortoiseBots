// Standalone test for runtime/GuildOrderGiveUpPolicy.h (guild order give-up).
// No core, no DB: feeds ClassifyGiveUp plain counters.
#include "../runtime/GuildOrderGiveUpPolicy.h"

#include <cstdio>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    CHECK(kGiveUpWorkedSeconds == 3600);
    CHECK(kGiveUpDeaths == 5);

    // Below both thresholds: keep working.
    CHECK(ClassifyGiveUp(0, 0, false) == GiveUpAction::KeepWorking);
    CHECK(ClassifyGiveUp(3599, 4, false) == GiveUpAction::KeepWorking);
    CHECK(ClassifyGiveUp(3599, 4, true) == GiveUpAction::KeepWorking);

    // First hit moves zone once, by time or by deaths.
    CHECK(ClassifyGiveUp(3600, 0, false) == GiveUpAction::MoveZone);
    CHECK(ClassifyGiveUp(0, 5, false) == GiveUpAction::MoveZone);
    CHECK(ClassifyGiveUp(3600, 5, false) == GiveUpAction::MoveZone);

    // Second hit ends the order.
    CHECK(ClassifyGiveUp(3600, 0, true) == GiveUpAction::GiveUp);
    CHECK(ClassifyGiveUp(0, 5, true) == GiveUpAction::GiveUp);

    // Reported progress never drops.
    CHECK(ReportedHave(0, 5, true, 0) == 5);
    CHECK(ReportedHave(0, 3, true, 5) == 5);
    CHECK(ReportedHave(4, 0, false, 7) == 7);
    CHECK(ReportedHave(4, 0, false, 2) == 4);
    CHECK(ReportedHave(4, 9, true, 7) == 13);

    // Only a real online rise resets the give-up clock.
    CHECK(IsNewItemRise(true, true, 6, 5));
    CHECK(!IsNewItemRise(true, true, 5, 5));
    CHECK(!IsNewItemRise(true, true, 4, 5));
    CHECK(!IsNewItemRise(true, false, 6, 0));
    CHECK(!IsNewItemRise(false, true, 6, 5));
    if (failures == 0)
        std::printf("test_guild_order_giveup_policy ok\n");
    return failures ? 1 : 0;
}
