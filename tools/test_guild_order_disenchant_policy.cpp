// Standalone test for runtime/GuildOrderDisenchantPolicy.h: the
// disenchant decisions by mail and by trade (subject match, skill thresholds,
// batching, drop-off window and room check).
// Build and run:
//   g++ -std=c++17 -Wall -Wextra -pedantic tools/test_guild_order_disenchant_policy.cpp -o /tmp/test_guild_order_disenchant_policy
//   /tmp/test_guild_order_disenchant_policy
#include "../runtime/GuildOrderDisenchantPolicy.h"

#include <cstdio>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    // Subject match: case-insensitive, trimmed; anything else stays intake.
    CHECK(IsDisenchantServiceSubject("disenchant"));
    CHECK(IsDisenchantServiceSubject(" Disenchant "));
    CHECK(IsDisenchantServiceSubject("DISENCHANT"));
    CHECK(!IsDisenchantServiceSubject("disenchant now"));
    CHECK(!IsDisenchantServiceSubject(""));
    CHECK(!IsDisenchantServiceSubject("Item(s) you asked for"));

    // Skill floor: 5 * ceil(req / 5) - 75 on the shared row.
    CHECK(DisenchantRequiredSkill(10, 10, false) == 1);
    CHECK(DisenchantRequiredSkill(25, 20, false) == 25);
    CHECK(DisenchantRequiredSkill(40, 35, false) == 100);
    CHECK(DisenchantRequiredSkill(55, 50, false) == 175);
    CHECK(DisenchantRequiredSkill(60, 60, false) == 225);
    // High item level with a low requirement uses the item level.
    CHECK(DisenchantRequiredSkill(60, 1, false) == 200);
    // Epics: 61+ need 225, 90+ need 300, lower ones use the shared row.
    CHECK(DisenchantRequiredSkill(60, 60, true) == 225);
    CHECK(DisenchantRequiredSkill(70, 60, true) == 225);
    CHECK(DisenchantRequiredSkill(90, 60, true) == 300);
    CHECK(DisenchantRequiredSkill(40, 35, true) == 100);

    // Verdicts: non-disenchantable first, then skill, then the floor.
    CHECK(ClassifyDisenchantItem(true, false, 40, 35, 300, false) == DisenchantVerdict::CannotDisenchant);
    CHECK(ClassifyDisenchantItem(false, true, 40, 35, 0, false) == DisenchantVerdict::NoSkill);
    CHECK(ClassifyDisenchantItem(true, true, 40, 35, 99, false) == DisenchantVerdict::LowSkill);
    CHECK(ClassifyDisenchantItem(true, true, 40, 35, 100, false) == DisenchantVerdict::Ok);
    CHECK(ClassifyDisenchantItem(false, false, 40, 35, 0, false) == DisenchantVerdict::CannotDisenchant);

    // One item per mail.
    CHECK(DisenchantResultBatches(0) == 0);
    CHECK(DisenchantResultBatches(1) == 1);
    CHECK(DisenchantResultBatches(3) == 3);

    // The cast only runs when the bot can finish one.
    CHECK(DisenchantCastReady(false, false, false, false, false, true));
    CHECK(!DisenchantCastReady(true, false, false, false, false, true));
    CHECK(!DisenchantCastReady(false, true, false, false, false, true));
    CHECK(!DisenchantCastReady(false, false, true, false, false, true));
    CHECK(!DisenchantCastReady(false, false, false, true, false, true));
    CHECK(!DisenchantCastReady(false, false, false, false, true, true));
    CHECK(!DisenchantCastReady(false, false, false, false, false, false));

    // A mailed item needs its own slot plus one for its result, and each
    // item already waiting keeps one result slot free.
    CHECK(DisenchantIntakeHasRoom(2, 0));
    CHECK(!DisenchantIntakeHasRoom(1, 0));
    CHECK(DisenchantIntakeHasRoom(5, 3));
    CHECK(!DisenchantIntakeHasRoom(4, 3));
    // Drop-off whisper: same match as the mail subject; trade talk around it
    // must not open a window.
    CHECK(IsDisenchantDropOffWhisper("disenchant"));
    CHECK(IsDisenchantDropOffWhisper(" Disenchant "));
    CHECK(IsDisenchantDropOffWhisper("DISENCHANT"));
    CHECK(!IsDisenchantDropOffWhisper("disenchant now"));
    CHECK(!IsDisenchantDropOffWhisper("please disenchant this"));
    CHECK(!IsDisenchantDropOffWhisper(""));
    // Window: right member and a fresh whisper; wrong member, empty, stale.
    CHECK(DisenchantDropOffOpen({11, 1000}, 11, 1000 + 60 * 1000));
    CHECK(!DisenchantDropOffOpen({11, 1000}, 12, 1000 + 60 * 1000));
    CHECK(!DisenchantDropOffOpen({0, 1000}, 11, 1000 + 60 * 1000));
    CHECK(!DisenchantDropOffOpen({11, 1000}, 11, 1000 + kDisenchantDropOffWindowMs));
    CHECK(!DisenchantDropOffOpen({11, 1000}, 11, 1000 + kDisenchantDropOffWindowMs + 1));
    // Room: two slots per further item (its slot plus its result slot).
    CHECK(DisenchantDropOffRoom(2, 0) == 1);
    CHECK(DisenchantDropOffRoom(1, 0) == 0);
    CHECK(DisenchantDropOffRoom(5, 3) == 1);
    CHECK(DisenchantDropOffRoom(6, 3) == 1);
    CHECK(DisenchantDropOffRoom(7, 3) == 2);
    // Offer verdicts: gold or empty never records, too many takes nothing.
    CHECK(CheckDisenchantDropOff({1, true}, 10, 0) == DisenchantDropOffVerdict::HasGold);
    CHECK(CheckDisenchantDropOff({0, false}, 10, 0) == DisenchantDropOffVerdict::Empty);
    CHECK(CheckDisenchantDropOff({2, false}, 2, 0) == DisenchantDropOffVerdict::TooMany);
    CHECK(CheckDisenchantDropOff({1, false}, 2, 0) == DisenchantDropOffVerdict::Accept);
    CHECK(CheckDisenchantDropOff({2, false}, 7, 3) == DisenchantDropOffVerdict::Accept);
    CHECK(CheckDisenchantDropOff({3, false}, 7, 3) == DisenchantDropOffVerdict::TooMany);
    CHECK(DisenchantDropOffRoomText(1) == "I only have room for 1 item.");
    CHECK(DisenchantDropOffRoomText(0) == "I only have room for 0 items.");
    CHECK(DisenchantDropOffToken(DisenchantDropOffVerdict::Accept) == std::string("accept"));
    auto& queueA = DisenchantServiceQueue(1);
    auto& queueB = DisenchantServiceQueue(2);
    QueueDisenchantServiceItem(queueA, 0, 7, 9);
    CHECK(queueA.empty());
    QueueDisenchantServiceItem(queueA, 11, 7, 9);
    QueueDisenchantServiceItem(queueA, 11, 7, 9);
    CHECK(queueA.size() == 1);
    CHECK(queueB.empty());
    CHECK(queueA.front().senderLow == 11);
    CHECK(queueA.front().itemId == 9);
    CHECK(DisenchantServiceMailSubject("Copper Axe") == "Disenchanted: Copper Axe");
    CHECK(DisenchantServiceMailSubject("") == "Disenchanted");
    // Queued items and landed results shield from disposal, read from the rows.
    struct Row { uint32_t itemId; std::string results; };
    std::vector<Row> rows = { { 9, "" }, { 15, "1234:2" } };
    CHECK(DisenchantRowsHoldItem(rows, 9));
    CHECK(DisenchantRowsHoldItem(rows, 1234));
    CHECK(!DisenchantRowsHoldItem(rows, 10));
    CHECK(!DisenchantRowsHoldItem(rows, 0));
    queueA.front().results.push_back({1234, 2});
    CHECK(DisenchantResultBatches(2) == 2);
    CHECK(FormatDisenchantResults(queueA.front().results) == "1234:2");
    CHECK(ParseDisenchantResults("1234:2;12:3").size() == 2);
    CHECK(ParseDisenchantResults("bogus;1234:2").size() == 1);
    CHECK(ParseDisenchantResults("").empty());
    queueA.clear();

    if (failures == 0)
        std::printf("test_guild_order_disenchant_policy ok\n");
    return failures ? 1 : 0;
}
