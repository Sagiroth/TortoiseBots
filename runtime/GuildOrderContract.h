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

enum class OrderState : uint8_t { Working, Waiting, Delivering, Done, Mailed, NoSkill, Cant };

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
};

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
    }
    return "working";
}

inline bool ParseState(std::string const& token, OrderState& out)
{
    for (uint8_t i = 0; i <= uint8_t(OrderState::Cant); ++i)
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
    }
    return "-";
}

inline bool ParseReason(std::string const& token, OrderReason& out)
{
    for (uint8_t i = 0; i <= uint8_t(OrderReason::Timeout); ++i)
        if (token == ReasonToken(OrderReason(i))) { out = OrderReason(i); return true; }
    return false;
}

// The state a reason belongs to. Working has no reason.
inline OrderState StateForReason(OrderReason reason)
{
    switch (reason)
    {
        case OrderReason::Grouped:  return OrderState::Waiting;
        case OrderReason::NoSkill:
        case OrderReason::LowSkill: return OrderState::NoSkill;
        case OrderReason::LowLevel:
        case OrderReason::NoRange:
        case OrderReason::NoSource:
        case OrderReason::NoRecipe:
        case OrderReason::Elite:
        case OrderReason::Stuck:
        case OrderReason::Timeout:  return OrderState::Cant;
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
    return state == OrderState::Done || state == OrderState::Mailed || state == OrderState::NoSkill ||
           state == OrderState::Cant;
}

// One order, as the store keeps it in memory (one per bot).
struct OrderRecord
{
    uint32_t botGuidLow = 0;
    uint32_t guildId = 0;
    uint32_t giverGuidLow = 0;
    OrderType type = OrderType::Farm;
    uint32_t itemId = 0;
    uint32_t amount = 0;
    uint32_t hours = 1;          // player-chosen farm time, whole hours 1-24
    uint32_t delivered = 0;
    OrderState state = OrderState::Working;
    OrderReason reason = OrderReason::None;
    uint32_t farmZoneId = 0;   // 0 = not planned yet
    uint8_t farmChance = 0;    // percent at the best source in farmZoneId
    uint32_t zoneIndex = 0;    // position in the current farm plan
    bool farmRest = false;     // buy order whose AH attempt is over: farm what is missing
    uint32_t deathsInZone = 0; // deaths in the current farm zone
    uint32_t workedSinceNewItemSec = 0;
    uint32_t workedTotalSec = 0; // worked time over the whole order, ends it at hours
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
};

inline std::string SourcesToken(uint8_t flags)
{
    static char const* const names[] = {"drop", "skin", "herb", "ore", "craft", "ah"};
    std::string out;
    for (uint8_t i = 0; i < 6; ++i)
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
