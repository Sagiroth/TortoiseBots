#include "../ai/playerbot/QuestStallPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::FormatQuestStallAnchor;
using ai::ParseQuestStallAnchor;
using ai::QuestObjectiveStalled;
using ai::QuestStallAnchor;
using ai::kQuestStallAnchorTtlSec;
using ai::kQuestStallHorizonSec;
using ai::kQuestStallParkSec;

int main()
{
    std::cout << "Starting TortoiseBots quest-stall tests...\n";

    // (1) Donor numbers: 5-min horizon, 30-min park, 30-min anchor TTL.
    {
        CHECK(kQuestStallHorizonSec == 5 * 60);
        CHECK(kQuestStallParkSec == 30 * 60);
        CHECK(kQuestStallAnchorTtlSec == 30 * 60);
        std::cout << "  [PASS] donor constants (5-min verdict, 30-min park)\n";
    }

    // (2) No anchor yet: a first pick anchors, never stalls.
    {
        CHECK(!QuestObjectiveStalled(0, 0, 0, 0, 0, 1000));
        std::cout << "  [PASS] first pick never stalls\n";
    }

    // (3) Horizon not elapsed: same counters, 4:59 in - no stall.
    {
        CHECK(!QuestObjectiveStalled(2, 0, 2, 0, 1000, 1000 + 299));
        CHECK(QuestObjectiveStalled(2, 0, 2, 0, 1000, 1000 + 300));
        std::cout << "  [PASS] horizon boundary 299/300 s\n";
    }

    // (4) Any kill progress refreshes: anchored 0/0, now 1/0 past horizon.
    {
        CHECK(!QuestObjectiveStalled(1, 0, 0, 0, 1000, 1000 + 3600));
        std::cout << "  [PASS] kill progress defeats stall\n";
    }

    // (5) Item progress refreshes too: anchored 0/0, now 0/3 past horizon.
    {
        CHECK(!QuestObjectiveStalled(0, 3, 0, 0, 1000, 1000 + 3600));
        std::cout << "  [PASS] item progress defeats stall\n";
    }

    // (6) Flat zero counters past the horizon still stall (nothing to show
    // for five minutes of pursuit is the stall, not just slow progress).
    {
        CHECK(QuestObjectiveStalled(0, 0, 0, 0, 1000, 1000 + 3600));
        std::cout << "  [PASS] zero-progress past horizon stalls\n";
    }

    // (7) Anchor round-trip: format then parse restores every field; garbage
    // fails to parse (caller treats it as "no anchor", re-anchors).
    {
        QuestStallAnchor anchor;
        CHECK(ParseQuestStallAnchor(FormatQuestStallAnchor(12345, 2, 7, 3), anchor));
        CHECK(anchor.time == 12345);
        CHECK(anchor.objective == 2u);
        CHECK(anchor.kill == 7u);
        CHECK(anchor.item == 3u);
        CHECK(!ParseQuestStallAnchor("", anchor));
        CHECK(!ParseQuestStallAnchor("garbage", anchor));
        CHECK(!ParseQuestStallAnchor("0 1 2 3", anchor));
        std::cout << "  [PASS] anchor format/parse round-trip\n";
    }

    std::cout << "All quest-stall checks PASSED!\n";
    return 0;
}
