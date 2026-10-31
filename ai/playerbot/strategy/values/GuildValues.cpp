
#include "playerbot/playerbot.h"
#include "GuildValues.h"
#include "playerbot/strategy/values/BudgetValues.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/GuildOrderSources.h"
#include "playerbot/GuildOrderCraft.h"
#include "runtime/GuildOrderStore.h"
#include "runtime/GuildOrderSourcesPolicy.h"
#include "runtime/GuildOrderSupplyPolicy.h"
#include "runtime/GuildOrderRefundPolicy.h"
#include "runtime/AltBotService.h"
#include "Guild/GuildMgr.h"
#include "Mail/Mail.h"
#include "MapNodes/MasterPlayer.h"

using namespace ai;

namespace
{

using TortoiseBots::GuildOrders::NeedItem;
using TortoiseBots::GuildOrders::BankLegSkipped;
using TortoiseBots::GuildOrders::OrderRecord;
using TortoiseBots::GuildOrders::OrderReason;
using TortoiseBots::GuildOrders::OrderType;
using TortoiseBots::GuildOrders::SupplyOptions;
using TortoiseBots::GuildOrders::SupplyStep;

struct SupplyCandidate
{
    uint32_t itemId = 0;
    uint32_t count = 0;
    bool craft = false;
    bool takeMail = false;
};

bool IsUnlimitedVendorItem(uint32_t itemId)
{
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
    return proto && ItemUsageValue::IsItemSoldByAnyVendor(proto) &&
        !ItemUsageValue::IsItemSoldByAnyVendorButHasLimitedMaxCount(proto);
}

// Every reagent the bot still needs for `count` casts of the output, one
// entry per reagent: the need list and its whisper name them all at once
// instead of asking the player again after each delivery.
void CollectMissingReagents(PlayerbotAI* ai, Player* bot, uint32_t itemId, uint32_t count,
    std::vector<NeedItem>& out)
{
    if (!ai || !bot || !itemId || !count)
        return;
    uint32_t spellId = ai::FindCraftSpellFor(bot, itemId);
    if (!spellId || IsUnlimitedVendorItem(itemId))
    {
        uint32_t owned = ai->GetInventoryItemsCountWithId(itemId) + ai::OrderMailboxItems(bot, itemId);
        if (owned < count)
            out.push_back(NeedItem{ itemId, count - owned });
        return;
    }
    SpellEntry const* spell = sServerFacade.LookupSpellInfo(spellId);
    if (!spell)
        return;
    uint32_t casts = ai::CraftCastsForRemaining(spellId, itemId, count);
    for (uint8_t i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (!spell->ReagentCount[i] || spell->Reagent[i] <= 0)
            continue;
        uint32_t reagentId = uint32_t(spell->Reagent[i]);
        CollectMissingReagents(ai, bot, reagentId, spell->ReagentCount[i] * casts, out);
    }
}

bool FindSupplyCandidate(PlayerbotAI* ai, Player* bot, uint32_t itemId, uint32_t count,
    std::vector<uint32_t>& path, uint32_t depth, SupplyCandidate& out)
{
    if (!ai || !bot || !itemId || !count)
        return false;
    if (depth >= 8 || std::find(path.begin(), path.end(), itemId) != path.end())
    {
        out = { itemId, count, false };
        return true;
    }

    uint32_t spellId = ai::FindCraftSpellFor(bot, itemId);
    if (!spellId || IsUnlimitedVendorItem(itemId))
    {
        out = { itemId, count, false };
        return true;
    }
    SpellEntry const* spell = sServerFacade.LookupSpellInfo(spellId);
    if (!spell)
    {
        out = { itemId, count, false };
        return true;
    }

    path.push_back(itemId);
    uint32_t casts = ai::CraftCastsForRemaining(spellId, itemId, count);
    std::vector<NeedItem> missingAll;
    for (uint8_t i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (!spell->ReagentCount[i] || spell->Reagent[i] <= 0)
            continue;
        uint32_t reagentId = uint32_t(spell->Reagent[i]);
        uint32_t required = spell->ReagentCount[i] * casts;
        uint32_t inBags = ai->GetInventoryItemsCountWithId(reagentId);
        uint32_t inMail = ai::OrderMailboxItems(bot, reagentId);
        if (inMail && inBags < required)
        {
            path.pop_back();
            out = { reagentId, required - inBags, false, true };
            return true;
        }
        uint32_t owned = inBags + inMail;
        if (owned >= required)
            continue;
        SupplyCandidate nested;
        if (FindSupplyCandidate(ai, bot, reagentId, required - owned, path, depth + 1, nested) && !nested.craft)
            missingAll.push_back(NeedItem{ nested.itemId, nested.count });
    }
    path.pop_back();
    if (!missingAll.empty())
    {
        out = { missingAll.front().itemId, missingAll.front().count, false };
        return true;
    }
    out = { itemId, count, true };
    return true;
}

SupplyStep ChooseSourceStep(Player* bot, OrderRecord const& order, SupplyCandidate const& candidate)
{
    if (candidate.craft)
        return SupplyStep::Craft;
    // A banker visit that found none of this item skips the bank leg until
    bool bankSkipped = BankLegSkipped(candidate.itemId, order.bankEmptyItemId,
        WorldTimer::getMSTime(), order.bankEmptyMs);
    bool sameItem = order.supplyItemId == candidate.itemId;
    SupplyStep current = sameItem ? order.supplyStep : SupplyStep::None;
    bool canFarm = IsUnlimitedVendorItem(candidate.itemId) && order.reason != OrderReason::NoSource;
    if (!canFarm)
    {
        TortoiseBots::GuildOrders::SourcePlan farm = ai::PlanGuildOrderSources(bot, candidate.itemId, OrderType::Farm);
        canFarm = farm.verdict == OrderReason::None && !farm.zones.empty();
    }
    if (sameItem)
    {
        if (current == SupplyStep::AuctionHouse || current == SupplyStep::Sub || current == SupplyStep::Need)
            return current;
        if (current == SupplyStep::Farm && canFarm)
            return current;
        if (current == SupplyStep::Craft && candidate.craft)
            return current;
        if (current == SupplyStep::Bags && ai::OrderMailboxItems(bot, candidate.itemId) > 0)
            return current;
        if (current == SupplyStep::Bags && order.supplyReturnStep != SupplyStep::None)
            return order.supplyReturnStep;
        if (current == SupplyStep::Bank && sPlayerbotAIConfig.guildOrderBankWithdraw && !bankSkipped)
            return current;
    }
    SupplyOptions options;
    options.bankEnabled = sPlayerbotAIConfig.guildOrderBankWithdraw && !bankSkipped;
    options.bankAttempted = bankSkipped || (sameItem && (current == SupplyStep::AuctionHouse || current == SupplyStep::Farm ||
        current == SupplyStep::Craft || current == SupplyStep::Sub || current == SupplyStep::Need));
    options.auctionHouseAttempted = sameItem && (current == SupplyStep::Farm || current == SupplyStep::Craft ||
        current == SupplyStep::Sub || current == SupplyStep::Need);
    options.selfCanCraft = candidate.craft;
    options.selfCanFarm = canFarm;
    options.subAttempted = sameItem && current == SupplyStep::Need;
    return TortoiseBots::GuildOrders::NextSupplyStep(options);
}

SupplyStep MailReturnStep(Player* bot, OrderRecord const& order, SupplyCandidate const& candidate)
{
    if (order.supplyItemId == candidate.itemId)
    {
        if (order.supplyStep == SupplyStep::Bags)
            return order.supplyReturnStep == SupplyStep::None ? SupplyStep::Bank : order.supplyReturnStep;
        if (order.supplyStep == SupplyStep::Bank || order.supplyStep == SupplyStep::AuctionHouse ||
            order.supplyStep == SupplyStep::Farm || order.supplyStep == SupplyStep::Craft ||
            order.supplyStep == SupplyStep::Sub)
            return order.supplyStep;
    }
    return ChooseSourceStep(bot, order, candidate);
}

void SetSupplyStage(PlayerbotAI* ai, Player* bot, OrderRecord const& order, SupplyCandidate const& candidate,
    SupplyStep step, SupplyStep returnStep = SupplyStep::None)
{
    if (step == SupplyStep::Sub)
    {
        if (TortoiseBots::GuildOrders::RequestSupply(order.botGuidLow, candidate.itemId, candidate.count))
        {
            TortoiseBots::GuildOrders::SetSupply(order.botGuidLow, SupplyStep::Sub,
                candidate.itemId, candidate.count, SupplyStep::None, order.reason);
            return;
        }
        std::vector<NeedItem> missing;
        CollectMissingReagents(ai, bot, order.itemId, order.amount > order.delivered ? order.amount - order.delivered : 0, missing);
        if (missing.empty())
            missing.push_back(NeedItem{ candidate.itemId, candidate.count });
        TortoiseBots::GuildOrders::SetNeedItems(order.botGuidLow,
            missing, order.reason == OrderReason::BankDenied);
        return;
    }
    if (step == SupplyStep::Need)
    {
        std::vector<NeedItem> missing;
        CollectMissingReagents(ai, bot, order.itemId, order.amount > order.delivered ? order.amount - order.delivered : 0, missing);
        if (missing.empty())
            missing.push_back(NeedItem{ candidate.itemId, candidate.count });
        TortoiseBots::GuildOrders::SetNeedItems(order.botGuidLow,
            missing, order.reason == OrderReason::BankDenied);
        return;
    }
    (void)ai;
    (void)bot;
    TortoiseBots::GuildOrders::SetSupply(order.botGuidLow, step, candidate.itemId,
        candidate.count, returnStep, order.reason);
}

void RefreshGuildOrderSupply(PlayerbotAI* ai, Player* bot, OrderRecord const& order)
{
    if (!ai || !bot || order.state != TortoiseBots::GuildOrders::OrderState::Working)
        return;

    uint32_t carried = ai->GetInventoryItemsCountWithId(order.itemId);
    uint32_t mailed = ai::OrderMailboxItems(bot, order.itemId);
    if (order.type != OrderType::Craft)
    {
        uint32_t have = order.delivered + carried + mailed;
        uint32_t missing = have >= order.amount ? 0 : order.amount - have;
        if (mailed && order.delivered + carried < order.amount)
        {
            SupplyCandidate candidate{ order.itemId, missing, false, true };
            SupplyStep afterMail = MailReturnStep(bot, order, candidate);
            if (order.supplyStep != SupplyStep::Bags || order.supplyItemId != order.itemId)
                TortoiseBots::GuildOrders::SetSupply(order.botGuidLow, SupplyStep::Bags,
                    order.itemId, missing, afterMail, order.reason);
            return;
        }
        if (!missing)
        {
            if (carried + order.delivered >= order.amount)
                TortoiseBots::GuildOrders::SetSupply(order.botGuidLow, SupplyStep::Deliver,
                    order.itemId, 0, SupplyStep::None, order.reason);
            return;
        }
        SupplyCandidate candidate{ order.itemId, missing, false };
        if (mailed)
        {
            SupplyStep afterMail = MailReturnStep(bot, order, candidate);
            if (order.supplyStep != SupplyStep::Bags || order.supplyItemId != order.itemId)
                TortoiseBots::GuildOrders::SetSupply(order.botGuidLow, SupplyStep::Bags,
                    order.itemId, missing, afterMail, order.reason);
            return;
        }
        // Farm and buy orders never take their own order item from the bank:
        // the bank holds reagents, and withdrawing the finished item would
        // just deposit it back on delivery. Craft reagents below still use
        // the bank first.
        SupplyStep step = ChooseSourceStep(bot, order, candidate);
        if (step == SupplyStep::Bank)
            step = SupplyStep::AuctionHouse;
        SupplyStep fallback = step == SupplyStep::AuctionHouse ? TortoiseBots::GuildOrders::NextSupplyStep(
            SupplyOptions{ false, true, true, false,
                IsUnlimitedVendorItem(order.itemId) ||
                    (ai::PlanGuildOrderSources(bot, order.itemId, OrderType::Farm).verdict == OrderReason::None &&
                     !ai::PlanGuildOrderSources(bot, order.itemId, OrderType::Farm).zones.empty()), false }) :
            SupplyStep::None;
        SetSupplyStage(ai, bot, order, candidate, step, fallback);
        return;
    }

    uint32_t outputHave = order.delivered + carried + mailed;
    uint32_t outputMissing = outputHave >= order.amount ? 0 : order.amount - outputHave;
    if (mailed && order.delivered + carried < order.amount)
    {
        SupplyCandidate candidate{ order.itemId, outputMissing, false, true };
        SupplyStep afterMail = MailReturnStep(bot, order, candidate);
        if (order.supplyStep != SupplyStep::Bags || order.supplyItemId != order.itemId)
            TortoiseBots::GuildOrders::SetSupply(order.botGuidLow, SupplyStep::Bags,
                order.itemId, outputMissing, afterMail, order.reason);
        return;
    }
    if (!outputMissing)
    {
        if (mailed && order.delivered + carried < order.amount)
            TortoiseBots::GuildOrders::SetSupply(order.botGuidLow, SupplyStep::Bags,
                order.itemId, 0, SupplyStep::None, order.reason);
        else if (order.delivered + carried >= order.amount)
            TortoiseBots::GuildOrders::SetSupply(order.botGuidLow, SupplyStep::Deliver,
                order.itemId, 0, SupplyStep::None, order.reason);
        return;
    }

    SupplyCandidate candidate;
    std::vector<uint32_t> path;
    if (!FindSupplyCandidate(ai, bot, order.itemId, outputMissing, path, 0, candidate))
    {
        TortoiseBots::GuildOrders::SetState(order.botGuidLow,
            TortoiseBots::GuildOrders::OrderState::Cant, OrderReason::NoRecipe);
        return;
    }
    if (candidate.craft)
    {
        SetSupplyStage(ai, bot, order, candidate, SupplyStep::Craft);
        return;
    }
    if (ai::OrderMailboxItems(bot, candidate.itemId) > 0)
    {
        SupplyStep afterMail = MailReturnStep(bot, order, candidate);
        if (order.supplyStep != SupplyStep::Bags || order.supplyItemId != candidate.itemId)
            TortoiseBots::GuildOrders::SetSupply(order.botGuidLow, SupplyStep::Bags,
                candidate.itemId, candidate.count, afterMail, order.reason);
        return;
    }
    SupplyStep step = ChooseSourceStep(bot, order, candidate);
    SupplyOptions fallbackOptions;
    fallbackOptions.bankEnabled = false;
    fallbackOptions.bankAttempted = true;
    fallbackOptions.auctionHouseAttempted = false;
    fallbackOptions.selfCanCraft = candidate.craft;
    fallbackOptions.selfCanFarm = IsUnlimitedVendorItem(candidate.itemId);
    if (!fallbackOptions.selfCanFarm)
    {
        TortoiseBots::GuildOrders::SourcePlan farm = ai::PlanGuildOrderSources(bot,
            candidate.itemId, OrderType::Farm);
        fallbackOptions.selfCanFarm = farm.verdict == OrderReason::None && !farm.zones.empty();
    }
    SupplyStep fallback = TortoiseBots::GuildOrders::NextSupplyStep(fallbackOptions);
    SetSupplyStage(ai, bot, order, candidate, step, fallback);
}

} // namespace

GuildOrder GuildOrderValue::Calculate()
{
    GuildOrder order;

    if (!bot->GetGuildId())
        return order;

    // The store is the only order source; bots never read officer notes.
    std::optional<TortoiseBots::GuildOrders::OrderRecord> stored =
        TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
    if (!stored || stored->guildId != bot->GetGuildId() ||
        TortoiseBots::GuildOrders::IsFinished(stored->state))
        return order;

    switch (stored->type)
    {
        case TortoiseBots::GuildOrders::OrderType::Farm:
            order.type = GuildOrderType::Farm;
            break;
        case TortoiseBots::GuildOrders::OrderType::Buy:
            order.type = stored->farmRest ? GuildOrderType::Farm : GuildOrderType::AuctionHouse;
            break;
        case TortoiseBots::GuildOrders::OrderType::Craft:
            order.type = GuildOrderType::Craft;
            break;
    }
    order.itemId = stored->itemId;
    order.amount = stored->amount;
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(stored->itemId);
    order.target = proto ? proto->Name1 : std::to_string(stored->itemId);
    return order;
}

uint32 ai::OrderMailboxItems(Player* bot, uint32 itemId)
{
    if (!bot || !bot->GetSession() || !itemId)
        return 0;
    MasterPlayer* master = bot->GetSession()->GetMasterPlayer();
    if (!master)
        return 0;
    uint32 count = 0;
    time_t curTime = time(nullptr);
    for (PlayerMails::iterator itr = master->GetMailBegin(); itr != master->GetMailEnd(); ++itr)
    {
        Mail* mail = *itr;
        if (!mail || mail->state == MAIL_STATE_DELETED || curTime < mail->deliver_time)
            continue;
        if (!mail->has_items)
            continue;
        for (MailItemInfoVec::const_iterator itemItr = mail->items.begin(); itemItr != mail->items.end(); ++itemItr)
            if (itemItr->item_template == itemId)
            {
                Item* mailItem = master->GetMItem(itemItr->item_guid);
                count += mailItem ? mailItem->GetCount() : 1;
            }
    }
    return count;
}

bool ai::OrderBotMayTravel(PlayerbotAI* ai, Player* bot)
{
    if (!ai || !bot)
        return false;
    // An autonomous alt travels like a pool bot even though it stays an
    // owned bot. Checked first: the master test below would keep it out
    // while its owner is online.
    if (TortoiseBots::AltBotService::Instance().IsAutonomous(bot->GetObjectGuid()))
        return true;
    if (ai->HasRealPlayerMaster())
        return false;
    return sRandomBotFacade.IsFreeBot(bot);
}

bool ai::OrderStillNeedsWork(Player* bot, GuildOrder const& order)
{
    if (!bot || !order.itemId || !order.amount)
        return false;
    std::optional<TortoiseBots::GuildOrders::OrderRecord> stored =
        TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
    if (!stored || stored->itemId != order.itemId)
        return false;
    if (stored->state == TortoiseBots::GuildOrders::OrderState::Waiting ||
        stored->state == TortoiseBots::GuildOrders::OrderState::Delivering)
        return false;
    return stored->delivered + bot->GetItemCount(order.itemId) +
        OrderMailboxItems(bot, order.itemId) < order.amount;
}

bool HasGuildTravelOrderValue::Calculate()
{
    // No order work while grouped with a player, for any order type. An
    // autonomous alt still travels: it plays on its own like a pool bot.
    if (!OrderBotMayTravel(ai, bot))
        return false;

    GuildOrder order = AI_VALUE(GuildOrder, "guild order");
    if (std::optional<OrderRecord> stored = TortoiseBots::GuildOrders::Find(bot->GetGUIDLow()))
        RefreshGuildOrderSupply(ai, bot, *stored);
    std::optional<OrderRecord> current = TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
    if (!current || current->state != TortoiseBots::GuildOrders::OrderState::Working ||
        (current->supplyStep != SupplyStep::Bank && current->supplyStep != SupplyStep::AuctionHouse &&
         current->supplyStep != SupplyStep::Farm && current->supplyStep != SupplyStep::Bags))
        return false;
    // A craft order short on a farmable reagent travels like a farm order.
    if (order.IsCraftOrder())
        return AI_VALUE(uint32, "guild order farm item") != 0;
    if (!order.IsTravelOrder())
        return false;

    // No order work while delivering or done with the amount (delivered
    // plus bags plus mailbox). Mail waiting never blocks the leg: the
    // travel code walks to a mailbox and collects it before farming, buying
    // or delivering, since bought items arrive by mail.
    if (!OrderStillNeedsWork(bot, order))
        return false;
    return true;
}

uint32 GuildOrderVendorReagentValue::Calculate()
{
    GuildOrder order = AI_VALUE(GuildOrder, "guild order");
    if (!order.IsCraftOrder())
        return 0;

    std::optional<TortoiseBots::GuildOrders::OrderRecord> record =
        TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
    if (record && record->state != TortoiseBots::GuildOrders::OrderState::Working)
        return 0;
    if (!record)
        return 0;
    RefreshGuildOrderSupply(ai, bot, *record);
    record = TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
    if (!record || record->supplyStep != SupplyStep::Farm || !record->supplyItemId ||
        record->reason == OrderReason::NoSource || !IsUnlimitedVendorItem(record->supplyItemId))
        return 0;
    return record->supplyItemId;
}

bool GuildOrderNeedsFocusValue::Calculate()
{
    GuildOrder order = AI_VALUE(GuildOrder, "guild order");
    if (!order.IsCraftOrder())
        return false;

    std::optional<TortoiseBots::GuildOrders::OrderRecord> record =
        TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
    if (record && record->state != TortoiseBots::GuildOrders::OrderState::Working)
        return false;

    uint32 itemId = order.itemId;
    uint32 orderAmount = order.amount;
    if (!itemId || !orderAmount)
        return false;

    uint32 delivered = record ? std::min(record->delivered, record->amount) : 0;
    uint32 currentCount = ai->GetInventoryItemsCountWithId(itemId) + delivered;
    if (currentCount >= orderAmount)
        return false;

    uint32 craftSpellId = ai::FindCraftSpellFor(bot, itemId);
    if (!craftSpellId)
        return false;

    const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(craftSpellId);
    if (!pSpellInfo || !pSpellInfo->RequiresSpellFocus)
        return false;

    for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (!pSpellInfo->ReagentCount[i] || pSpellInfo->Reagent[i] <= 0)
            continue;

        uint32 have = ai->GetInventoryItemsCountWithId(pSpellInfo->Reagent[i]);
        if (TortoiseBots::CraftOrderPolicy::CastsAffordable(have, pSpellInfo->ReagentCount[i]) == 0)
            return false;
    }

    std::list<ObjectGuid> objects = AI_VALUE(std::list<ObjectGuid>, "nearest game objects no los");
    for (ObjectGuid guid : objects)
    {
        GameObject* go = ai->GetGameObject(guid);
        if (!go || !sServerFacade.isSpawned(go))
            continue;

        GameObjectInfo const* info = sObjectMgr.GetGameObjectInfo(go->GetEntry());
        if (!info || info->type != GAMEOBJECT_TYPE_SPELL_FOCUS ||
            info->spellFocus.focusId != pSpellInfo->RequiresSpellFocus)
            continue;

        if (bot->GetDistance(go) <= sPlayerbotAIConfig.sightDistance)
            return false;
    }

    return true;
}

uint32 ai::GuildOrderCraftReagentFor(PlayerbotAI* ai, Player* bot, uint32 itemId, uint32 amount,
    uint32 delivered)
{
    if (!ai || !bot || !itemId || !amount)
        return 0;

    uint32 currentCount = ai->GetInventoryItemsCountWithId(itemId) + std::min(delivered, amount);
    if (currentCount >= amount)
        return 0;

    uint32 craftSpellId = ai::FindCraftSpellFor(bot, itemId);
    if (!craftSpellId)
        return 0;

    const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(craftSpellId);
    if (!pSpellInfo)
        return 0;

    uint32 casts = ai::CraftCastsForRemaining(craftSpellId, itemId, amount - currentCount);

    uint32 affordable = ai::CraftAffordableCasts(pSpellInfo, casts,
        [&](uint32 reagentId) { return ai->GetInventoryItemsCountWithId(reagentId); });

    AiObjectContext* context = ai->GetAiObjectContext();
    return ai::FirstFarmableReagentFor(pSpellInfo, casts, affordable,
        [&](uint32 reagentId) { return ai->GetInventoryItemsCountWithId(reagentId); },
        [&](uint32 reagentId)
        {
            auto* space = context->GetValue<uint32>("stack space for item", std::to_string(reagentId));
            return space ? space->Get() : 0;
        },
        [&](uint32 reagentId)
        {
            ItemPrototype const* reagentProto = sObjectMgr.GetItemPrototype(reagentId);
            return reagentProto && ItemUsageValue::IsItemSoldByAnyVendor(reagentProto) &&
                !ItemUsageValue::IsItemSoldByAnyVendorButHasLimitedMaxCount(reagentProto);
        },
        [&](uint32 reagentId)
        {
            return ai::PlanGuildOrderSources(bot, reagentId,
                TortoiseBots::GuildOrders::OrderType::Farm).verdict == TortoiseBots::GuildOrders::OrderReason::None;
        });
}

double ai::GuildOrderCraftHardestReagentKills(PlayerbotAI* ai, Player* bot, uint32 itemId, uint32 amount,
    uint32 delivered)
{
    if (!ai || !bot || !itemId || !amount)
        return -1.0;

    uint32 currentCount = ai->GetInventoryItemsCountWithId(itemId) + std::min(delivered, amount);
    if (currentCount >= amount)
        return -1.0;

    uint32 craftSpellId = ai::FindCraftSpellFor(bot, itemId);
    if (!craftSpellId)
        return -1.0;

    const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(craftSpellId);
    if (!pSpellInfo)
        return -1.0;

    uint32 casts = ai::CraftCastsForRemaining(craftSpellId, itemId, amount - currentCount);
    double hardest = -1.0;
    for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (!pSpellInfo->ReagentCount[i] || pSpellInfo->Reagent[i] <= 0)
            continue;
        uint32 reagentId = pSpellInfo->Reagent[i];
        uint32 need = pSpellInfo->ReagentCount[i] * casts;
        uint32 have = ai->GetInventoryItemsCountWithId(reagentId);
        if (have >= need)
            continue;
        ItemPrototype const* reagentProto = sObjectMgr.GetItemPrototype(reagentId);
        if (reagentProto && ItemUsageValue::IsItemSoldByAnyVendor(reagentProto) &&
            !ItemUsageValue::IsItemSoldByAnyVendorButHasLimitedMaxCount(reagentProto))
            continue;
        TortoiseBots::GuildOrders::SourcePlan plan =
            ai::PlanGuildOrderSources(bot, reagentId, TortoiseBots::GuildOrders::OrderType::Farm);
        if (plan.verdict != TortoiseBots::GuildOrders::OrderReason::None || plan.zones.empty())
            continue;
        hardest = std::max(hardest,
            TortoiseBots::GuildOrders::ExpectedKills(need - have, plan.bestChance));
    }
    return hardest;
}

uint32 GuildOrderFarmItemValue::Calculate()
{
    std::optional<TortoiseBots::GuildOrders::OrderRecord> record =
        TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
    if (!record || record->state != TortoiseBots::GuildOrders::OrderState::Working)
        return 0;
    RefreshGuildOrderSupply(ai, bot, *record);
    record = TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
    if (!record)
        return 0;
    if (record->supplyStep != TortoiseBots::GuildOrders::SupplyStep::Bank &&
        record->supplyStep != TortoiseBots::GuildOrders::SupplyStep::AuctionHouse &&
        record->supplyStep != TortoiseBots::GuildOrders::SupplyStep::Farm &&
        record->supplyStep != TortoiseBots::GuildOrders::SupplyStep::Bags)
        return 0;
    if (record->type == TortoiseBots::GuildOrders::OrderType::Craft &&
        IsUnlimitedVendorItem(record->supplyItemId) && record->reason != OrderReason::NoSource)
        return 0;
    return record->supplyItemId;
}

bool ai::OrderNeedsDelivery(Player* bot)
{
    if (!bot || !bot->GetGuildId())
        return false;
    std::optional<TortoiseBots::GuildOrders::OrderRecord> stored =
        TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
    if (!stored || stored->guildId != bot->GetGuildId())
        return false;
    if (stored->state != TortoiseBots::GuildOrders::OrderState::Delivering || !stored->itemId)
        return false;
    return bot->GetItemCount(stored->itemId) > 0;
}

bool HasGuildOrderDeliveryValue::Calculate()
{
    if (!OrderBotMayTravel(ai, bot))
        return false;
    return OrderNeedsDelivery(bot);
}

uint8 PetitionSignsValue::Calculate()
{
    if (bot->GetGuildId())
        return 0;

    std::list<Item*> petitions = AI_VALUE2(std::list<Item*>, "inventory items", chat->formatQItem(5863));

    if (petitions.empty())
        return 0;

    auto result = CharacterDatabase.PQuery("SELECT playerguid FROM petition_sign WHERE petitionguid = '%u'", petitions.front()->getObjectGuid().GetCounter());

    return result ? (uint8)result->GetRowCount() : 0;
};

bool CanBuyTabard::Calculate()
{
	if (!bot->GetGuildId())
		return false;

	if (AI_VALUE(TravelTarget*,"travel target")->GetStatus() == TravelStatus::TRAVEL_STATUS_TRAVEL)
		return false;

	bool inCity = false;
	AreaTableEntry const* areaEntry = GetAreaEntryByAreaID(sServerFacade.GetAreaId(bot));
	if (areaEntry)
	{
		if (areaEntry->ZoneId)
			areaEntry = GetAreaEntryByAreaID(areaEntry->ZoneId);

		if (areaEntry && areaEntry->Flags & AREA_FLAG_CAPITAL)
			inCity = true;
	}

	if (!inCity)
		return false;

	if (AI_VALUE2(uint32, "item count", chat->formatQItem(5976)))
		return false;

	if (AI_VALUE2(uint32, "free money for", uint32(NeedMoneyFor::guild)) < 10000)
		return false;

	return true;
}
