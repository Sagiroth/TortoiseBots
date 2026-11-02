// Standalone test for runtime/GuildOrderStoreColumns.h: the guild order load
// schema. A column listed twice shifts every field after it onto its
// neighbour's value (farm_rest reads carried, keep reads farm_rest, guild
// gold, state and timestamps follow), and the next save writes that shifted
// row back.
//
// No characters database is available here, so the two things the load
// depends on are pinned against each other instead: the column order the
// statement carries, and the index constant each record field is read
// through. Both are written out independently in this file.
#include "../runtime/GuildOrderContract.h"
#include "../runtime/GuildOrderStoreColumns.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

using namespace TortoiseBots::GuildOrders;
using namespace TortoiseBots::GuildOrders::StoreColumns;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

// The SELECT column order the table rows are written in, spelled out here so
// the header's list is checked against a second copy rather than itself.
static char const* const kExpectedOrder[] = {
    "bot_guid",
    "guild_id",
    "giver_guid",
    "parent_bot",
    "root_giver",
    "type",
    "item_id",
    "amount",
    "hours",
    "delivered",
    "state",
    "reason",
    "farm_zone",
    "farm_chance",
    "farm_item",
    "zone_index",
    "deaths_in_zone",
    "worked_sec",
    "worked_total",
    "worked_nogain",
    "reported",
    "farm_rest",
    "keep",
    "gold_cap",
    "gold_cap_set",
    "guild_gold_withdrawn",
    "guild_gold_held",
    "cancelled",
    "supply_step",
    "supply_return_step",
    "supply_item",
    "supply_count",
    "supply_gold_count",
    "need_items",
    "need_whispered",
    "last_need_whisper",
    "need_bank_denied",
    "stock_target",
    "last_bank_check",
    "created_at",
    "updated_at",
    "carried",
};

static constexpr size_t kExpectedCount = sizeof(kExpectedOrder) / sizeof(kExpectedOrder[0]);

// The load's field mapping: which index constant reads which column.
struct FieldColumn
{
    size_t index;
    char const* column;
};

static FieldColumn const kFields[] = {
    { ColBotGuid, "bot_guid" },
    { ColGuildId, "guild_id" },
    { ColGiverGuid, "giver_guid" },
    { ColParentBot, "parent_bot" },
    { ColRootGiver, "root_giver" },
    { ColType, "type" },
    { ColItemId, "item_id" },
    { ColAmount, "amount" },
    { ColHours, "hours" },
    { ColDelivered, "delivered" },
    { ColState, "state" },
    { ColReason, "reason" },
    { ColFarmZone, "farm_zone" },
    { ColFarmChance, "farm_chance" },
    { ColFarmItem, "farm_item" },
    { ColZoneIndex, "zone_index" },
    { ColDeathsInZone, "deaths_in_zone" },
    { ColWorkedSec, "worked_sec" },
    { ColWorkedTotal, "worked_total" },
    { ColWorkedNoGain, "worked_nogain" },
    { ColReported, "reported" },
    { ColFarmRest, "farm_rest" },
    { ColKeep, "keep" },
    { ColGoldCap, "gold_cap" },
    { ColGoldCapSet, "gold_cap_set" },
    { ColGuildGoldWithdrawn, "guild_gold_withdrawn" },
    { ColGuildGoldHeld, "guild_gold_held" },
    { ColCancelled, "cancelled" },
    { ColSupplyStep, "supply_step" },
    { ColSupplyReturnStep, "supply_return_step" },
    { ColSupplyItem, "supply_item" },
    { ColSupplyCount, "supply_count" },
    { ColSupplyGoldCount, "supply_gold_count" },
    { ColNeedItems, "need_items" },
    { ColNeedWhispered, "need_whispered" },
    { ColLastNeedWhisper, "last_need_whisper" },
    { ColNeedBankDenied, "need_bank_denied" },
    { ColStockTarget, "stock_target" },
    { ColLastBankCheck, "last_bank_check" },
    { ColCreatedAt, "created_at" },
    { ColUpdatedAt, "updated_at" },
    { ColCarried, "carried" },
};

static constexpr size_t kFieldCount = sizeof(kFields) / sizeof(kFields[0]);

int main()
{
    CHECK(kCount == kExpectedCount);
    CHECK(kFieldCount == kExpectedCount);

    std::set<std::string> seen;
    bool used[kCount] = {};
    for (size_t i = 0; i < kCount && i < kExpectedCount; ++i)
    {
        CHECK(std::strcmp(kSelect[i], kExpectedOrder[i]) == 0);
        CHECK(seen.insert(kSelect[i]).second);
    }

    for (FieldColumn const& field : kFields)
    {
        CHECK(field.index < kCount);
        if (field.index >= kCount)
            continue;
        // The constant points at its own column, at the position this file
        // expects it.
        CHECK(std::strcmp(kSelect[field.index], field.column) == 0);
        CHECK(field.index < kExpectedCount && std::strcmp(kExpectedOrder[field.index], field.column) == 0);
        CHECK(!used[field.index]);
        used[field.index] = true;
    }
    for (size_t i = 0; i < kCount; ++i)
        CHECK(used[i]);

    // The carry column is read once, last, so the columns added before it
    // keep the indices they always had.
    CHECK(std::strcmp(kSelect[kCount - 1], "carried") == 0);
    CHECK(ColCarried == kCount - 1);
    CHECK(std::strcmp(kSelect[ColReported], "reported") == 0);
    CHECK(std::strcmp(kSelect[ColFarmRest], "farm_rest") == 0);
    CHECK(std::strcmp(kSelect[ColGuildGoldWithdrawn], "guild_gold_withdrawn") == 0);
    CHECK(std::strcmp(kSelect[ColGuildGoldHeld], "guild_gold_held") == 0);
    CHECK(std::strcmp(kSelect[ColCancelled], "cancelled") == 0);

    // The statement the load sends is exactly this list.
    std::string expected;
    for (size_t i = 0; i < kExpectedCount; ++i)
    {
        if (i)
            expected += ", ";
        expected += '`';
        expected += kExpectedOrder[i];
        expected += '`';
    }
    CHECK(SelectList() == expected);

    // Nonzero sentinel per column: each field must read the sentinel of the
    // column it names, at the position this file expects. Gold, state and
    // timestamp fields are in the same list as everything else.
    std::string values[kCount];
    for (size_t i = 0; i < kExpectedCount && i < kCount; ++i)
        values[i] = std::string("v") + kExpectedOrder[i];
    for (FieldColumn const& field : kFields)
        CHECK(values[field.index] == std::string("v") + field.column);

    // Token columns decode through the same parsers the load calls.
    values[ColState] = StateToken(OrderState::Cant);
    values[ColReason] = ReasonToken(OrderReason::Timeout);
    values[ColSupplyStep] = SupplyStepToken(SupplyStep::Bank);
    values[ColSupplyReturnStep] = SupplyStepToken(SupplyStep::Deliver);
    OrderState state = OrderState::Working;
    OrderReason reason = OrderReason::None;
    SupplyStep step = SupplyStep::None;
    SupplyStep returnStep = SupplyStep::None;
    CHECK(ParseState(values[ColState], state) && state == OrderState::Cant);
    CHECK(ParseReason(values[ColReason], reason) && reason == OrderReason::Timeout);
    CHECK(ParseSupplyStep(values[ColSupplyStep], step) && step == SupplyStep::Bank);
    CHECK(ParseSupplyStep(values[ColSupplyReturnStep], returnStep) && returnStep == SupplyStep::Deliver);

    // Numeric columns are read as decimal text, nonzero sentinels.
    values[ColReported] = "1234";
    values[ColCarried] = "4321";
    values[ColGuildGoldWithdrawn] = "500";
    values[ColGuildGoldHeld] = "250";
    values[ColGoldCap] = "9000";
    CHECK(std::strtoul(values[ColReported].c_str(), nullptr, 10) == 1234);
    CHECK(std::strtoul(values[ColCarried].c_str(), nullptr, 10) == 4321);
    CHECK(std::strtoul(values[ColGuildGoldWithdrawn].c_str(), nullptr, 10) == 500);
    CHECK(std::strtoul(values[ColGuildGoldHeld].c_str(), nullptr, 10) == 250);
    CHECK(std::strtoul(values[ColGoldCap].c_str(), nullptr, 10) == 9000);

    if (failures == 0)
        std::printf("test_guild_order_columns ok\n");
    return failures ? 1 : 0;
}
