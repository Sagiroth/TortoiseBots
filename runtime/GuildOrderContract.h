#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace GuildOrders
{

// Guild orders given with `.bot order`. The tokens below are the addon
// contract: TBM parses them, so a rename changes the addon as well.

enum class OrderType : uint8_t { Farm, Buy, Craft };

enum class OrderState : uint8_t { Working, Waiting, Delivering, Done, Mailed, NoSkill, Cant, Stocked };

enum class SupplyStep : uint8_t { None, Bags, Bank, AuctionHouse, Farm, Craft, Sub, Need, Deliver };

enum class OrderReason : uint8_t
{
    None,
    Grouped,
    NoSkill,
    LowSkill,
    LowLevel,
    NoRange,
    NoSource,
    NoRecipe,
    Stuck,
    BagsFull,
    NoBank,
    Elite,
    Timeout,
    NoProgress,
    NeedItems,
    BankDenied,
};

struct NeedItem
{
    uint32_t itemId = 0;
    uint32_t count = 0;
};

inline char const* SupplyStepToken(SupplyStep step)
{
    switch (step)
    {
        case SupplyStep::None:        return "-";
        case SupplyStep::Bags:        return "bags";
        case SupplyStep::Bank:        return "bank";
        case SupplyStep::AuctionHouse:return "ah";
        case SupplyStep::Farm:        return "farm";
        case SupplyStep::Craft:       return "craft";
        case SupplyStep::Sub:         return "sub";
        case SupplyStep::Need:        return "need";
        case SupplyStep::Deliver:     return "deliver";
    }
    return "-";
}

inline bool ParseSupplyStep(std::string const& token, SupplyStep& out)
{
    for (uint8_t i = 0; i <= uint8_t(SupplyStep::Deliver); ++i)
        if (token == SupplyStepToken(SupplyStep(i))) { out = SupplyStep(i); return true; }
    return false;
}

inline std::string NeedItemsToken(std::vector<NeedItem> const& items)
{
    std::string out;
    for (NeedItem const& item : items)
    {
        if (!item.itemId || !item.count)
            continue;
        if (!out.empty())
            out += ';';
        out += std::to_string(item.itemId) + ':' + std::to_string(item.count);
    }
    return out.empty() ? "-" : out;
}

inline char const* TypeToken(OrderType type)
{
    switch (type)
    {
        case OrderType::Farm:  return "farm";
        case OrderType::Buy:   return "buy";
        case OrderType::Craft: return "craft";
    }
    return "farm";
}

inline bool ParseType(std::string const& token, OrderType& out)
{
    if (token == "farm")  { out = OrderType::Farm;  return true; }
    if (token == "buy")   { out = OrderType::Buy;   return true; }
    if (token == "craft") { out = OrderType::Craft; return true; }
    return false;
}

inline char const* StateToken(OrderState state)
{
    switch (state)
    {
        case OrderState::Working:    return "working";
        case OrderState::Waiting:    return "waiting";
        case OrderState::Delivering: return "delivering";
        case OrderState::Done:       return "done";
        case OrderState::Mailed:     return "mailed";
        case OrderState::NoSkill:    return "noskill";
        case OrderState::Cant:       return "cant";
        case OrderState::Stocked:    return "stocked";
    }
    return "working";
}

inline bool ParseState(std::string const& token, OrderState& out)
{
    for (uint8_t i = 0; i <= uint8_t(OrderState::Stocked); ++i)
        if (token == StateToken(OrderState(i))) { out = OrderState(i); return true; }
    return false;
}

inline char const* ReasonToken(OrderReason reason)
{
    switch (reason)
    {
        case OrderReason::None:     return "-";
        case OrderReason::Grouped:  return "grouped";
        case OrderReason::NoSkill:  return "noskill";
        case OrderReason::LowSkill: return "lowskill";
        case OrderReason::LowLevel: return "lowlevel";
        case OrderReason::NoRange:  return "norange";
        case OrderReason::NoSource: return "nosource";
        case OrderReason::NoRecipe: return "norecipe";
        case OrderReason::Stuck:    return "stuck";
        case OrderReason::BagsFull: return "bagsfull";
        case OrderReason::NoBank:   return "nobank";
        case OrderReason::Elite:    return "elite";
        case OrderReason::Timeout:  return "timeout";
        case OrderReason::NoProgress: return "noprogress";
        case OrderReason::NeedItems: return "needitems";
        case OrderReason::BankDenied: return "bankdenied";
    }
    return "-";
}

inline bool ParseReason(std::string const& token, OrderReason& out)
{
    for (uint8_t i = 0; i <= uint8_t(OrderReason::BankDenied); ++i)
        if (token == ReasonToken(OrderReason(i))) { out = OrderReason(i); return true; }
    return false;
}

// The state a reason belongs to. Working has no reason.
inline OrderState StateForReason(OrderReason reason)
{
    switch (reason)
    {
        case OrderReason::Grouped:  return OrderState::Waiting;
        case OrderReason::NeedItems:return OrderState::Waiting;
        case OrderReason::BankDenied:return OrderState::Working;
        case OrderReason::NoSkill:
        case OrderReason::LowSkill: return OrderState::NoSkill;
        case OrderReason::LowLevel:
        case OrderReason::NoRange:
        case OrderReason::NoSource:
        case OrderReason::NoRecipe:
        case OrderReason::Elite:
        case OrderReason::Stuck:
        case OrderReason::Timeout:
        case OrderReason::NoProgress: return OrderState::Cant;
        case OrderReason::BagsFull: return OrderState::Delivering;
        case OrderReason::NoBank:   return OrderState::Mailed;
        case OrderReason::None:     break;
    }
    return OrderState::Working;
}

// Difficulty estimate for a farm plan, from the expected kills or gathers.
// Search rows estimate one item; order rows estimate the full amount. Buy
// orders carry none.
enum class OrderDifficulty : uint8_t { None, Easy, Medium, Hard };

inline char const* DifficultyToken(OrderDifficulty difficulty)
{
    switch (difficulty)
    {
        case OrderDifficulty::None:   return "-";
        case OrderDifficulty::Easy:   return "easy";
        case OrderDifficulty::Medium: return "medium";
        case OrderDifficulty::Hard:   return "hard";
    }
    return "-";
}

inline bool IsFinished(OrderState state)
{
    // Stocked counts as finished: nothing runs (no travel, no timers, no
    // pool hold), the store just watches the bank for the next round.
    return state == OrderState::Done || state == OrderState::Mailed || state == OrderState::NoSkill ||
           state == OrderState::Cant || state == OrderState::Stocked;
}

// Trailing TBM:ORDER field: whether the order was given with `keep`.
inline char const* KeepToken(bool keep)
{
    return keep ? "keep" : "-";
}

// The hours slot of TBM:ORDER: the chosen whole hours, or `inf` for no time
// limit (stored as 0 in memory and in the database).
inline std::string HoursToken(uint32_t hours)
{
    return hours == 0 ? "inf" : std::to_string(hours);
}

// One order, as the store keeps it in memory (one per bot).
struct OrderRecord
{
    uint32_t botGuidLow = 0;
    uint32_t guildId = 0;
    uint32_t giverGuidLow = 0;
    uint32_t parentBotGuidLow = 0; // order needing this item: 0 = top level
    uint32_t rootGiverGuidLow = 0; // player who gave the root order (live rows go here)
    OrderType type = OrderType::Farm;
    uint32_t itemId = 0;
    uint32_t amount = 0;       // current round quota; re-armed per round for keep orders
    uint32_t hours = 0;        // player-chosen farm time per round: whole hours 1-24, 0 = no time limit (the default)
    bool keep = false;         // keep the stock target filled: new rounds while the bank holds less
    uint32_t goldCap = 0;      // maximum guild copper this order may withdraw
    bool hasGoldCap = false;
    uint32_t guildGoldWithdrawn = 0;
    uint32_t guildGoldHeld = 0;
    bool cancelled = false; // cancel kept the row: unspent guild gold is still owed to the bank
    std::vector<NeedItem> supplyGains; // order-caused inflow per item (bank, AH, mail, vendor, farm)
    uint32_t bankEmptyItemId = 0; // item the bank had none of (skip until the cooldown)
    uint32_t bankEmptyMs = 0; // when the empty visit happened, in server uptime ms
    SupplyStep supplyStep = SupplyStep::None;
    SupplyStep supplyReturnStep = SupplyStep::None;
    uint32_t supplyItemId = 0;
    uint32_t supplyCount = 0;
    uint32_t supplyGoldCount = 0;
    std::vector<NeedItem> needItems;
    bool needWhispered = false;
    std::string lastNeedWhisper;
    bool needBankDenied = false;
    uint32_t stockTarget = 0;  // keep level in the bank (the ordered amount)
    uint32_t lastBankCheckMs = 0; // last keep bank look, in server uptime ms
    uint32_t delivered = 0;
    OrderState state = OrderState::Working;
    OrderReason reason = OrderReason::None;
    uint32_t farmZoneId = 0;   // 0 = not planned yet
    uint8_t farmChance = 0;    // percent at the best source in farmZoneId
    uint32_t zoneIndex = 0;    // position in the current farm plan
    uint32_t farmItemId = 0;   // item actually farmed: order item, or the reagent of the current craft leg (0 = order item)
    bool farmRest = false;     // buy order whose AH attempt is over: farm what is missing
    uint32_t deathsInZone = 0; // deaths in the current farm zone
    uint32_t searchFailsInZone = 0; // consecutive rejected/empty order-leg searches in farmZoneId
    uint32_t workedSinceNewItemSec = 0;
    uint32_t workedTotalSec = 0; // worked time over the whole order, ends it at hours
    uint32_t workedNoGainSec = 0; // worked time since the bot last gained an order item (the progress watchdog)
    uint32_t reportedHave = 0;   // last reported progress; reported have never drops below this
    uint64_t createdAt = 0;
    uint64_t updatedAt = 0;
};

// Source kinds, as listed in TBM:ORDER_ITEM <sources>.
enum SourceFlag : uint8_t
{
    SourceDrop  = 1 << 0,
    SourceSkin  = 1 << 1,
    SourceHerb  = 1 << 2,
    SourceOre   = 1 << 3,
    SourceCraft = 1 << 4,
    SourceAh    = 1 << 5,
    SourceFish  = 1 << 6,
};

inline std::string SourcesToken(uint8_t flags)
{
    static char const* const names[] = {"drop", "skin", "herb", "ore", "craft", "ah", "fish"};
    std::string out;
    for (uint8_t i = 0; i < 7; ++i)
    {
        if (!(flags & (1 << i)))
            continue;
        if (!out.empty())
            out += ',';
        out += names[i];
    }
    return out.empty() ? "-" : out;
}

struct FarmZone
{
    uint32_t zoneId = 0;
    uint8_t chance = 0;   // percent at the best source in this zone
    double score = 0.0;   // spawns x chance
    bool onBotMap = false; // has spawns on the map the bot is on
};

// What one bot can do about one item: the answer of the source finder, used by
// the search reply, the give command and the travel code alike.
struct SourcePlan
{
    uint8_t sources = 0;             // SourceFlag bits usable by this bot
    uint32_t minLevel = 0;           // lowest usable source level, 0 = none
    uint32_t skillId = 0;            // 0 = no skill needed
    uint32_t skillValue = 0;
    OrderReason verdict = OrderReason::None;  // None = ok
    std::vector<FarmZone> zones;     // best first; empty for buy and craft
    float bestChance = 0.0f;         // unfloored percent at zones[0]; 0 when no zones
};

} // namespace GuildOrders
} // namespace TortoiseBots
