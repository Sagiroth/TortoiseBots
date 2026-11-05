#include "BotCommandHandlers.h"
#include "CommandShared.h"

#include "../runtime/BotManager.h"
#include "../runtime/GuildOrderBringPolicy.h"
#include "../runtime/GuildOrderReagentTree.h"
#include "../ai/playerbot/GuildOrderCraft.h"
#include "../ai/playerbot/GuildOrderEnchants.h"
#include "../ai/playerbot/ServerFacade.h"
#include "../runtime/ClaimLifecycle.h"
#include "../runtime/GuildOrderAccess.h"
#include "../runtime/GuildOrderCategoryPolicy.h"
#include "../runtime/GuildOrderGiveUpPolicy.h"
#include "../runtime/GuildOrderCraftPolicy.h"
#include "../runtime/GuildOrderPlayerBuyPolicy.h"
#include "../runtime/GuildOrderStore.h"
#include "../runtime/GuildOrderWhoSearchPolicy.h"
#include "../runtime/GuildOrderMeetPolicy.h"
#include "../runtime/EnchantCapital.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "playerbot/PlayerbotAIConfig.h"
#include "../runtime/RandomBotAccountRegistry.h"
#include "../ai/playerbot/GuildOrderSources.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "Group.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Group/Group.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Maps/Map.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Chat.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Guild/GuildMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Spells/SpellMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"

#include <algorithm>
#include <cctype>
#include <ctime>
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

// (BotBusyForMeet lives in runtime/EnchantCapital.h, shared with the
// lifecycle action.)

// True when the giver ranks as guild master or may remove guild members
// (an officer): replacing another giver's running enchant order.
bool GiverMayOverrideEnchant(Guild* guild, Player* requester)
{
    if (!guild || !requester)
        return false;
    if (guild->GetLeaderGuid() == requester->getObjectGuid())
        return true;
    return guild->HasRankRight(requester->GetRank(), GR_RIGHT_REMOVE);
}

bool HandleOrderMeet(ChatHandler* handler, Player* requester, std::string const& botName)
{
    // order <bot> meet: the giver records a Meet press; the lead comes once
    // ready. Every refusal names its code: noorder, notcapital, later (kept)
    // or gathering (kept).
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
    if (!CheckGiver(handler, requester, GuildOf(bot, guid)))
        return true;
    std::optional<OrderRecord> row = GuildOrders::Find(guid.GetCounter());
    bool liveEnchant = row && !row->cancelled && row->type == OrderType::Enchant &&
        !GuildOrders::IsFinished(row->state) &&
        row->giverGuidLow == requester->GetGUIDLow();
    if (!liveEnchant)
    {
        OrderError(handler, "noorder", "That bot has no enchant order from you.");
        return true;
    }
    bool giverInCapital = GuildOrders::GiverInBotCapital(requester->GetZoneId(), !bot || bot->GetTeam() == ALLIANCE);
    bool botTiedUp = TortoiseBots::EnchantCapital::BotBusyForMeet(bot);
    bool online = bot && bot->IsInWorld();
    bool gathering;
    if (!online)
        gathering = true;
    else if (row->state == OrderState::Working)
        gathering = true;
    else if (row->state == OrderState::Waiting)
        gathering = row->reason != OrderReason::NeedItems;
    else
        gathering = false;
    GuildOrders::MeetFacts facts;
    facts.liveEnchantOrder = liveEnchant;
    facts.giverInCapital = giverInCapital;
    facts.botTiedUp = botTiedUp;
    facts.gathering = gathering;
    switch (GuildOrders::DecideMeet(facts))
    {
        case GuildOrders::MeetVerdict::NoOrder:
            OrderError(handler, "noorder", "That bot has no enchant order from you.");
            return true;
        case GuildOrders::MeetVerdict::NotCapital:
        {
            std::string text = (bot && bot->GetTeam() != ALLIANCE) ?
                "Meet me in Orgrimmar." : "Meet me in Stormwind.";
            OrderError(handler, "notcapital", text.c_str());
            return true;
        }
        case GuildOrders::MeetVerdict::Later:
            GuildOrders::RequestMeet(guid.GetCounter(), requester->GetGUIDLow());
            handler->PSendSysMessage("I'll come when I'm out.");
            SendActionAck(handler, "order", "bot:" + (bot ? bot->GetName() : botName), 1, "meet later");
            return true;
        case GuildOrders::MeetVerdict::Gathering:
            GuildOrders::RequestMeet(guid.GetCounter(), requester->GetGUIDLow());
            handler->PSendSysMessage("Still gathering, I'll tell you.");
            SendActionAck(handler, "order", "bot:" + (bot ? bot->GetName() : botName), 1, "meet gathering");
            return true;
        case GuildOrders::MeetVerdict::Coming:
            GuildOrders::RequestMeet(guid.GetCounter(), requester->GetGUIDLow());
            handler->PSendSysMessage("On my way.");
            SendActionAck(handler, "order", "bot:" + (bot ? bot->GetName() : botName), 1, "meet coming");
            return true;
    }
    return true;
}

// `.bot order <bot> enchant <spellId or spell link> [hours|inf] [gold=<copper>] [bring=<item>,<item>]`:
// one cast of a permanent enchant the bot knows. The record holds the spell
// id (type tells it apart from items), amount 1, state Working; the
// gathering lifecycle owns everything after. Runs on the world thread.
bool HandleOrderEnchantGive(ChatHandler* handler, Player* requester, std::string const& botName,
    std::string const& rest)
{
    std::istringstream in(rest);
    std::string spellToken;
    if (!(in >> spellToken))
    {
        OrderError(handler, "invalid", "Usage: order <bot> enchant <spell link or id> [hours|inf] [gold=<copper>] [bring=<item>,<item>].");
        return true;
    }
    uint32_t spellId = 0;
    if (!GuildOrders::ParseEnchantSpellToken(spellToken, spellId))
    {
        OrderError(handler, "invalid", "Unknown enchant: shift-click a spell link or pass its id.");
        return true;
    }
    std::string token, hoursToken, goldToken, bringToken, keepToken;
    while (in >> token)
    {
        std::string lower = token;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower == "inf" || GuildOrders::IsEnchantHoursToken(lower))
        {
            if (!hoursToken.empty())
            {
                OrderError(handler, "invalid", "Usage: order <bot> enchant <spell link or id> [hours|inf] [gold=<copper>] [bring=<item>,<item>].");
                return true;
            }
            hoursToken = token;
            continue;
        }
        if (lower.rfind("gold=", 0) == 0)
        {
            if (!goldToken.empty())
            {
                OrderError(handler, "invalid", "Guild gold cap must be copper from 0 to 4294967295.");
                return true;
            }
            goldToken = token;
            continue;
        }
        if (lower.rfind("bring=", 0) == 0)
        {
            if (!bringToken.empty())
            {
                OrderError(handler, "invalid", "Bring list must be item ids joined by commas: bring=<item>,<item>.");
                return true;
            }
            bringToken = token;
            continue;
        }
        if (lower == "keep")
        {
            keepToken = token;
            continue;
        }
        OrderError(handler, "invalid", "Usage: order <bot> enchant <spell link or id> [hours|inf] [gold=<copper>] [bring=<item>,<item>].");
        return true;
    }
    uint32_t hours = GuildOrders::kDefaultOrderHours;
    if (!hoursToken.empty() && !GuildOrders::ParseEnchantHours(hoursToken, hours))
    {
        OrderError(handler, "invalid", "Hours must be 1-24 or inf.");
        return true;
    }
    bool hasGoldCap = false;
    uint32_t goldCap = 0;
    if (!goldToken.empty() && !GuildOrders::ParseEnchantGoldCap(goldToken.substr(5), goldCap))
    {
        OrderError(handler, "invalid", "Guild gold cap must be copper from 0 to 4294967295.");
        return true;
    }
    else if (!goldToken.empty())
        hasGoldCap = true;
    std::vector<uint32_t> bring;
    if (!bringToken.empty() && !GuildOrders::ParseBringList(bringToken.substr(6), bring))
    {
        OrderError(handler, "invalid", "Bring list must be item ids joined by commas: bring=<item>,<item>.");
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
    if (std::optional<OrderRecord> existing = GuildOrders::Find(guid.GetCounter()))
    {
        if (!existing->cancelled && existing->type == OrderType::Enchant &&
            !GuildOrders::IsFinished(existing->state))
        {
            Guild* guild = sGuildMgr.GetGuildById(guildId);
            bool sameGiver = existing->giverGuidLow == requester->GetGUIDLow();
            bool mayOverride = GiverMayOverrideEnchant(guild, requester);
            if (GuildOrders::DecideEnchantReplace(true, sameGiver, mayOverride) ==
                GuildOrders::EnchantReplaceVerdict::Busy)
            {
                std::string giverName = "?";
                sObjectMgr.GetPlayerNameByGUID(
                    ObjectGuid(HIGHGUID_PLAYER, existing->giverGuidLow), giverName);
                OrderError(handler, "busy", ((bot ? bot->GetName() : botName) +
                    std::string(" is busy with an enchant order for ") + giverName + ".").c_str());
                return true;
            }
        }
    }
    TortoiseBots::GuildOrders::EnchantEntry catalogue;
    for (TortoiseBots::GuildOrders::EnchantEntry const& candidate : ai::GuildOrderEnchants())
        if (candidate.spellId == spellId)
        {
            catalogue = candidate;
            break;
        }
    GuildOrders::EnchantGiveArgs args;
    args.inCatalogue = catalogue.spellId != 0;
    args.knownByBot = bot && bot->HasSpell(spellId);
    SpellEntry const* spell = sSpellMgr.GetSpellEntry(spellId);
    uint32_t reqSkill = catalogue.spellId ? catalogue.reqSkill : 0;
    if (spell && (!catalogue.spellId || !reqSkill))
    {
        uint32_t recipeItem = ai::GuildOrderRecipeItemFor(spellId);
        ItemPrototype const* recipeProto = recipeItem ? sObjectMgr.GetItemPrototype(recipeItem) : nullptr;
        reqSkill = GuildOrders::EnchantReqSkillCombine(false, 0, recipeProto != nullptr,
            recipeProto ? recipeProto->RequiredSkillRank : 0);
    }
    args.skillEnough = !bot || bot->GetSkillValue(333) >= reqSkill;
    args.keep = !keepToken.empty();
    if (GuildOrders::OrderReason verdict = GuildOrders::EnchantGiveVerdict(args);
        verdict != GuildOrders::OrderReason::None)
    {
        OrderError(handler, ReasonToken(verdict), GuildOrders::EnchantGiveRefusal(verdict));
        return true;
    }
    if (!bring.empty())
    {
        std::vector<uint32_t> baseIds;
        if (spell)
            for (uint8_t i = 0; i < MAX_SPELL_REAGENTS; ++i)
                if (spell->ReagentCount[i] && spell->Reagent[i] > 0)
                    baseIds.push_back(uint32_t(spell->Reagent[i]));
        bring = GuildOrders::FilterBringToBaseReagents(bring, baseIds.data(), baseIds.size());
    }
    OrderRecord record;
    record.botGuidLow = guid.GetCounter();
    record.guildId = guildId;
    record.giverGuidLow = requester->GetGUIDLow();
    record.rootGiverGuidLow = requester->GetGUIDLow();
    record.type = OrderType::Enchant;
    record.itemId = spellId;
    record.amount = 1;
    record.hours = hours;
    record.keep = false;
    record.stockTarget = 0;
    record.goldCap = goldCap;
    record.hasGoldCap = hasGoldCap;
    record.bringItems = bring;
    record.state = OrderState::Working;
    if (!GuildOrders::Give(record))
    {
        OrderError(handler, "invalid", "The order could not be stored.");
        return true;
    }
    SendActionAck(handler, "order", "bot:" + (bot ? bot->GetName() : botName), 1, "enchant");
    return true;
}

bool HandleOrderGive(ChatHandler* handler, Player* requester, std::string const& rest)
{
    // order <bot> <farm|buy|craft|enchant> <itemId or item link> <amount> [hours|inf] [keep] [gold=<copper>]
    std::istringstream in(rest);
    std::string botName, typeToken;
    if (!(in >> botName >> typeToken))
    {
        OrderError(handler, "invalid", "Usage: order <bot> <farm|buy|craft|enchant> <item> <amount> [hours|inf] [keep] [gold=<copper>].");
        return true;
    }
    OrderType type;
    std::string lowerType = typeToken;
    std::transform(lowerType.begin(), lowerType.end(), lowerType.begin(), ::tolower);
    if (!GuildOrders::ParseType(lowerType, type))
    {
        OrderError(handler, "invalid", "Unknown order type: use farm, buy, craft or enchant.");
        return true;
    }
    if (type == OrderType::Enchant)
    {
        OrderError(handler, "invalid", "Usage: order <bot> enchant <spell link or id> [hours|inf] [gold=<copper>] [bring=<item>,<item>].");
        return true;
    }
    typeToken = lowerType;
    if (GuildOrders::PlayerBuyOrderRefused(type, sPlayerbotAIConfig.guildOrderPlayerBuy))
    {
        OrderError(handler, GuildOrders::PlayerBuyRefusalCode(), GuildOrders::PlayerBuyRefusal());
        return true;
    }
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
    // Trailing options pop in either order: gold=<copper> caps the guild
    // gold the order may take, bring=<item>,<item> names base reagents the
    // giver delivers themselves (craft only).
    bool hasGoldCap = false;
    uint32_t goldCap = 0;
    std::vector<uint32_t> bring;
    for (int pass = 0; pass < 2; ++pass)
    {
        size_t optionSpace = itemPart.rfind(' ');
        std::string maybeOption = optionSpace == std::string::npos ? itemPart : itemPart.substr(optionSpace + 1);
        std::string lowerOption = maybeOption;
        std::transform(lowerOption.begin(), lowerOption.end(), lowerOption.begin(), ::tolower);
        if (lowerOption.rfind("gold=", 0) == 0)
        {
            if (hasGoldCap || !ParseUintToken(maybeOption.substr(5), goldCap))
            {
                OrderError(handler, "invalid", "Guild gold cap must be copper from 0 to 4294967295.");
                return true;
            }
            hasGoldCap = true;
            itemPart = optionSpace == std::string::npos ? std::string() : itemPart.substr(0, optionSpace);
        }
        else if (lowerOption.rfind("bring=", 0) == 0)
        {
            if (!bring.empty() || !GuildOrders::ParseBringList(maybeOption.substr(6), bring))
            {
                OrderError(handler, "invalid", "Bring list must be item ids joined by commas: bring=<item>,<item>.");
                return true;
            }
            itemPart = optionSpace == std::string::npos ? std::string() : itemPart.substr(0, optionSpace);
        }
        else
            break;
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
    // An unfinished enchant order of another giver blocks every new order:
    // only the giver or the guild master (or a rank that may remove
    // members) replaces it. Other order types keep today's rule.
    if (std::optional<OrderRecord> existing = GuildOrders::Find(guid.GetCounter()))
    {
        if (!existing->cancelled && existing->type == OrderType::Enchant &&
            !GuildOrders::IsFinished(existing->state))
        {
            Guild* guild = sGuildMgr.GetGuildById(guildId);
            bool sameGiver = existing->giverGuidLow == requester->GetGUIDLow();
            bool mayOverride = GiverMayOverrideEnchant(guild, requester);
            if (GuildOrders::DecideEnchantReplace(true, sameGiver, mayOverride) ==
                GuildOrders::EnchantReplaceVerdict::Busy)
            {
                std::string giverName = "?";
                sObjectMgr.GetPlayerNameByGUID(
                    ObjectGuid(HIGHGUID_PLAYER, existing->giverGuidLow), giverName);
                OrderError(handler, "busy", ((bot ? bot->GetName() : botName) +
                    std::string(" is busy with an enchant order for ") + giverName + ".").c_str());
                return true;
            }
        }
    }

    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
    if (!proto)
    {
        OrderError(handler, "invalid", "Unknown item.");
        return true;
    }
    if (!GuildOrders::IsOrderableItem(proto))
    {
        OrderError(handler, "bound", "That item is bound when picked up (or a quest or timed item), so the bot cannot hand it over.");
        return true;
    }

    if (!bring.empty() && type != OrderType::Craft)
    {
        OrderError(handler, "invalid", "bring= needs a craft order.");
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
    record.bringItems = bring;

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
            // Refuse at give, like search does: storing Cant would still
            // ACK the giver and start the reporting round-trip. The plan
            // verdict already ran the shared orderable rule, so the text
            // matches the Professions view and the search verdict.
            OrderError(handler, ReasonToken(plan.verdict),
                GuildOrders::CraftOrderRefusal(plan.verdict));
            return true;
        }
        else if (!plan.zones.empty())
        {
            record.farmZoneId = plan.zones[0].zoneId;
            record.farmChance = plan.zones[0].chance;
        }
        if (type == OrderType::Buy && record.farmZoneId == 0)
            record.farmZoneId = bot->GetTeam() == ALLIANCE ? kStormwindZone : kOrgrimmarZone;
        if (type == OrderType::Craft && !record.bringItems.empty())
        {
            // Only base reagents of this craft survive: anything else was
            // never the bot's to source. The bot is online here (craft
            // needs its spellbook), so the tree walk sees its recipes.
            auto recipeFor = [bot](uint32_t recipeItemId, GuildOrders::Recipe& out) -> bool
            {
                uint32_t spellId = ai::FindCraftSpellFor(bot, recipeItemId);
                if (!spellId)
                    return false;
                SpellEntry const* spell = sServerFacade.LookupSpellInfo(spellId);
                if (!spell)
                    return false;
                uint32_t effect = ai::FindCraftEffectFor(spellId, recipeItemId);
                out.yieldPerCast = TortoiseBots::CraftOrderPolicy::MinYieldPerCast(
                    spell->EffectBasePoints[effect], spell->EffectBaseDice[effect],
                    spell->EffectDieSides[effect]);
                for (uint8_t i = 0; i < MAX_SPELL_REAGENTS; ++i)
                {
                    if (!spell->ReagentCount[i] || spell->Reagent[i] <= 0)
                        continue;
                    out.reagents.push_back({ uint32_t(spell->Reagent[i]), uint32_t(spell->ReagentCount[i]) });
                }
                return true;
            };
            auto held = [](uint32_t heldId) -> uint32_t
            {
                // The full base set, not the current shortfall: stock in the
                // bags still counts at the ladder, but must not shrink which
                // ids the set names.
                (void)heldId;
                return 0;
            };
            std::vector<GuildOrders::ReagentNeed> base =
                GuildOrders::CollectBaseReagents(itemId, amount, recipeFor, held);
            std::vector<uint32_t> baseIds;
            baseIds.reserve(base.size());
            for (GuildOrders::ReagentNeed const& need : base)
                baseIds.push_back(need.itemId);
            record.bringItems = GuildOrders::FilterBringToBaseReagents(record.bringItems,
                baseIds.data(), baseIds.size());
        }
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
    // A parked refund stub never makes a bot busy: giving or reassigning to
    // that bot keeps the debt on the new row (Give carries held gold in),
    // so the refund drain still repays it. Stubs never count as running.
    std::optional<GuildOrders::OrderRecord> newBotOrder = GuildOrders::Find(newGuid.GetCounter());
    bool newBotBusy = newBotOrder && !newBotOrder->cancelled &&
        (!GuildOrders::IsFinished(newBotOrder->state) || newBotOrder->state == GuildOrders::OrderState::Stocked);
    if (newBotBusy)
    {
        OrderError(handler, "invalid", "The new bot already has an order.");
        return true;
    }
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
    bool canMake = false, canFarm = false, gathers = false;
    if (!GuildOrders::SubOrderCandidateUsable(live, sub->itemId, canMake, canFarm, gathers))
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
    // Cancel in any state closes the trade window first: the materials the
    // order gained still walk to the guild bank through the cancel path.
    if (bot && bot->IsInWorld() && bot->GetTrader())
    {
        WorldPacket cancel;
        uint32 cancelStatus = 0;
        cancel << cancelStatus;
        bot->GetSession()->HandleCancelTradeOpcode(cancel);
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
        handler->PSendSysMessage("%s",
            GuildOrders::FormatOrderLine(requester->GetGuildId(), order,
                GuildOrders::ReportedHave(order.delivered, order.reportedHave),
                GuildOrders::Carry(bot, order)).c_str());
    }
    handler->PSendSysMessage("TBM:ORDER_END");
    return true;
}

// Read-only (no CheckGiver): the requester only needs to be in the bot's
// guild, like `order list`. An order-less or cancelled bot answers with an
// empty pair, which is a real answer.
bool HandleOrderSteps(ChatHandler* handler, Player* requester, std::string const& botName)
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
    if (!requester->GetGuildId() || GuildOf(bot, guid) != requester->GetGuildId())
    {
        OrderError(handler, "not-guild", "The bot is not in your guild.");
        return true;
    }
    std::string name;
    if (bot)
        name = bot->GetName();
    else
    {
        std::string stored;
        name = sObjectMgr.GetPlayerNameByGUID(guid, stored) ? stored : botName;
    }
    name = ProtocolSafe(name);
    std::optional<GuildOrders::OrderStepHistory> h = GuildOrders::FindStepHistory(guid.GetCounter());
    std::vector<std::string> lines = GuildOrders::FormatOrderStepLines(name,
        h ? *h : GuildOrders::OrderStepHistory{}, static_cast<uint64_t>(std::time(nullptr)));
    for (std::string const& line : lines)
        handler->PSendSysMessage("%s", line.c_str());
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
        OrderError(handler, "invalid", "Usage: order <bot> <farm|buy|craft|cancel|meet|steps|preview> ... | order list | order search ....");
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
        // order search <craft|enchant> <text> (no bot: guild-wide who-search)
        // or order search <bot> <farm|buy|craft> [cat=<kind>] <text>. A first
        // argument that is one of farm|buy|craft|enchant means "no bot".
        std::string botName, typeToken;
        if (!(in >> botName >> typeToken))
        {
            OrderError(handler, "invalid", "Usage: order search <bot> <farm|buy|craft> [cat=<kind>] <text>.");
            return true;
        }
        std::string text;
        std::getline(in, text);
        text = Trim(text);
        // A first argument that is one of farm|buy|craft|enchant means "no
        // bot": the giver's guild is searched instead. A bot literally named
        // like a type keyword is not resolved here.
        std::string lowerFirst = botName;
        std::transform(lowerFirst.begin(), lowerFirst.end(), lowerFirst.begin(), ::tolower);
        if (GuildOrders::IsNoBotFirstArg(lowerFirst))
        {
            std::string combined = typeToken;
            if (!text.empty())
                combined += " " + text;
            return HandleOrderWhoSearch(handler, requester, lowerFirst, combined);
        }
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

    // order reassign <subOrderBot> <newBot> | order <bot> cancel | order <bot> meet |
    // order <bot> <type> <item> <amount> [hours] [keep] [gold=<copper>]
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
    // order <bot> cancel | order <bot> meet | order <bot> steps | order <bot> preview <farm|craft> <item> <amount> |
    // order <bot> <type> <item> <amount> [hours] [keep] [gold=<copper>]
    // order <bot> enchant <spell> [hours|inf] [gold=<copper>] [bring=<item>,<item>]
    std::string second;
    if (!(in >> second))
    {
        OrderError(handler, "invalid", "Usage: order <bot> <farm|buy|craft|enchant|cancel|meet|steps|preview> ....");
        return true;
    }
    std::string lowerSecond = second;
    std::transform(lowerSecond.begin(), lowerSecond.end(), lowerSecond.begin(), ::tolower);
    if (lowerSecond == "enchant")
    {
        std::string giveRest;
        std::getline(in, giveRest);
        return HandleOrderEnchantGive(handler, requester, first, giveRest);
    }
    if (lowerSecond == "meet")
        return HandleOrderMeet(handler, requester, first);
    if (lowerSecond == "cancel")
        return HandleOrderCancel(handler, requester, first);
    if (lowerSecond == "steps")
        return HandleOrderSteps(handler, requester, first);
    if (lowerSecond == "preview")
    {
        std::string typeToken, itemToken, amountToken;
        if (!(in >> typeToken >> itemToken >> amountToken))
        {
            OrderError(handler, "invalid", "Usage: order <bot> preview <farm|craft|enchant> <item> <amount> [bring=<item>,<item>].");
            return true;
        }
        std::string lowerPreview = typeToken;
        std::transform(lowerPreview.begin(), lowerPreview.end(), lowerPreview.begin(), ::tolower);
        if (lowerPreview == "enchant")
        {
            OrderError(handler, "invalid", "Enchant preview is not available: search shows the reagents instead.");
            return true;
        }
        std::string bringToken, extra;
        if (in >> bringToken)
        {
            if (in >> extra)
            {
                OrderError(handler, "invalid", "Usage: order <bot> preview <farm|craft> <item> <amount> [bring=<item>,<item>].");
                return true;
            }
        }
        return HandleOrderPreview(handler, requester, first, typeToken, itemToken, amountToken, bringToken);
    }


    return HandleOrderGive(handler, requester, rest);
}

} // namespace BotCommands
} // namespace TortoiseBots
