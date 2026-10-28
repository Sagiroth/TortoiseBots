#include "BotCommandHandlers.h"
#include "CommandShared.h"

#include "../runtime/BotManager.h"
#include "../runtime/GuildOrderStore.h"
#include "../ai/playerbot/ChatHelper.h"
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
#include <set>
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

uint32_t GuildOf(Player* bot, ObjectGuid guid)
{
    if (bot)
        return bot->GetGuildId();
    return Player::GetGuildIdFromDB(guid);
}

bool IsOrderableItem(ItemPrototype const* proto)
{
    return proto && proto->Bonding != BIND_WHEN_PICKED_UP && proto->Bonding != BIND_QUEST_ITEM &&
           proto->Class != ITEM_CLASS_QUEST && proto->Duration == 0;
}

bool ParseItemToken(std::string const& token, uint32_t& itemId)
{
    itemId = 0;
    if (token.find("Hitem:") != std::string::npos)
    {
        std::set<uint32_t> ids = ai::ChatHelper::ExtractAllItemIds(token);
        if (!ids.empty())
            itemId = *ids.begin();
        return itemId != 0;
    }
    if (!token.empty() && std::all_of(token.begin(), token.end(), ::isdigit))
        itemId = static_cast<uint32_t>(std::stoul(token));
    return itemId != 0;
}

bool ParseAmount(std::string const& token, uint32_t& amount)
{
    amount = 0;
    if (token.empty() || !std::all_of(token.begin(), token.end(), ::isdigit))
        return false;
    amount = static_cast<uint32_t>(std::stoul(token));
    return amount >= kMinAmount && amount <= kMaxAmount;
}

bool HandleOrderGive(ChatHandler* handler, Player* requester, std::string const& rest)
{
    // order <bot> <farm|buy|craft> <itemId or item link> <amount>
    std::istringstream in(rest);
    std::string botName, typeToken;
    if (!(in >> botName >> typeToken))
    {
        OrderError(handler, "invalid", "Usage: order <bot> <farm|buy|craft> <item> <amount>.");
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
    // The item link carries spaces; the amount is the last token.
    std::string itemPart, token, amountToken;
    while (in >> token)
    {
        if (!itemPart.empty())
            itemPart += " ";
        itemPart += token;
    }
    size_t lastSpace = itemPart.rfind(' ');
    if (lastSpace == std::string::npos)
    {
        OrderError(handler, "invalid", "Usage: order <bot> <farm|buy|craft> <item> <amount>.");
        return true;
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
    if (bot && !BotManager::Instance().IsBot(guid))
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
    if (!IsOrderableItem(proto))
    {
        OrderError(handler, "bound", "That item cannot go to the guild bank.");
        return true;
    }

    OrderRecord record;
    record.botGuidLow = guid.GetCounter();
    record.guildId = guildId;
    record.giverGuidLow = requester->GetGUIDLow();
    record.type = type;
    record.itemId = itemId;
    record.amount = amount;

    if (bot)
    {
        GuildOrders::SourcePlan plan = ai::PlanGuildOrderSources(bot, itemId, type);
        if (plan.verdict != OrderReason::None)
        {
            record.state = GuildOrders::StateForReason(plan.verdict);
            record.reason = plan.verdict;
        }
        else if (!plan.zones.empty())
        {
            record.farmZoneId = plan.zones[0].zoneId;
            record.farmChance = plan.zones[0].chance;
        }
    }

    if (!GuildOrders::Give(record))
    {
        OrderError(handler, "invalid", "The order could not be stored.");
        return true;
    }

    std::optional<OrderRecord> stored = GuildOrders::Find(record.botGuidLow);
    if (stored)
        GuildOrders::SendLiveUpdate(*stored, GuildOrders::Progress(bot, *stored));

    SendActionAck(handler, "order", "bot:" + (bot ? bot->GetName() : botName), 1, typeToken);
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
    std::optional<OrderRecord> existing = GuildOrders::Find(guid.GetCounter());
    if (!existing)
    {
        OrderError(handler, "invalid", "That bot has no order.");
        return true;
    }
    if (!CheckGiver(handler, requester, existing->guildId))
        return true;
    GuildOrders::Cancel(guid.GetCounter());
    SendActionAck(handler, "order", "bot:" + (bot ? bot->GetName() : botName), 1, "cancel");
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
    for (OrderRecord const& order : orders)
        handler->PSendSysMessage("%s", GuildOrders::FormatOrderLine(requester->GetGuildId(), order).c_str());
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

    if (lower == "search")
    {
        // order search <bot> <farm|buy|craft> <text>
        std::string botName, typeToken;
        if (!(in >> botName >> typeToken))
        {
            OrderError(handler, "invalid", "Usage: order search <bot> <farm|buy|craft> <text>.");
            return true;
        }
        std::string text;
        std::getline(in, text);
        text = Trim(text);
        if (text.size() < 3)
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
        uint32_t guildId = GuildOf(bot, guid);
        if (!guildId || !requester->GetGuildId() || requester->GetGuildId() != guildId)
        {
            OrderError(handler, "not-guild", "The bot is not in your guild.");
            return true;
        }
        if (!CheckGiver(handler, requester, guildId))
            return true;
        if (!bot)
        {
            OrderError(handler, "no-bot", "Search needs the bot online.");
            return true;
        }
        return HandleOrderSearch(handler, requester, bot, typeToken, text);
    }

    // order <bot> cancel | order <bot> <type> <item> <amount>
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
