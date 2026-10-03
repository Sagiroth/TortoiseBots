#include "../ai/playerbot/QuestStallPolicy.h"

#include <array>
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
using ai::QuestObjectiveTrackable;
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

    // (2) No anchor yet: a first WORK tick anchors, never stalls.
    {
        std::array<std::uint32_t, 4> cur = {{0, 0, 0, 0}};
        CHECK(!QuestObjectiveStalled(cur, cur, cur, cur, 0, 1000));
        std::cout << "  [PASS] first WORK tick never stalls\n";
    }

    // (3) Horizon not elapsed: same counters, 4:59 in - no stall.
    {
        std::array<std::uint32_t, 4> cur = {{2, 0, 0, 0}};
        CHECK(!QuestObjectiveStalled(cur, cur, cur, cur, 1000, 1000 + 299));
        CHECK(QuestObjectiveStalled(cur, cur, cur, cur, 1000, 1000 + 300));
        std::cout << "  [PASS] horizon boundary 299/300 s\n";
    }

    // (4) Any kill progress refreshes: anchored 0, now 1 past horizon.
    {
        std::array<std::uint32_t, 4> anchor = {{0, 0, 0, 0}};
        std::array<std::uint32_t, 4> cur = {{1, 0, 0, 0}};
        CHECK(!QuestObjectiveStalled(cur, cur, anchor, anchor, 1000, 1000 + 3600));
        std::cout << "  [PASS] kill progress defeats stall\n";
    }

    // (5) Item progress refreshes too.
    {
        std::array<std::uint32_t, 4> anchor = {{0, 0, 0, 0}};
        std::array<std::uint32_t, 4> curKill = {{0, 0, 0, 0}};
        std::array<std::uint32_t, 4> curItem = {{0, 0, 3, 0}};
        std::array<std::uint32_t, 4> anchorItem = {{0, 0, 0, 0}};
        CHECK(!QuestObjectiveStalled(curKill, curItem, anchor, anchorItem, 1000, 1000 + 3600));
        std::cout << "  [PASS] item progress defeats stall\n";
    }

    // (6) Flat zero counters past the horizon still stall (nothing to show
    // for five arrived minutes is the stall, not just slow progress).
    {
        std::array<std::uint32_t, 4> zero = {{0, 0, 0, 0}};
        CHECK(QuestObjectiveStalled(zero, zero, zero, zero, 1000, 1000 + 3600));
        std::cout << "  [PASS] zero-progress past horizon stalls\n";
    }

    // (7) Alternating objectives share one per-quest anchor: progress on ANY
    // objective moves the verdict, and a cycle between two stalled
    // objectives still stalls (the old per-objective anchor reset forever).
    {
        std::array<std::uint32_t, 4> anchorKill = {{2, 0, 0, 0}};
        std::array<std::uint32_t, 4> anchorItem = {{0, 0, 0, 0}};
        std::array<std::uint32_t, 4> sameKill = {{2, 0, 0, 0}};
        std::array<std::uint32_t, 4> sameItem = {{0, 0, 0, 0}};
        CHECK(QuestObjectiveStalled(sameKill, sameItem, anchorKill, anchorItem, 1000, 1000 + 3600));
        std::array<std::uint32_t, 4> progressed = {{2, 1, 0, 0}};
        CHECK(!QuestObjectiveStalled(progressed, sameItem, anchorKill, anchorItem, 1000, 1000 + 3600));
        std::cout << "  [PASS] per-quest anchor across alternating objectives\n";
    }

    // (8) Non-counter objectives are exempt: explore/event/spell quests carry
    // no kill or item requirement, so there is nothing to count.
    {
        CHECK(!QuestObjectiveTrackable(0, 0, 0, 0));
        CHECK(!QuestObjectiveTrackable(1234, 0, 0, 0));
        CHECK(!QuestObjectiveTrackable(0, 0, 5678, 0));
        CHECK(QuestObjectiveTrackable(1234, 5, 0, 0));
        CHECK(QuestObjectiveTrackable(0, 0, 5678, 3));
        CHECK(QuestObjectiveTrackable(-9876, 2, 0, 0));
        std::cout << "  [PASS] exploration/event/spell objectives exempt\n";
    }

    // (9) Anchor round-trip: format then parse restores every field; garbage
    // fails to parse (caller treats it as "no anchor", re-anchors).
    {
        std::array<std::uint32_t, 4> kill = {{7, 0, 2, 0}};
        std::array<std::uint32_t, 4> item = {{0, 3, 0, 1}};
        QuestStallAnchor anchor;
        CHECK(ParseQuestStallAnchor(FormatQuestStallAnchor(12345, kill, item), anchor));
        CHECK(anchor.time == 12345);
        CHECK((anchor.kill == kill));
        CHECK((anchor.item == item));
        CHECK(!ParseQuestStallAnchor("", anchor));
        CHECK(!ParseQuestStallAnchor("garbage", anchor));
        CHECK(!ParseQuestStallAnchor("0 1 2 3", anchor));
        std::cout << "  [PASS] anchor format/parse round-trip\n";
    }

    std::cout << "All quest-stall checks PASSED!\n";
    return 0;
}
