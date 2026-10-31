#include "BotCommandHandlers.h"
#include "CommandShared.h"

#include "../runtime/BotManager.h"
#include "../runtime/ClaimLifecycle.h"
#include "../runtime/GuildOrderAccess.h"
#include "../runtime/GuildOrderCategoryPolicy.h"
#include "../runtime/GuildOrderGiveUpPolicy.h"
#include "../runtime/GuildOrderStore.h"
#include "../runtime/RandomBotAccountRegistry.h"
#include "../ai/playerbot/GuildOrderSources.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "Chat.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Guild/GuildMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace TortoiseBots {
namespace BotCommands {
namespace {

using GuildOrders::OrderReason;
using GuildOrders::OrderRecord;
using GuildOrders::OrderState;
using GuildOrders::OrderType;

inline constexpr uint32_t kMaxAmount = 200;
inline constexpr uint32_t kMinAmount = 1;
inline constexpr uint32_t kStormwindZone = 1519;
inline constexpr uint32_t kOrgrimmarZone = 1637;

void OrderError(ChatHandler* handler, char const* code, std::string const& message)
{
    SendActionError(handler, "order", code, message.c_str());
}

// The giver is a real player (not a bot) in the bot's guild whose rank may
// edit officer notes.
bool CheckGiver(ChatHandler* handler, Player* requester, uint32_t guildId)
{
    if (!requester || !requester->GetSession() || !requester->GetSession()->HasNetworkTransport())
    {
        OrderError(handler, "forbidden", "Only a real player can give orders.");
        return false;
    }
    if (!requester->GetGuildId() || requester->GetGuildId() != guildId)
    {
        OrderError(handler, "not-guild", "The bot is not in your guild.");
        return false;
    }
    Guild* guild = sGuildMgr.GetGuildById(guildId);
    if (!guild || !guild->HasRankRight(requester->GetRank(), GR_RIGHT_EOFFNOTE))
    {
        OrderError(handler, "forbidden", "Your guild rank may not give orders.");
        return false;
    }
    return true;
}

// Any bot in the giver's guild, online or not: an order on an offline bot is
// kept and runs when the bot is online again.
bool ResolveOrderBot(std::string const& name, Player*& bot, ObjectGuid& guid)
{
    bot = nullptr;
    guid.Clear();
    std::string fixed = name;
    if (fixed.empty() || !normalizePlayerName(fixed))
        return false;
    bot = sObjectMgr.GetPlayer(fixed.c_str());
    if (!bot)
        bot = sObjectAccessor.FindPlayerByName(fixed.c_str());
    if (bot)
    {
        guid = bot->getObjectGuid();
        return true;
    }
    guid = sObjectMgr.GetPlayerGuidByName(fixed);
    return !guid.IsEmpty();
}

// Online: the live module record decides. Offline: the character is a bot
// when it is claimed into a guild or lives on a random-pool account.
bool IsOrderBot(Player* bot, ObjectGuid guid)
{
    if (bot)
        return BotManager::Instance().IsBot(guid);
    if (ClaimLifecycle::Instance().IsClaimed(guid.GetCounter()))
        return true;
    uint32_t accountId = sObjectMgr.GetPlayerAccountIdByGUID(guid);
    return accountId && RandomBotAccountRegistry::Instance().IsRegistered(accountId);
}

uint32_t GuildOf(Player* bot, ObjectGuid guid)
{
    if (bot)
        return bot->GetGuildId();
    return Player::GetGuildIdFromDB(guid);
}

bool ParseUintToken(std::string const& token, uint32_t& value)
{
    value = 0;
    // At most 10 digits: longer input cannot fit uint32 and must fail, not throw.
    if (token.empty() || token.size() > 10 || !std::all_of(token.begin(), token.end(), ::isdigit))
        return false;
    unsigned long parsed = std::stoul(token);
    if (parsed > 0xFFFFFFFFul)
        return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

bool ParseItemToken(std::string const& token, uint32_t& itemId)
{
    itemId = 0;
    size_t link = token.find("Hitem:");
    if (link != std::string::npos)
    {
        size_t begin = link + 6;
        size_t end = begin;
        while (end < token.size() && std::isdigit(static_cast<unsigned char>(token[end])))
            ++end;
        return ParseUintToken(token.substr(begin, end - begin), itemId) && itemId != 0;
    }
    return ParseUintToken(token, itemId) && itemId != 0;
}

bool ParseAmount(std::string const& token, uint32_t& amount)
{
    amount = 0;
    if (!ParseUintToken(token, amount))
        return false;
    return amount >= kMinAmount && amount <= kMaxAmount;
}

bool ParseHours(std::string const& token, uint32_t& hours)
{
    hours = 0;
    std::string lower = token;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "inf")
        return true;
    if (!ParseUintToken(token, hours))
        return false;
    return hours >= GuildOrders::kMinOrderHours && hours <= GuildOrders::kMaxOrderHours;
}

bool HandleOrderGive(ChatHandler* handler, Player* requester, std::string const& rest)
{
    // order <bot> <farm|buy|craft> <itemId or item link> <amount> [hours|inf] [keep] [gold=<copper>]
    std::istringstream in(rest);
    std::string botName, typeToken;
    if (!(in >> botName >> typeToken))
    {
        OrderError(handler, "invalid", "Usage: order <bot> <farm|buy|craft> <item> <amount> [hours|inf] [keep] [gold=<copper>].");
        return true;
    }
    OrderType type;
    std::string lowerType = typeToken;
    std::transform(lowerType.begin(), lowerType.end(), lowerType.begin(), ::tolower);
    if (!GuildOrders::ParseType(lowerType, type))
    {
        OrderError(handler, "invalid", "Unknown order type: use farm, buy or craft.");
        return true;
    }
    typeToken = lowerType;
    // The item link carries spaces; the amount is the last token, with an
    // optional trailing whole hours 1-24 or `inf` and a trailing `keep`
    // word: the bot works that long in total per round and, with keep,
    // restocks the bank.
    std::string itemPart, token, amountToken;
    while (in >> token)
    {
        if (!itemPart.empty())
            itemPart += " ";
        itemPart += token;
    }
    bool hasGoldCap = false;
    uint32_t goldCap = 0;
    size_t goldSpace = itemPart.rfind(' ');
    std::string maybeGold = goldSpace == std::string::npos ? itemPart : itemPart.substr(goldSpace + 1);
    std::string lowerGold = maybeGold;
    std::transform(lowerGold.begin(), lowerGold.end(), lowerGold.begin(), ::tolower);
    if (lowerGold.rfind("gold=", 0) == 0)
    {
        if (!ParseUintToken(maybeGold.substr(5), goldCap))
        {
            OrderError(handler, "invalid", "Guild gold cap must be copper from 0 to 4294967295.");
            return true;
        }
        hasGoldCap = true;
        itemPart = goldSpace == std::string::npos ? std::string() : itemPart.substr(0, goldSpace);
    }
    // A trailing `keep` word marks a keep-in-stock order; hours still sit
    // between the amount and keep.
    bool keep = false;
    {
        size_t keepSpace = itemPart.rfind(' ');
        if (keepSpace != std::string::npos)
        {
            std::string maybeKeep = itemPart.substr(keepSpace + 1);
            std::transform(maybeKeep.begin(), maybeKeep.end(), maybeKeep.begin(), ::tolower);
            if (maybeKeep == "keep")
            {
                keep = true;
                itemPart = itemPart.substr(0, keepSpace);
            }
        }
    }
    uint32_t hours = GuildOrders::kDefaultOrderHours;
    size_t lastSpace = itemPart.rfind(' ');
    if (lastSpace == std::string::npos)
    {
        OrderError(handler, "invalid", "Usage: order <bot> <farm|buy|craft> <item> <amount> [hours|inf] [keep] [gold=<copper>].");
        return true;
    }
    // A trailing 1-24 or `inf` is hours only when what is left still reads
    // as an item plus an amount; otherwise it is the amount itself.
    std::string maybeHours = itemPart.substr(lastSpace + 1);
    uint32_t parsedHours = 0;
    bool hoursGiven = false;
    if (ParseHours(maybeHours, parsedHours))
    {
        std::string rest = itemPart.substr(0, lastSpace);
        size_t amountSpace = rest.rfind(' ');
        if (amountSpace != std::string::npos)
        {
            uint32_t restAmount = 0;
            uint32_t restItem = 0;
            if (ParseAmount(rest.substr(amountSpace + 1), restAmount) &&
                ParseItemToken(rest.substr(0, amountSpace), restItem))
            {
                hours = parsedHours;
                hoursGiven = true;
                itemPart = rest;
                lastSpace = amountSpace;
            }
        }
    }
    // A trailing number outside 1-24 after an item plus an amount is an
    // hours attempt, not an amount: say so instead of misreading it.
    if (!hoursGiven)
    {
        uint32_t parsedUint = 0;
        std::string rest = itemPart.substr(0, lastSpace);
        size_t amountSpace = rest.rfind(' ');
        if (ParseUintToken(maybeHours, parsedUint) && amountSpace != std::string::npos)
        {
            uint32_t restAmount = 0;
            uint32_t restItem = 0;
            if (ParseAmount(rest.substr(amountSpace + 1), restAmount) &&
                ParseItemToken(rest.substr(0, amountSpace), restItem))
            {
                OrderError(handler, "invalid", "Hours must be 1-24 or inf.");
                return true;
            }
        }
    }
    amountToken = itemPart.substr(lastSpace + 1);
    itemPart = itemPart.substr(0, lastSpace);
    uint32_t itemId = 0;
    if (!ParseItemToken(itemPart, itemId))
    {
        OrderError(handler, "invalid", "Unknown item: shift-click an item link or pass its id.");
        return true;
    }
    uint32_t amount = 0;
    if (!ParseAmount(amountToken, amount))
    {
        OrderError(handler, "invalid", "Amount must be 1-200.");
        return true;
    }

    Player* bot = nullptr;
    ObjectGuid guid;
    if (!ResolveOrderBot(botName, bot, guid))
    {
        OrderError(handler, "no-bot", "No such bot.");
        return true;
    }
    if (!IsOrderBot(bot, guid))
    {
        OrderError(handler, "no-bot", "That character is not a bot.");
        return true;
    }
    uint32_t guildId = GuildOf(bot, guid);
    if (!guildId)
    {
        OrderError(handler, "not-guild", "The bot is not in a guild.");
        return true;
    }
    if (!requester || requester->GetGuildId() != guildId)
    {
        OrderError(handler, "not-guild", "The bot is not in your guild.");
        return true;
    }
    if (!CheckGiver(handler, requester, guildId))
        return true;

    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
    if (!proto)
    {
        OrderError(handler, "invalid", "Unknown item.");
        return true;
    }
    if (!GuildOrders::IsOrderableItem(proto))
    {
        OrderError(handler, "bound", "That item cannot go to the guild bank.");
        return true;
    }

    OrderRecord record;
    record.botGuidLow = guid.GetCounter();
    record.guildId = guildId;
    record.giverGuidLow = requester->GetGUIDLow();
    record.rootGiverGuidLow = requester->GetGUIDLow();
    record.type = type;
    record.itemId = itemId;
    record.amount = amount;
    record.hours = hours;
    record.keep = keep;
    record.stockTarget = keep ? amount : 0;
    record.goldCap = goldCap;
    record.hasGoldCap = hasGoldCap;

    if (type == OrderType::Craft && !bot)
    {
        // No live spellbook offline: a craft order stored now could never be
        // recipe-checked, and pre-owned stock would flip it to delivering.
        OrderError(handler, "norecipe", "The bot is offline: its recipes cannot be checked.");
        return true;
    }
    if (bot)
    {
        GuildOrders::SourcePlan plan = ai::PlanGuildOrderSources(bot, itemId, type);
        if (type == OrderType::Craft && plan.verdict != OrderReason::None)
        {
            if (plan.verdict == OrderReason::NoRecipe)
            {
                // Refuse at give, like search does: storing Cant would still
                // ACK the giver and start the reporting round-trip.
                OrderError(handler, "norecipe", "The bot does not know that recipe.");
                return true;
            }
        }
        else if (!plan.zones.empty())
        {
            record.farmZoneId = plan.zones[0].zoneId;
            record.farmChance = plan.zones[0].chance;
        }
        if (type == OrderType::Buy && record.farmZoneId == 0)
            record.farmZoneId = bot->GetTeam() == ALLIANCE ? kStormwindZone : kOrgrimmarZone;
    }

    if (!GuildOrders::Give(record))
    {
        OrderError(handler, "invalid", "The order could not be stored.");
        return true;
    }

    // The next world-thread Update reports the new row to the giver.

    SendActionAck(handler, "order", "bot:" + (bot ? bot->GetName() : botName), 1, typeToken);
    return true;
}
bool HandleOrderReassign(ChatHandler* handler, Player* requester, std::string const& subBotName,
    std::string const& newBotName)
{
    Player* subBot = nullptr;
    ObjectGuid subGuid;
    if (!ResolveOrderBot(subBotName, subBot, subGuid) || !IsOrderBot(subBot, subGuid))
    {
        OrderError(handler, "no-bot", "No such sub-order bot.");
        return true;
    }
    Player* newBot = nullptr;
    ObjectGuid newGuid;
    if (!ResolveOrderBot(newBotName, newBot, newGuid) || !IsOrderBot(newBot, newGuid))
    {
        OrderError(handler, "no-bot", "No such bot.");
        return true;
    }
    if (!CheckGiver(handler, requester, GuildOf(subBot, subGuid)))
        return true;
    std::optional<OrderRecord> sub = GuildOrders::Find(subGuid.GetCounter());
    if (!sub || !sub->parentBotGuidLow)
    {
        OrderError(handler, "invalid", "That bot has no sub-order.");
        return true;
    }
    if (GuildOf(newBot, newGuid) != sub->guildId)
    {
        OrderError(handler, "not-guild", "The new bot is not in the order's guild.");
        return true;
    }
    std::optional<GuildOrders::OrderRecord> newBotOrder = GuildOrders::Find(newGuid.GetCounter());
    bool newBotBusy = newBotOrder && !newBotOrder->cancelled &&
        (!GuildOrders::IsFinished(newBotOrder->state) || newBotOrder->state == GuildOrders::OrderState::Stocked);
    if (newBotBusy)
    {
        OrderError(handler, "invalid", "The new bot already has an order.");
        return true;
    }
    if (newBotOrder && newBotOrder->cancelled)
        GuildOrders::ClearParkedStub(newGuid.GetCounter());
    Player* live = sObjectAccessor.FindPlayer(newGuid);
    if (!live || !live->IsInWorld() || live->GetGuildId() != sub->guildId)
    {
        OrderError(handler, "no-bot", "The new bot is offline or not in the order's guild.");
        return true;
    }
    if (!GuildOrders::IsDurableOrderBot(newGuid.GetCounter()))
    {
        OrderError(handler, "no-bot", "Only guild bots take sub-orders: never alts or hires.");
        return true;
    }
    bool canMake = false, canFarm = false;
    if (!GuildOrders::SubOrderCandidateUsable(live, sub->itemId, canMake, canFarm))
    {
        OrderError(handler, "noskill", "The new bot can neither make nor farm that item.");
        return true;
    }
    if (!GuildOrders::ReassignSubOrder(subGuid.GetCounter(), newGuid.GetCounter(),
        canMake ? GuildOrders::OrderType::Craft : GuildOrders::OrderType::Farm))
    {
        OrderError(handler, "invalid", "The sub-order could not be moved.");
        return true;
    }
    SendActionAck(handler, "order", "bot:" + (subBot ? subBot->GetName() : subBotName), 1, "reassign");
    return true;
}

bool HandleOrderCancel(ChatHandler* handler, Player* requester, std::string const& botName)
{
    Player* bot = nullptr;
    ObjectGuid guid;
    if (!ResolveOrderBot(botName, bot, guid))
    {
        OrderError(handler, "no-bot", "No such bot.");
        return true;
    }
    if (!IsOrderBot(bot, guid))
    {
        OrderError(handler, "no-bot", "That character is not a bot.");
        return true;
    }
    // Permission before existence: an unprivileged requester gets
    // forbidden/not-guild even when there is no order.
    if (!CheckGiver(handler, requester, GuildOf(bot, guid)))
        return true;
    if (!GuildOrders::Find(guid.GetCounter()))
    {
        OrderError(handler, "invalid", "That bot has no order.");
        return true;
    }
    GuildOrders::Cancel(guid.GetCounter());
    SendActionAck(handler, "order", "bot:" + (bot ? bot->GetName() : botName), 1, "cancel");
    return true;
}

bool HandleOrderAccess(ChatHandler* handler, Player* requester)
{
    if (!requester || !requester->GetGuildId())
    {
        OrderError(handler, "not-guild", "You are not in a guild.");
        return true;
    }
    handler->PSendSysMessage("%s",
        GuildOrders::FormatAccessLine(requester->GetGuildId(), requester->GetGUIDLow()).c_str());
    return true;
}

bool HandleOrderRank(ChatHandler* handler, Player* requester, std::string const& rankToken)
{
    if (!requester || !requester->GetSession() || !requester->GetSession()->HasNetworkTransport())
    {
        OrderError(handler, "forbidden", "Only a real player can change the bots' rank.");
        return true;
    }
    uint32_t guildId = requester->GetGuildId();
    if (!guildId)
    {
        OrderError(handler, "not-guild", "You are not in a guild.");
        return true;
    }
    Guild* guild = sGuildMgr.GetGuildById(guildId);
    if (!guild)
    {
        OrderError(handler, "not-guild", "You are not in a guild.");
        return true;
    }
    if (!guild->HasRankRight(requester->GetRank(), GR_RIGHT_PROMOTE) ||
        !guild->HasRankRight(requester->GetRank(), GR_RIGHT_DEMOTE))
    {
        OrderError(handler, "forbidden", "Your guild rank may not change ranks.");
        return true;
    }
    uint32_t wanted = 0;
    if (!ParseUintToken(rankToken, wanted))
    {
        OrderError(handler, "invalid", "Usage: order rank <rankIndex>.");
        return true;
    }
    uint32_t rankCount = guild->GetRanksSize();
    if (rankCount < 2 || wanted < 1 || wanted >= rankCount)
    {
        OrderError(handler, "invalid", "Rank must be between 1 and the lowest rank.");
        return true;
    }
    if (!GuildOrders::StoreBotRank(guildId, wanted))
    {
        OrderError(handler, "invalid", "The rank could not be stored.");
        return true;
    }
    GuildOrders::ApplyBotRankToGuild(guildId);
    SendActionAck(handler, "order", "rank:" + std::to_string(wanted), 1, "rank");
    handler->PSendSysMessage("%s",
        GuildOrders::FormatAccessLine(guildId, requester->GetGUIDLow()).c_str());
    return true;
}

bool HandleOrderList(ChatHandler* handler, Player* requester)
{
    if (!requester || !requester->GetGuildId())
    {
        OrderError(handler, "not-guild", "You are not in a guild.");
        return true;
    }
    std::vector<OrderRecord> orders = GuildOrders::ListForGuild(requester->GetGuildId());
    handler->PSendSysMessage("TBM:ORDER_BEGIN|%u", static_cast<uint32_t>(orders.size()));
    handler->PSendSysMessage("%s", GuildOrders::FormatAccessLine(requester->GetGuildId(), requester->GetGUIDLow()).c_str());
    for (OrderRecord const& order : orders)
    {
        Player* bot = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, order.botGuidLow));
        bool online = bot && bot->IsInWorld();
        uint32_t bags = online ? bot->GetItemCount(order.itemId) : 0;
        handler->PSendSysMessage("%s",
            GuildOrders::FormatOrderLine(requester->GetGuildId(), order,
                GuildOrders::ReportedHave(order.delivered, bags, online, order.reportedHave)).c_str());
    }
    handler->PSendSysMessage("TBM:ORDER_END");
    return true;
}

} // namespace

bool HandleOrder(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    if (!requester)
    {
        OrderError(handler, "forbidden", "You must be in-game.");
        return true;
    }

    // Guild orders need the travel tables: without them the source index
    // is empty and no farm plan exists.
    if (!ai::GuildOrderSourcesReady())
    {
        OrderError(handler, "disabled", "Guild orders need travel data (AiPlayerbot.AutoDoQuests = 1).");
        return true;
    }

    std::string rest = Trim(args ? args : "");
    if (rest.empty())
    {
        OrderError(handler, "invalid", "Usage: order <bot> <farm|buy|craft|cancel> ... | order list | order search ....");
        return true;
    }

    std::istringstream in(rest);
    std::string first;
    in >> first;

    std::string lower = first;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "list")
        return HandleOrderList(handler, requester);
    if (lower == "access")
        return HandleOrderAccess(handler, requester);
    if (lower == "rank")
    {
        std::string rankToken;
        if (!(in >> rankToken))
        {
            OrderError(handler, "invalid", "Usage: order rank <rankIndex>.");
            return true;
        }
        return HandleOrderRank(handler, requester, rankToken);
    }

    if (lower == "search")
    {
        // order search <bot> <farm|buy|craft> [cat=<kind>] <text>
        std::string botName, typeToken;
        if (!(in >> botName >> typeToken))
        {
            OrderError(handler, "invalid", "Usage: order search <bot> <farm|buy|craft> [cat=<kind>] <text>.");
            return true;
        }
        std::string text;
        std::getline(in, text);
        text = Trim(text);
        // The search command answers the usage errors; this only skips the
        // bot lookups for text that is certainly too short. A cat= word
        // anywhere may still leave browsable text behind.
        GuildOrders::OrderItemCategory category = GuildOrders::OrderItemCategory::Other;
        std::string query = text;
        GuildOrders::CategoryArgParse const catParse = GuildOrders::StripCategoryArg(query, category);
        if (text.size() < 3 && catParse == GuildOrders::CategoryArgParse::None)
        {
            OrderError(handler, "invalid", "Search text needs at least 3 characters.");
            return true;
        }
        Player* bot = nullptr;
        ObjectGuid guid;
        if (!ResolveOrderBot(botName, bot, guid))
        {
            OrderError(handler, "no-bot", "No such bot.");
            return true;
        }
        if (!bot)
        {
            OrderError(handler, "no-bot", "Search needs the bot online.");
            return true;
        }
        if (!IsOrderBot(bot, guid))
        {
            OrderError(handler, "no-bot", "That character is not a bot.");
            return true;
        }
        uint32_t guildId = GuildOf(bot, guid);
        if (!guildId || !requester->GetGuildId() || requester->GetGuildId() != guildId)
        {
            OrderError(handler, "not-guild", "The bot is not in your guild.");
            return true;
        }
        if (!CheckGiver(handler, requester, guildId))
            return true;
        return HandleOrderSearch(handler, requester, bot, typeToken, text);
    }

    // order reassign <subOrderBot> <newBot> | order <bot> cancel | order <bot> <type> <item> <amount> [hours] [keep] [gold=<copper>]
    if (lower == "reassign")
    {
        std::string subBotName, newBotName;
        if (!(in >> subBotName >> newBotName))
        {
            OrderError(handler, "invalid", "Usage: order reassign <subOrderBot> <newBot>.");
            return true;
        }
        return HandleOrderReassign(handler, requester, subBotName, newBotName);
    }
    // order <bot> cancel | order <bot> <type> <item> <amount> [hours] [keep] [gold=<copper>]
    std::string second;
    if (!(in >> second))
    {
        OrderError(handler, "invalid", "Usage: order <bot> <farm|buy|craft|cancel> ....");
        return true;
    }
    std::string lowerSecond = second;
    std::transform(lowerSecond.begin(), lowerSecond.end(), lowerSecond.begin(), ::tolower);
    if (lowerSecond == "cancel")
        return HandleOrderCancel(handler, requester, first);


    return HandleOrderGive(handler, requester, rest);
}

} // namespace BotCommands
} // namespace TortoiseBots
