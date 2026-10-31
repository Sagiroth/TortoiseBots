
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
#include <cstdint>
#include <unordered_map>
#include <vector>

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
using TortoiseBots::GuildOrders::MailLegSkipped;
using TortoiseBots::GuildOrders::SupplyFarmItemFor;
using TortoiseBots::GuildOrders::SupplyMailItemId;

struct SupplyCandidate
{
    uint32_t itemId = 0;
    uint32_t count = 0;
    bool craft = false;
    bool takeMail = false;
};
// Latest supply refresh per order bot: at most one ladder run per tick, so
// the values that read above it in one tick share a single pass. Memory
// only (no behaviour change across restarts); entries prune themselves
// when a bot refreshes less often than the window.
inline constexpr uint32_t kSupplyRefreshCoalesceMs = 1500;
inline std::unordered_map<uint32_t, uint32_t>& SupplyRefreshMsByBot()
{
    static std::unordered_map<uint32_t, uint32_t> refreshedMs;
    return refreshedMs;
}
inline bool SupplyRefreshDue(uint32_t botGuidLow, uint32_t nowMs)
{
    auto& refreshed = SupplyRefreshMsByBot();
    auto found = refreshed.find(botGuidLow);
    if (found == refreshed.end())
        return true;
    uint32_t last = found->second;
    bool stillFresh = last <= nowMs ? nowMs - last < kSupplyRefreshCoalesceMs :
        last - nowMs > UINT32_MAX - kSupplyRefreshCoalesceMs;
    if (stillFresh)
        return false;
    refreshed.erase(found);
    return true;
}
inline void NoteSupplyRefreshed(uint32_t botGuidLow, uint32_t nowMs)
{
    SupplyRefreshMsByBot()[botGuidLow] = nowMs;
}
// One Refresh's view of the bot's bags, mailbox and recipes: bag counts and
// the mailbox are scanned once per ladder pass instead of once per level of
// the recipe walk. The mailbox scan walks every mail; the bag count walks
// the inventory slots; the spell walk below feeds both missing-reagent
// paths, so sharing one snapshot cuts the per-Refresh scans to one each.
struct SupplySnapshot
{
    std::unordered_map<uint32_t, uint32_t> bags;
    std::unordered_map<uint32_t, uint32_t> mail;
    std::unordered_map<uint32_t, uint32_t> craftSpell;
    bool mailTaken = false;
    bool spellsTaken = false;
    uint32_t nowMs = 0;
};

inline uint32_t SnapshotBags(PlayerbotAI* ai, SupplySnapshot& snapshot, uint32_t itemId)
{
    if (!itemId)
        return 0;
    auto found = snapshot.bags.find(itemId);
    if (found != snapshot.bags.end())
        return found->second;
    uint32_t count = ai->GetInventoryItemsCountWithId(itemId);
    snapshot.bags.emplace(itemId, count);
    return count;
}

// The whole mailbox in one walk: bought items arrive by mail, so the first
// mail question of the pass counts every waiting item and later ones read
// the map. Matches OrderMailboxItems per item (undelivered and deleted mail
// never counts).
inline void TakeMailboxSnapshot(Player* bot, SupplySnapshot& snapshot)
{
    snapshot.mailTaken = true;
    if (!bot || !bot->GetSession())
        return;
    MasterPlayer* master = bot->GetSession()->GetMasterPlayer();
    if (!master)
        return;
    time_t curTime = time(nullptr);
    for (PlayerMails::iterator itr = master->GetMailBegin(); itr != master->GetMailEnd(); ++itr)
    {
        Mail* mail = *itr;
        if (!mail || mail->state == MAIL_STATE_DELETED || curTime < mail->deliver_time)
            continue;
        if (!mail->has_items)
            continue;
        for (MailItemInfoVec::const_iterator itemItr = mail->items.begin(); itemItr != mail->items.end(); ++itemItr)
        {
            Item* mailItem = master->GetMItem(itemItr->item_guid);
            snapshot.mail[itemItr->item_template] += mailItem ? mailItem->GetCount() : 1;
        }
    }
}

inline uint32_t SnapshotMail(Player* bot, SupplySnapshot& snapshot, uint32_t itemId)
{
    if (!itemId)
        return 0;
    if (!snapshot.mailTaken)
        TakeMailboxSnapshot(bot, snapshot);
    auto found = snapshot.mail.find(itemId);
    return found == snapshot.mail.end() ? 0 : found->second;
}

inline uint32_t SnapshotCraftSpell(Player* bot, SupplySnapshot& snapshot, uint32_t itemId)
{
    if (!itemId)
        return 0;
    if (!snapshot.spellsTaken)
    {
        snapshot.spellsTaken = true;
        for (auto& [spellId, spellState] : bot->GetSpellMap())
        {
            if (spellState.state == PLAYERSPELL_REMOVED || spellState.disabled || IsPassiveSpell(spellId))
                continue;
            const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
            if (!pSpellInfo)
                continue;
            for (int i = 0; i < 3; ++i)
            {
                if (pSpellInfo->Effect[i] == SPELL_EFFECT_CREATE_ITEM && pSpellInfo->EffectItemType[i] &&
                    !snapshot.craftSpell.count(pSpellInfo->EffectItemType[i]))
                    snapshot.craftSpell.emplace(pSpellInfo->EffectItemType[i], spellId);
            }
        }
    }
    auto found = snapshot.craftSpell.find(itemId);
    return found == snapshot.craftSpell.end() ? 0 : found->second;
}

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
    std::vector<NeedItem>& out, SupplySnapshot* snapshot = nullptr)
{
    if (!ai || !bot || !itemId || !count)
        return;
    SupplySnapshot local;
    SupplySnapshot& snap = snapshot ? *snapshot : local;
    if (!snap.nowMs)
        snap.nowMs = WorldTimer::getMSTime();
    uint32_t spellId = SnapshotCraftSpell(bot, snap, itemId);
    if (!spellId || IsUnlimitedVendorItem(itemId))
    {
        uint32_t owned = SnapshotBags(ai, snap, itemId) + SnapshotMail(bot, snap, itemId);
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
        CollectMissingReagents(ai, bot, reagentId, spell->ReagentCount[i] * casts, out, &snap);
    }
}

bool FindSupplyCandidate(PlayerbotAI* ai, Player* bot, uint32_t itemId, uint32_t count,
    std::vector<uint32_t>& path, uint32_t depth, SupplyCandidate& out, OrderRecord const* order = nullptr,
    SupplySnapshot* snapshot = nullptr)
{
    if (!ai || !bot || !itemId || !count)
        return false;
    if (depth >= 8 || std::find(path.begin(), path.end(), itemId) != path.end())
    {
        out = { itemId, count, false };
        return true;
    }

    SupplySnapshot local;
    SupplySnapshot& snap = snapshot ? *snapshot : local;
    if (!snap.nowMs)
        snap.nowMs = WorldTimer::getMSTime();
    uint32_t spellId = SnapshotCraftSpell(bot, snap, itemId);
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
        uint32_t inBags = SnapshotBags(ai, snap, reagentId);
        // A mailbox no search could reach is not a source: the ladder treats
        // the reagent as bag-only and moves down instead of queueing mail.
        uint32_t inMail = SnapshotMail(bot, snap, reagentId);
        if (order && MailLegSkipped(reagentId, order->mailEmptyItemId, snap.nowMs, order->mailEmptyMs))
            inMail = 0;
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
        if (FindSupplyCandidate(ai, bot, reagentId, required - owned, path, depth + 1, nested, order, &snap) &&
            !nested.craft)
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

// Farmable verdict for one ladder pass, planned lazily: the ladder calls
// CanFarm only for steps that farm, so most same-item ticks never plan.
class FarmableVerdict
{
public:
    FarmableVerdict(Player* bot, OrderRecord const& order, SupplyCandidate const& candidate)
        : bot_(bot), order_(order), candidate_(candidate) {}
    bool CanFarm()
    {
        if (planned_)
            return canFarm_;
        planned_ = true;
        if (IsUnlimitedVendorItem(candidate_.itemId))
            canFarm_ = order_.reason != OrderReason::NoSource;
        // The farm leg gave up on this item (no zone of the plan yielded a
        // spot): the plan still lists zones, so the recorded verdict alone
        // stops a retry without planning again.
        else if (order_.supplyItemId == candidate_.itemId &&
            order_.supplyStep == SupplyStep::Farm && order_.reason == OrderReason::NoSource)
            canFarm_ = false;
        else
        {
            TortoiseBots::GuildOrders::SourcePlan farm =
                ai::PlanGuildOrderSources(bot_, candidate_.itemId, OrderType::Farm);
            canFarm_ = farm.verdict == OrderReason::None && !farm.zones.empty();
        }
        return canFarm_;
    }
private:
    Player* bot_ = nullptr;
    OrderRecord const& order_;
    SupplyCandidate const& candidate_;
    bool planned_ = false;
    bool canFarm_ = false;
};

SupplyStep ChooseSourceStep(Player* bot, OrderRecord const& order, SupplyCandidate const& candidate,
    SupplySnapshot* snapshot, FarmableVerdict* farmable)
{
    if (candidate.craft)
        return SupplyStep::Craft;
    SupplySnapshot local;
    SupplySnapshot& snap = snapshot ? *snapshot : local;
    if (!snap.nowMs)
        snap.nowMs = WorldTimer::getMSTime();
    // A banker visit that found none of this item skips the bank leg until
    bool bankSkipped = BankLegSkipped(candidate.itemId, order.bankEmptyItemId, snap.nowMs, order.bankEmptyMs);
    bool sameItem = order.supplyItemId == candidate.itemId;
    SupplyStep current = sameItem ? order.supplyStep : SupplyStep::None;
    FarmableVerdict localVerdict(bot, order, candidate);
    FarmableVerdict& verdict = farmable ? *farmable : localVerdict;
    if (sameItem)
    {
        // A delivered sub-order put the item in the guild bank: fetch it
        // there instead of asking for it again.
        if (current == SupplyStep::Sub && !bankSkipped && sPlayerbotAIConfig.guildOrderBankWithdraw &&
            TortoiseBots::GuildOrders::SubOrderDelivered(order.botGuidLow, candidate.itemId))
            return SupplyStep::Bank;
        if (current == SupplyStep::AuctionHouse || current == SupplyStep::Sub || current == SupplyStep::Need)
            return current;
        if (current == SupplyStep::Craft && candidate.craft)
            return current;
        if (current == SupplyStep::Bags && SnapshotMail(bot, snap, candidate.itemId) > 0)
            return current;
        if (current == SupplyStep::Bags && order.supplyReturnStep != SupplyStep::None)
            return order.supplyReturnStep;
        if (current == SupplyStep::Bank && sPlayerbotAIConfig.guildOrderBankWithdraw && !bankSkipped)
            return current;
        // A kept farm leg plans only when the ladder reaches it: steady-state
        // ticks for a working farm step never touch the planner.
        if (current == SupplyStep::Farm && verdict.CanFarm())
            return current;
        SupplyOptions options;
        options.bankEnabled = sPlayerbotAIConfig.guildOrderBankWithdraw && !bankSkipped;
        options.bankAttempted = bankSkipped || current == SupplyStep::AuctionHouse ||
            current == SupplyStep::Farm || current == SupplyStep::Craft ||
            current == SupplyStep::Sub || current == SupplyStep::Need;
        options.auctionHouseAttempted = current == SupplyStep::Farm || current == SupplyStep::Craft ||
            current == SupplyStep::Sub || current == SupplyStep::Need;
        options.selfCanCraft = candidate.craft;
        options.selfCanFarm = verdict.CanFarm();
        options.subAttempted = current == SupplyStep::Need;
        return TortoiseBots::GuildOrders::NextSupplyStep(options);
    }
    SupplyOptions options;
    options.bankEnabled = sPlayerbotAIConfig.guildOrderBankWithdraw && !bankSkipped;
    options.bankAttempted = bankSkipped;
    options.auctionHouseAttempted = false;
    options.selfCanCraft = candidate.craft;
    options.selfCanFarm = verdict.CanFarm();
    options.subAttempted = false;
    return TortoiseBots::GuildOrders::NextSupplyStep(options);
}

SupplyStep MailReturnStep(Player* bot, OrderRecord const& order, SupplyCandidate const& candidate,
    SupplySnapshot* snapshot, FarmableVerdict* farmable)
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
    return ChooseSourceStep(bot, order, candidate, snapshot, farmable);
}

void SetSupplyStage(PlayerbotAI* ai, Player* bot, OrderRecord const& order, SupplyCandidate const& candidate,
    SupplyStep step, SupplyStep returnStep = SupplyStep::None, SupplySnapshot* snapshot = nullptr)
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
        CollectMissingReagents(ai, bot, order.itemId, order.amount > order.delivered ? order.amount - order.delivered : 0, missing, snapshot);
        if (missing.empty())
            missing.push_back(NeedItem{ candidate.itemId, candidate.count });
        TortoiseBots::GuildOrders::SetNeedItems(order.botGuidLow,
            missing, order.reason == OrderReason::BankDenied);
        return;
    }
    if (step == SupplyStep::Need)
    {
        std::vector<NeedItem> missing;
        CollectMissingReagents(ai, bot, order.itemId, order.amount > order.delivered ? order.amount - order.delivered : 0, missing, snapshot);
        if (missing.empty())
            missing.push_back(NeedItem{ candidate.itemId, candidate.count });
        TortoiseBots::GuildOrders::SetNeedItems(order.botGuidLow,
            missing, order.reason == OrderReason::BankDenied);
        return;
    }
    (void)ai;
    (void)bot;
    (void)snapshot;
    TortoiseBots::GuildOrders::SetSupply(order.botGuidLow, step, candidate.itemId,
        candidate.count, returnStep, order.reason);
}

void RefreshGuildOrderSupply(PlayerbotAI* ai, Player* bot, OrderRecord const& order)
{
    if (!ai || !bot || order.state != TortoiseBots::GuildOrders::OrderState::Working)
        return;
    uint32_t nowMs = WorldTimer::getMSTime();
    if (!SupplyRefreshDue(order.botGuidLow, nowMs))
        return;
    NoteSupplyRefreshed(order.botGuidLow, nowMs);
    SupplySnapshot snapshot;
    snapshot.nowMs = nowMs;
    uint32_t carried = SnapshotBags(ai, snapshot, order.itemId);
    uint32_t mailed = SnapshotMail(bot, snapshot, order.itemId);
    if (order.type != OrderType::Craft)
    {
        uint32_t have = order.delivered + carried + mailed;
        uint32_t missing = have >= order.amount ? 0 : order.amount - have;
        if (mailed && order.delivered + carried < order.amount &&
            !MailLegSkipped(order.itemId, order.mailEmptyItemId, nowMs, order.mailEmptyMs))
        {
            SupplyCandidate candidate{ order.itemId, missing, false, true };
            FarmableVerdict mailVerdict(bot, order, candidate);
            SupplyStep afterMail = MailReturnStep(bot, order, candidate, &snapshot, &mailVerdict);
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
        if (mailed && !MailLegSkipped(order.itemId, order.mailEmptyItemId, nowMs, order.mailEmptyMs))
        {
            FarmableVerdict mailVerdict(bot, order, candidate);
            SupplyStep afterMail = MailReturnStep(bot, order, candidate, &snapshot, &mailVerdict);
            if (order.supplyStep != SupplyStep::Bags || order.supplyItemId != order.itemId)
                TortoiseBots::GuildOrders::SetSupply(order.botGuidLow, SupplyStep::Bags,
                    order.itemId, missing, afterMail, order.reason);
            return;
        }
        // Farm and buy orders never take their own order item from the bank:
        // the bank holds reagents, and withdrawing the finished item would
        // just deposit it back on delivery. Craft reagents below still use
        // the bank first.
        FarmableVerdict farmable(bot, order, candidate);
        SupplyStep step = ChooseSourceStep(bot, order, candidate, &snapshot, &farmable);
        if (step == SupplyStep::Bank)
            step = SupplyStep::AuctionHouse;
        SupplyStep fallback = SupplyStep::None;
        if (step == SupplyStep::AuctionHouse)
            fallback = TortoiseBots::GuildOrders::NextSupplyStep(
                SupplyOptions{ false, true, true, false, farmable.CanFarm(), false });
        SetSupplyStage(ai, bot, order, candidate, step, fallback, &snapshot);
        return;
    }

    uint32_t outputHave = order.delivered + carried + mailed;
    uint32_t outputMissing = outputHave >= order.amount ? 0 : order.amount - outputHave;
    if (mailed && order.delivered + carried < order.amount &&
        !MailLegSkipped(order.itemId, order.mailEmptyItemId, nowMs, order.mailEmptyMs))
    {
        SupplyCandidate candidate{ order.itemId, outputMissing, false, true };
        FarmableVerdict mailVerdict(bot, order, candidate);
        SupplyStep afterMail = MailReturnStep(bot, order, candidate, &snapshot, &mailVerdict);
        if (order.supplyStep != SupplyStep::Bags || order.supplyItemId != order.itemId)
            TortoiseBots::GuildOrders::SetSupply(order.botGuidLow, SupplyStep::Bags,
                order.itemId, outputMissing, afterMail, order.reason);
        return;
    }
    if (!outputMissing)
    {
        if (mailed && order.delivered + carried < order.amount &&
            !MailLegSkipped(order.itemId, order.mailEmptyItemId, nowMs, order.mailEmptyMs))
            TortoiseBots::GuildOrders::SetSupply(order.botGuidLow, SupplyStep::Bags,
                order.itemId, 0, SupplyStep::None, order.reason);
        else if (order.delivered + carried >= order.amount)
            TortoiseBots::GuildOrders::SetSupply(order.botGuidLow, SupplyStep::Deliver,
                order.itemId, 0, SupplyStep::None, order.reason);
        return;
    }

    SupplyCandidate candidate;
    std::vector<uint32_t> path;
    if (!FindSupplyCandidate(ai, bot, order.itemId, outputMissing, path, 0, candidate, &order, &snapshot))
    {
        TortoiseBots::GuildOrders::SetState(order.botGuidLow,
            TortoiseBots::GuildOrders::OrderState::Cant, OrderReason::NoRecipe);
        return;
    }
    if (candidate.craft)
    {
        SetSupplyStage(ai, bot, order, candidate, SupplyStep::Craft, SupplyStep::None, &snapshot);
        return;
    }
    if (SnapshotMail(bot, snapshot, candidate.itemId) > 0 &&
        !MailLegSkipped(candidate.itemId, order.mailEmptyItemId, nowMs, order.mailEmptyMs))
    {
        FarmableVerdict mailVerdict(bot, order, candidate);
        SupplyStep afterMail = MailReturnStep(bot, order, candidate, &snapshot, &mailVerdict);
        if (order.supplyStep != SupplyStep::Bags || order.supplyItemId != candidate.itemId)
            TortoiseBots::GuildOrders::SetSupply(order.botGuidLow, SupplyStep::Bags,
                candidate.itemId, candidate.count, afterMail, order.reason);
        return;
    }
    FarmableVerdict farmable(bot, order, candidate);
    SupplyStep step = ChooseSourceStep(bot, order, candidate, &snapshot, &farmable);
    SupplyOptions fallbackOptions;
    fallbackOptions.bankEnabled = false;
    fallbackOptions.bankAttempted = true;
    fallbackOptions.auctionHouseAttempted = false;
    fallbackOptions.selfCanCraft = candidate.craft;
    // The ladder already planned this item above: reuse the verdict instead
    // of planning it again for the auction fallback.
    fallbackOptions.selfCanFarm = farmable.CanFarm();
    SupplyStep fallback = TortoiseBots::GuildOrders::NextSupplyStep(fallbackOptions);
    SetSupplyStage(ai, bot, order, candidate, step, fallback, &snapshot);
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
    // The refreshed record already names the supply item, so this never
    // re-reads the farm-item value (and its second ladder refresh).
    if (order.IsCraftOrder())
        return SupplyFarmItemFor(current->supplyStep, current->supplyItemId, current->type, current->reason,
            current->supplyItemId != 0 && IsUnlimitedVendorItem(current->supplyItemId)) != 0;
    if (!order.IsTravelOrder())
        return false;

    // No order work while delivering or done with the amount (delivered
    // plus bags plus mailbox). Mail waiting never blocks the leg: the
    // travel code walks to a mailbox and collects it before farming, buying
    // or delivering, since bought items arrive by mail. One exception: bags
    // too full to lift the mail. The take gate refuses above 95, so the leg
    // would walk to a mailbox it cannot use; it yields to the vendor trip
    // instead and runs again after the sale.
    if (!OrderStillNeedsWork(bot, order))
        return false;
    if (TortoiseBots::GuildOrders::OrderMailYieldsToVendor(AI_VALUE(uint8, "bag space"),
        current->supplyStep == SupplyStep::Bags && ai::OrderMailboxItems(bot,
            TortoiseBots::GuildOrders::SupplyMailItemId(current->supplyStep,
                current->supplyItemId, current->itemId)) > 0) &&
        AI_VALUE(bool, "can sell"))
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
    return SupplyFarmItemFor(record->supplyStep, record->supplyItemId, record->type, record->reason,
        record->supplyItemId != 0 && IsUnlimitedVendorItem(record->supplyItemId));
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
