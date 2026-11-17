// Standalone check for ai/playerbot/policy/loot/LootHistoryPolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_loot_history_policy.cpp -o /tmp/test_loot_history_policy
//   /tmp/test_loot_history_policy
#include "../ai/playerbot/policy/loot/LootHistoryPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::LootHistoryPolicy::DecodeLootCursor;
using ai::LootHistoryPolicy::EncodeLootCursor;
using ai::LootHistoryPolicy::FitLootRollWireLine;
using ai::LootHistoryPolicy::FitLootWireLine;
using ai::LootHistoryPolicy::kLootHistoryMaxRows;
using ai::LootHistoryPolicy::kLootHistoryMaxWireBytes;
using ai::LootHistoryPolicy::kLootHistoryWindowSeconds;
using ai::LootHistoryPolicy::LootHistoryCursor;
using ai::LootHistoryPolicy::LootHistoryRow;
using ai::LootHistoryPolicy::ParseLootHistoryRow;
using ai::LootHistoryPolicy::ParseLootTimestamp;
using ai::LootHistoryPolicy::WithinLootWindow;

int main()
{
    CHECK(kLootHistoryWindowSeconds == 24 * 3600);
    CHECK(kLootHistoryMaxRows == 40);
    CHECK(kLootHistoryMaxWireBytes == 250);
    LootHistoryRow row;
    CHECK(ParseLootHistoryRow(
        "2026-10-27 10:00:00+00,Dudette,StoreLootAction,\"POINT(1 2)\",1,2,60.000000,"
        "\"Tough Jerky\"", row) == false);

    std::string item = "2026-10-27 10:00:00+00,Dudette,StoreLootAction,\"POINT(1 2)\",1,2,60.000000,"
        "\"Tough Jerky\",\"122\",\"corpse\",\"Kobold Vermin\",\"1\"";
    CHECK(ParseLootHistoryRow(item, row));
    CHECK(row.timestamp == "2026-10-27 10:00:00+00");
    CHECK(row.bot == "Dudette");
    CHECK(row.event == "StoreLootAction");
    CHECK(row.info1 == "Tough Jerky");
    CHECK(row.info2 == "122");
    CHECK(row.info3 == "corpse");
    CHECK(row.info4 == "Kobold Vermin");
    CHECK(row.info5 == "1");

    std::string money = "2026-10-27 10:00:01+00,Dudette,LootMoney,\"POINT(1 2)\",1,2,60.000000,"
        "\"137\",\"1234\",\"corpse\",\"Kobold Vermin\",\"\"";
    CHECK(ParseLootHistoryRow(money, row));
    CHECK(row.event == "LootMoney");
    CHECK(row.info1 == "137");
    CHECK(row.info5.empty());

    std::string roll = "2026-10-27 10:00:02+00,Dudette,LootRoll,\"POINT(1 2)\",1,2,60.000000,"
        "\"Gamemaster's Robe\",\"need:2586:for equiping as upgrade.\"";
    CHECK(ParseLootHistoryRow(roll, row));
    CHECK(row.event == "LootRoll");
    CHECK(row.info1 == "Gamemaster's Robe");
    CHECK(row.info2 == "need:2586:for equiping as upgrade.");
    CHECK(row.info3.empty() && row.info4.empty() && row.info5.empty());

    std::string oldRow = "2026-10-20 10:00:00+00,Dudette,StoreLootAction,\"POINT(1 2)\",1,2,60.000000,"
        "\"Tough Jerky\",\"122\"";
    CHECK(ParseLootHistoryRow(oldRow, row));
    CHECK(row.info3.empty() && row.info4.empty() && row.info5.empty());

    CHECK(!ParseLootHistoryRow(
        "2026-10-27 10:00:00+00,Dudette,GatherLoot,\"POINT(1 2)\",1,2,60.000000,\"136\",\"122\"", row));
    CHECK(!ParseLootHistoryRow("short,line", row));
    CHECK(!ParseLootHistoryRow("", row));

    std::string quoted = "2026-10-27 10:00:00+00,Dudette,StoreLootAction,\"POINT(1 2)\",1,2,60.000000,"
        "\"Sword, Blessed\",\"122\",\"corpse\",\"Smith, Jon\",\"2\"";
    CHECK(ParseLootHistoryRow(quoted, row));
    CHECK(row.info1 == "Sword, Blessed");
    CHECK(row.info4 == "Smith, Jon");

    time_t now = ParseLootTimestamp("2026-10-27 10:00:00+00");
    CHECK(now != 0);
    CHECK(ParseLootTimestamp("not a stamp") == 0);
    CHECK(ParseLootTimestamp("2026-13-27 10:00:00+00") == 0);
    CHECK(WithinLootWindow("2026-10-27 09:00:00+00", now));
    CHECK(WithinLootWindow("2026-10-26 10:00:01+00", now));
    CHECK(!WithinLootWindow("2026-10-26 09:59:59+00", now));
    CHECK(!WithinLootWindow("not a stamp", now));

    LootHistoryCursor cursor;
    CHECK(DecodeLootCursor("", cursor) && cursor.piece.empty() && cursor.offset == 0);
    CHECK(DecodeLootCursor("bot_events_2026-10-27_09-00-00.csv.gz@123", cursor));
    CHECK(cursor.piece == "bot_events_2026-10-27_09-00-00.csv.gz");
    CHECK(cursor.offset == 123);
    CHECK(EncodeLootCursor(cursor) == "bot_events_2026-10-27_09-00-00.csv.gz@123");
    CHECK(!DecodeLootCursor("no-offset", cursor));
    CHECK(!DecodeLootCursor("@12", cursor));
    CHECK(!DecodeLootCursor("piece@", cursor));
    CHECK(!DecodeLootCursor("piece@12x", cursor));

    std::string itemName(200, 'i');
    std::string sourceName(200, 's');
    FitLootWireLine(itemName, sourceName, 60);
    CHECK(60 + itemName.size() + sourceName.size() <= kLootHistoryMaxWireBytes);
    CHECK(!itemName.empty() && !sourceName.empty());
    itemName = "short";
    sourceName = "names";
    FitLootWireLine(itemName, sourceName, 60);
    CHECK(itemName == "short" && sourceName == "names");

    std::string rollInfo2 = "greed:2586:" + std::string(220, 'r');
    std::string rollItem = "Gamemaster's Robe";
    FitLootRollWireLine(rollInfo2, rollItem, 60);
    CHECK(60 + rollInfo2.size() + rollItem.size() <= kLootHistoryMaxWireBytes);
    CHECK(rollInfo2.rfind("greed:2586:", 0) == 0);
    CHECK(rollItem == "Gamemaster's Robe");

    std::cout << "test_loot_history_policy: OK" << std::endl;
    return 0;
}
