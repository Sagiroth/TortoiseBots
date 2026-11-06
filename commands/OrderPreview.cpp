// pi-lens-ignore-file: all
// `.bot order <bot> preview`: a read-only look at what the bot would do for
// one item and amount, before the player presses Send. Answers with one
// TBM:ORDER_PREVIEW line built from the same planner verdicts the give path
// uses (PlanGuildOrderSources) and the same whole-tree reagent walk the
// supply ladder uses (CollectBaseReagents), so preview and real order cannot
// disagree. Farm and craft only: a Buy preview would promise finished goods
// the gate refuses. Sends only on request, never on a timer. One bank SUM
// per distinct reagent; the auction mirror (a cached read) says whether the
// counter may have it, never a live auction scan.
#include "BotCommandHandlers.h"
#include "CommandShared.h"

#include "../ai/playerbot/GuildOrderSources.h"
#include "../ai/playerbot/GuildOrderCraft.h"
#include "../ai/playerbot/RandomBotFacade.h"
#include "../ai/playerbot/ServerFacade.h"
#include "../ai/playerbot/strategy/values/ItemUsageValue.h"
#include "../runtime/BotManager.h"
#include "../runtime/GuildOrderBringPolicy.h"
#include "../runtime/GuildOrderContract.h"
#include "../runtime/GuildOrderCraftPolicy.h"
#include "../runtime/GuildOrderPreviewPolicy.h"
#include "../runtime/GuildOrderReagentTree.h"
#include "../runtime/GuildOrderSourcesPolicy.h"
#include "../runtime/GuildOrderSubOrderPolicy.h"
#include "../runtime/GuildOrderStore.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "Guild/GuildMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Database/DatabaseEnv.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Database/DBCStores.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Mail/Mail.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "MapNodes/MasterPlayer.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "playerbot/PlayerbotAIConfig.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace TortoiseBots {
namespace BotCommands {
namespace {

using TortoiseBots::GuildOrders::OrderReason;
using TortoiseBots::GuildOrders::OrderType;
using TortoiseBots::GuildOrders::PreviewMinutesForKills;
using TortoiseBots::GuildOrders::PreviewNeedCount;
using TortoiseBots::GuildOrders::PreviewReagent;
using TortoiseBots::GuildOrders::PreviewReagentsToken;
using TortoiseBots::GuildOrders::ReasonToken;
using TortoiseBots::GuildOrders::TypeToken;

bool PreviewParseUint(std::string const& token, uint32_t& value)
{
    value = 0;
    if (token.empty() || token.size() > 10 || !std::all_of(token.begin(), token.end(), ::isdigit))
        return false;
    unsigned long parsed = std::stoul(token);
    if (parsed > 0xFFFFFFFFul)
        return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

bool PreviewParseItem(std::string const& token, uint32_t& itemId)
{
    itemId = 0;
    size_t link = token.find("Hitem:");
    if (link != std::string::npos)
    {
        size_t begin = link + 6;
        size_t end = begin;
        while (end < token.size() && std::isdigit(static_cast<unsigned char>(token[end])))
            ++end;
        return PreviewParseUint(token.substr(begin, end - begin), itemId) && itemId != 0;
    }
    return PreviewParseUint(token, itemId) && itemId != 0;
}

// Bags plus mailbox, the same two places the ladder's stock check counts
// (CollectMissingReagents spends bags + mail before naming a need).
uint32_t PreviewBags(Player* bot, uint32_t itemId)
{
    return (bot && itemId) ? bot->GetItemCount(itemId) : 0;
}

uint32_t PreviewMail(Player* bot, uint32_t itemId)
{
    if (!bot || !bot->GetSession() || !itemId)
        return 0;
    MasterPlayer* master = bot->GetSession()->GetMasterPlayer();
    if (!master)
        return 0;
    uint32_t count = 0;
    time_t curTime = time(nullptr);
    for (PlayerMails::iterator itr = master->GetMailBegin(); itr != master->GetMailEnd(); ++itr)
    {
        Mail* mail = *itr;
        if (!mail || mail->state == MAIL_STATE_DELETED || curTime < mail->deliver_time)
            continue;
        if (!mail->has_items)
            continue;
        for (MailItemInfoVec::const_iterator itemItr = mail->items.begin(); itemItr != mail->items.end();
            ++itemItr)
            if (itemItr->item_template == itemId)
            {
                Item* mailItem = master->GetMItem(itemItr->item_guid);
                count += mailItem ? mailItem->GetCount() : 1;
            }
    }
    return count;
}

// Guild bank stock of one item: one summed read, like the keep watch
// (BankItemCount). The preview asks once per distinct reagent, never per
// tick, and only for reagents the tree names.
uint32_t PreviewBank(uint32_t guildId, uint32_t itemId)
{
    if (!guildId || !itemId || !sGuildMgr.GetGuildById(guildId))
        return 0;
    std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
        "SELECT SUM(`count`) FROM `guild_bank` WHERE `guildid` = '%u' AND `isInferno` = '0' AND "
        "`item_template` = '%u'",
        guildId, itemId));
    if (!result)
        return 0;
    Field* fields = result->Fetch();
    return fields ? fields[0].GetUInt32() : 0;
}

// Whether the auction mirror holds any listing of the item for the bot's
// side: the same cached read NeedAhListed uses to wake a parked row. Price
// and affordability stay with the buy action; the preview only says the
// counter may have it.
bool PreviewAhListed(Player* bot, uint32_t itemId)
{
    if (!bot || !itemId)
        return false;
    return !sRandomBotFacade.GetAhPrices(itemId, bot).empty();
}

bool PreviewVendorUnlimited(uint32_t itemId)
{
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
    return proto && ::ai::ItemUsageValue::IsItemSoldByAnyVendor(proto) &&
        !::ai::ItemUsageValue::IsItemSoldByAnyVendorButHasLimitedMaxCount(proto);
}

// Whether another guild bot could take a sub-order for the item: the same
// Usable check PlaceSubOrders runs per roster member, over the same
// guild_member roster, capped at the same per-tick planner budget. Reads
// only live guildmates; the preview never queues anything.
bool PreviewGuildHasMaker(Player* self, uint32_t guildId, uint32_t itemId)
{
    if (!self || !guildId || !itemId)
        return false;
    std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
        "SELECT `guid` FROM `guild_member` WHERE `guildid` = '%u' ORDER BY `guid`",
        guildId));
    if (!result)
        return false;
    size_t evals = 0;
    do
    {
        Field* fields = result->Fetch();
        if (!fields)
            continue;
        uint32_t guidLow = fields[0].GetUInt32();
        if (!guidLow || guidLow == self->GetGUIDLow())
            continue;
        Player* bot = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, guidLow));
        if (!bot || !bot->IsInWorld() || bot->GetGuildId() != guildId)
            continue;
        if (!GuildOrders::IsDurableOrderBot(guidLow))
            continue;
        if (evals >= GuildOrders::kSubOrderPlannerEvalsPerTick)
            break;
        ++evals;
        bool canMake = false, canFarm = false, gathers = false;
        if (GuildOrders::SubOrderCandidateUsable(bot, itemId, canMake, canFarm, gathers))
            return true;
    } while (result->NextRow());
    return false;
}

std::string PreviewZoneName(uint32_t zoneId)
{
    if (!zoneId)
        return "-";
    AreaTableEntry const* entry = GetAreaEntryByAreaID(zoneId);
    if (!entry)
        return "-";
    AreaTableEntry const* zone = entry->ZoneId ? GetAreaEntryByAreaID(entry->ZoneId) : entry;
    if (!zone || !zone->Name)
        return "-";
    return zone->Name;
}

} // namespace

// `.bot order <bot> preview <farm|craft> <item> <amount>`: read-only, needs
// the bot online (its spellbook and bags are read live) and the requester in
// the bot's guild with giver rights, like give. Buy previews are refused:
// they would promise finished goods the gate refuses.
bool HandleOrderPreview(ChatHandler* handler, Player* requester, std::string const& botName,
    std::string const& typeToken, std::string const& itemToken, std::string const& amountToken,
    std::string const& bringToken)
{
    Player* bot = nullptr;
    ObjectGuid guid;
    if (botName.empty())
    {
        SendActionError(handler, "order", "invalid", "Usage: order <bot> preview <farm|craft> <item> <amount>.");
        return true;
    }
    // Resolve inline: the shared helpers live in Order.cpp's anonymous
    // namespace, so the preview repeats the small name fix here.
    std::string fixed = botName;
    if (fixed.empty() || !normalizePlayerName(fixed))
    {
        SendActionError(handler, "order", "no-bot", "No such bot.");
        return true;
    }
    bot = sObjectMgr.GetPlayer(fixed.c_str());
    if (!bot)
        bot = sObjectAccessor.FindPlayerByName(fixed.c_str());
    if (!bot)
        guid = sObjectMgr.GetPlayerGuidByName(fixed);
    else
        guid = bot->getObjectGuid();
    if (guid.IsEmpty())
    {
        SendActionError(handler, "order", "no-bot", "No such bot.");
        return true;
    }
    if (!bot)
    {
        SendActionError(handler, "order", "no-bot", "Preview needs the bot online.");
        return true;
    }
    OrderType type = OrderType::Farm;
    std::string lowerType = typeToken;
    std::transform(lowerType.begin(), lowerType.end(), lowerType.begin(), ::tolower);
    if (!GuildOrders::ParseType(lowerType, type) || type == OrderType::Buy || type == OrderType::Enchant)
    {
        SendActionError(handler, "order", "invalid", "Usage: order <bot> preview <farm|craft> <item> <amount>.");
        return true;
    }
    uint32_t itemId = 0;
    std::vector<uint32_t> bring;
    if (!bringToken.empty())
    {
        std::string lower = bringToken;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.rfind("bring=", 0) != 0 || !GuildOrders::ParseBringList(bringToken.substr(6), bring))
        {
            SendActionError(handler, "order", "invalid", "Usage: order <bot> preview <farm|craft> <item> <amount> [bring=<item>,<item>].");
            return true;
        }
        if (type != OrderType::Craft)
        {
            SendActionError(handler, "order", "invalid", "bring= needs a craft preview.");
            return true;
        }
    }
    if (!PreviewParseItem(itemToken, itemId))
    {
        SendActionError(handler, "order", "invalid", "Unknown item: shift-click an item link or pass its id.");
        return true;
    }
    uint32_t amount = 0;
    if (!PreviewParseUint(amountToken, amount) || amount < 1 || amount > 200)
    {
        SendActionError(handler, "order", "invalid", "Amount must be 1-200.");
        return true;
    }
    if (!BotManager::Instance().IsBot(guid))
    {
        SendActionError(handler, "order", "no-bot", "That character is not a bot.");
        return true;
    }
    uint32_t guildId = bot->GetGuildId();
    if (!guildId || !requester->GetGuildId() || requester->GetGuildId() != guildId)
    {
        SendActionError(handler, "order", "not-guild", "The bot is not in your guild.");
        return true;
    }
    Guild* guild = sGuildMgr.GetGuildById(guildId);
    if (!requester->GetSession() || !requester->GetSession()->HasNetworkTransport() ||
        !guild || !guild->HasRankRight(requester->GetRank(), GR_RIGHT_EOFFNOTE))
    {
        SendActionError(handler, "order", "forbidden", "Your guild rank may not give orders.");
        return true;
    }
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
    if (!proto)
    {
        SendActionError(handler, "order", "invalid", "Unknown item.");
        return true;
    }
    if (!GuildOrders::IsOrderableItem(proto))
    {
        SendActionError(handler, "order", "bound",
            "That item is bound when picked up (or a quest or timed item), so the bot cannot hand it over.");
        return true;
    }
    if (type == OrderType::Craft && !ai::FindCraftSpellFor(bot, itemId))
    {
        SendActionError(handler, "order", "norecipe", "The bot does not know that recipe.");
        return true;
    }
    // The same planner the give path runs: verdict, zones and skill come
    // from PlanGuildOrderSources, so a refusal here reads exactly like the
    // give refusal and the search verdict.
    GuildOrders::SourcePlan plan = ai::PlanGuildOrderSources(bot, itemId, type);
    if (plan.verdict != OrderReason::None)
    {
        std::string refusal = GuildOrders::CraftOrderRefusal(plan.verdict);
        if (type != OrderType::Craft)
            refusal = "The bot cannot get that item.";
        SendActionError(handler, "order", ReasonToken(plan.verdict), refusal.c_str());
        return true;
    }
    std::string botNameSafe = BotCommands::ProtocolSafe(bot->GetName());
    std::string itemNameSafe = BotCommands::ProtocolSafe(proto->Name1);
    // The whole-tree reagent walk the ladder runs (CollectBaseReagents with
    // the bot's known recipes and bags + mail as held): one row per base
    // reagent with the full need. Farm orders name the order item itself;
    // a craft output the bot already holds needs nothing.
    uint32_t outputHeld = PreviewBags(bot, itemId) + PreviewMail(bot, itemId);
    uint32_t outputMissing = outputHeld >= amount ? 0 : amount - outputHeld;
    std::vector<std::pair<uint32_t, uint32_t>> needs;
    if (type == OrderType::Farm)
    {
        if (outputMissing)
            needs.push_back({ itemId, outputMissing });
    }
    else if (outputMissing)
    {
        auto recipeFor = [bot](uint32_t recipeItemId, GuildOrders::Recipe& out) -> bool
        {
            uint32_t spellId = ai::FindCraftSpellFor(bot, recipeItemId);
            if (!spellId || PreviewVendorUnlimited(recipeItemId))
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
        auto held = [bot](uint32_t heldId) -> uint32_t
        {
            return PreviewBags(bot, heldId) + PreviewMail(bot, heldId);
        };
        for (GuildOrders::ReagentNeed const& need :
            GuildOrders::CollectBaseReagents(itemId, outputMissing, recipeFor, held))
            needs.push_back({ need.itemId, need.count });
    }
    // Per-row sourcing in ladder order (bank, AH, self, sub-order): the same
    // lanes RefreshGuildOrderSupply walks, read as one pass over cached
    // lookups. A vendor-unlimited row is bought at a shop, never farmed or
    // delegated; a row the bot can farm or make itself never becomes a
    // sub-order; the rest is what the player must send.
    // Only base reagents of this craft survive in the tick set, like the
    // give path filters: anything else was never the bot's to source. The
    // walk uses zero stock so the set names the craft's full base list,
    // not the current shortfall (the rows above net stock themselves).
    {
        std::vector<uint32_t> fullBase;
        if (type == OrderType::Craft)
        {
            auto fullRecipeFor = [bot](uint32_t recipeItemId, GuildOrders::Recipe& out) -> bool
            {
                uint32_t spellId = ai::FindCraftSpellFor(bot, recipeItemId);
                if (!spellId || PreviewVendorUnlimited(recipeItemId))
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
            auto noStock = [](uint32_t heldId) -> uint32_t
            {
                (void)heldId;
                return 0;
            };
            for (GuildOrders::ReagentNeed const& need :
                GuildOrders::CollectBaseReagents(itemId, amount, fullRecipeFor, noStock))
                fullBase.push_back(need.itemId);
        }
        bring = GuildOrders::FilterBringToBaseReagents(bring, fullBase.data(), fullBase.size());
    }
    std::vector<PreviewReagent> rows;
    std::unordered_map<uint32_t, uint32_t> bankCache;
    std::unordered_set<uint32_t> branch;
    branch.insert(itemId);
    uint32_t maxDepth = sPlayerbotAIConfig.guildOrderSubOrderDepth;
    if (maxDepth < 1 || maxDepth > 16)
        maxDepth = GuildOrders::kSubOrderDefaultDepth;
    double hardestKills = 0.0;
    bool hasFarmLeg = false;
    for (auto const& [needId, needCount] : needs)
    {
        if (!needId || !needCount)
            continue;
        // A ticked material is the giver's to bring: no bank, auction,
        // vendor, farm or sub-order lane, so the whole shortfall past the
        // bot's own bags and mailbox lands in the player column.
        bool ticked = GuildOrders::BringTickedBlocksLeg(bring, needId);
        PreviewReagent row;
        row.itemId = needId;
        row.required = needCount;
        row.inBags = PreviewBags(bot, needId);
        row.inMail = PreviewMail(bot, needId);
        if (!ticked)
        {
            auto bankFound = bankCache.find(needId);
            if (bankFound == bankCache.end())
                bankFound = bankCache.emplace(needId, PreviewBank(guildId, needId)).first;
            row.inBank = bankFound->second;
        }
        uint32_t held = row.inBags + row.inMail + row.inBank;
        uint32_t shortfall = held >= needCount ? 0 : needCount - held;
        if (!shortfall || ticked)
        {
            rows.push_back(row);
            continue;
        }
        if (PreviewVendorUnlimited(needId))
        {
            row.vendorCount = shortfall;
            rows.push_back(row);
            continue;
        }
        if (PreviewAhListed(bot, needId) && sPlayerbotAIConfig.guildOrderBankWithdraw)
        {
            row.ahCount = shortfall;
            rows.push_back(row);
            continue;
        }
        GuildOrders::SourcePlan farm = ai::PlanGuildOrderSources(bot, needId, OrderType::Farm);
        if (farm.verdict == OrderReason::None && !farm.zones.empty())
        {
            row.farmCount = shortfall;
            row.farmZone = BotCommands::ProtocolSafe(PreviewZoneName(farm.zones[0].zoneId));
            hasFarmLeg = true;
            if (farm.bestChance > 0.0f)
                hardestKills = std::max(hardestKills,
                    GuildOrders::ExpectedKills(shortfall, farm.bestChance));
            else
                hardestKills = std::max(hardestKills, double(shortfall) * 2.0);
            rows.push_back(row);
            continue;
        }
        if (sPlayerbotAIConfig.guildOrderSubOrders && maxDepth >= 2 && branch.count(needId) == 0 &&
            PreviewGuildHasMaker(bot, guildId, needId))
        {
            row.subCount = shortfall;
            rows.push_back(row);
            continue;
        }
        rows.push_back(row);
    }
    uint32_t minutes = PreviewMinutesForKills(hardestKills, hasFarmLeg);
    if (type == OrderType::Farm && !plan.zones.empty() && plan.bestChance > 0.0f)
        minutes = PreviewMinutesForKills(GuildOrders::ExpectedKills(amount, plan.bestChance), true);
    std::string difficulty = ai::PlanOrderDifficulty(bot, itemId, type, amount);
    uint32_t goldCap = 0;
    bool hasGoldCap = false;
    if (std::optional<GuildOrders::OrderRecord> live = GuildOrders::Find(bot->GetGUIDLow()))
    {
        goldCap = live->goldCap;
        hasGoldCap = live->hasGoldCap;
    }
    std::string zone = "-";
    if (!plan.zones.empty())
        zone = BotCommands::ProtocolSafe(PreviewZoneName(plan.zones[0].zoneId));
    handler->PSendSysMessage("TBM:ORDER_PREVIEW|%s|%s|%u|%s|%u|%u|%s|%s|%s|%u|%s|%s",
        botNameSafe.c_str(), TypeToken(type), itemId, itemNameSafe.c_str(), amount,
        outputMissing, difficulty.c_str(), zone.c_str(), PreviewReagentsToken(rows).c_str(),
        minutes, hasGoldCap ? std::to_string(goldCap).c_str() : "-",
        GuildOrders::BringItemsToken(bring).c_str());
    return true;
}

} // namespace BotCommands
} // namespace TortoiseBots
