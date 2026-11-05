
#include "playerbot/playerbot.h"
#include "GuildValues.h"
#include "playerbot/strategy/values/BudgetValues.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/actions/GuildOrderBuyAction.h"
#include "playerbot/GuildOrderSources.h"
#include "playerbot/GuildOrderCraft.h"
#include "playerbot/policy/guild/EnchantGatherPolicy.h"
#include "runtime/EnchantCapital.h"
#include "runtime/GuildOrderStore.h"
#include "runtime/GuildOrderBringPolicy.h"
#include "runtime/GuildOrderNeedPolicy.h"
#include "runtime/GuildOrderSourcesPolicy.h"
#include "runtime/GuildOrderReagentTree.h"
#include "runtime/GuildOrderSubOrderPolicy.h"
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
using TortoiseBots::GuildOrders::ReagentTotals;
using TortoiseBots::GuildOrders::BankLegSkipped;
using TortoiseBots::GuildOrders::OrderRecord;
using TortoiseBots::GuildOrders::OrderReason;
using TortoiseBots::GuildOrders::OrderType;
using TortoiseBots::GuildOrders::SupplyOptions;
using TortoiseBots::GuildOrders::SupplyStep;
using TortoiseBots::GuildOrders::MailLegSkipped;
using TortoiseBots::GuildOrders::SupplyFarmItemFor;
using TortoiseBots::GuildOrders::SupplyMailItemId;
using TortoiseBots::GuildOrders::MaterialFold;
using TortoiseBots::GuildOrders::MaterialOutcome;
using TortoiseBots::GuildOrders::FoldMaterials;
using TortoiseBots::GuildOrders::NeedListHas;
using TortoiseBots::GuildOrders::PruneNeedItems;
using TortoiseBots::GuildOrders::NeedItemsEqual;
using TortoiseBots::GuildOrders::PartialNeedVerdict;
using TortoiseBots::GuildOrders::PartialNeedVerdictFor;

struct SupplyCandidate
{
    uint32_t itemId = 0;
    uint32_t count = 0;
    bool craft = false;
    bool takeMail = false;
    bool blocked = false; // the ladder stopped at an item only the player can bring
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

// Recipe view of the bot's known recipes for the whole-tree walk: the spell
// map snapshot names the craft spell, its create-item effect carries the
// guaranteed yield, and a vendor-unlimited item stays a leaf (bought, never
// made), exactly as the ladder treats it.
struct SnapshotRecipes
{
    Player* bot;
    SupplySnapshot* snap;
    OrderRecord const* order = nullptr;

    bool operator()(uint32_t itemId, TortoiseBots::GuildOrders::Recipe& out) const
    {
        // An enchant order's key is the enchant spell itself: its reagents
        // feed the ladder as a one-level recipe, so bags, bank, auction
        // house, the need list and bring= all work unchanged.
        if (order && order->type == OrderType::Enchant && itemId == order->itemId)
        {
            SpellEntry const* enchant = sServerFacade.LookupSpellInfo(itemId);
            if (!enchant || enchant->Effect[0] != SPELL_EFFECT_ENCHANT_ITEM)
                return false;
            out.yieldPerCast = 1;
            for (uint8_t i = 0; i < MAX_SPELL_REAGENTS; ++i)
            {
                if (!enchant->ReagentCount[i] || enchant->Reagent[i] <= 0)
                    continue;
                out.reagents.push_back({ uint32_t(enchant->Reagent[i]), uint32_t(enchant->ReagentCount[i]) });
            }
            return true;
        }
        uint32_t spellId = SnapshotCraftSpell(bot, *snap, itemId);
        if (!spellId || IsUnlimitedVendorItem(itemId))
            return false;
        SpellEntry const* spell = sServerFacade.LookupSpellInfo(spellId);
        if (!spell)
            return false;
        uint32_t effect = ai::FindCraftEffectFor(spellId, itemId);
        out.yieldPerCast = TortoiseBots::CraftOrderPolicy::MinYieldPerCast(spell->EffectBasePoints[effect],
            spell->EffectBaseDice[effect], spell->EffectDieSides[effect]);
        for (uint8_t i = 0; i < MAX_SPELL_REAGENTS; ++i)
        {
            if (!spell->ReagentCount[i] || spell->Reagent[i] <= 0)
                continue;
            out.reagents.push_back({ uint32_t(spell->Reagent[i]), uint32_t(spell->ReagentCount[i]) });
        }
        if (!sPlayerbotAIConfig.craftToolsAlways)
            for (uint32_t totem : spell->Totem)
                if (totem)
                    out.tools.push_back(totem);
        return true;
    }
};

// Whole-tree totals behind one snapshot: base reagents summed over every
// branch, each id once, with bags and mail spent once across the walk.
std::vector<NeedItem> MissingReagentsFromTree(PlayerbotAI* ai, Player* bot, SupplySnapshot& snap,
    uint32_t itemId, uint32_t count, OrderRecord const* order = nullptr)
{
    SnapshotRecipes recipes{ bot, &snap, order };
    auto held = [ai, bot, &snap](uint32_t heldId)
    {
        return SnapshotBags(ai, snap, heldId) + SnapshotMail(bot, snap, heldId);
    };
    std::vector<NeedItem> needs;
    for (TortoiseBots::GuildOrders::ReagentNeed const& need :
        TortoiseBots::GuildOrders::CollectBaseReagents(itemId, count, recipes, held))
        needs.push_back(NeedItem{ need.itemId, need.count });
    return needs;
}

// Whole-tree aggregates behind one snapshot: the three counts the ladder
// needs, one row per base reagent. A mailbox leg the ladder skips is dead
// stock here too, or the totals answer a need the ladder will not take mail
// for and the order parks asking for zero.
std::vector<ReagentTotals> ReagentTotalsFromTree(PlayerbotAI* ai, Player* bot, SupplySnapshot& snap,
    uint32_t itemId, uint32_t count, OrderRecord const* order)
{
    SnapshotRecipes recipes{ bot, &snap, order };
    auto bags = [ai, &snap](uint32_t bagsId)
    {
        return SnapshotBags(ai, snap, bagsId);
    };
    auto mail = [bot, &snap, order](uint32_t mailId)
    {
        bool skipped = order &&
            MailLegSkipped(mailId, order->mailEmptyItemId, snap.nowMs, order->mailEmptyMs);
        return TortoiseBots::GuildOrders::SpentMail(SnapshotMail(bot, snap, mailId), skipped);
    };
    return TortoiseBots::GuildOrders::CollectReagentTotals(itemId, count, recipes, bags, mail);
}

// Which whole-tree count a ladder step asks for. The mailbox leg fills in what
// the bags leave short, so it must not see the smaller external need; a bank,
// auction or farm leg sources only what neither bags nor mail hold.
enum class ReagentAggregate
{
    BagShort,
    ExtShort
};

// A leaf candidate asks for the whole order's need for that reagent, not the
// share of the branch the walk happened to reach first: a sub-order, a bank
// withdrawal or an auction buy then covers the tree instead of half of it.
// The branch's own count stays the floor: three walks over shared stock can
// leave the tree total below the branch it is standing on.
uint32_t TreeTotalFor(std::unordered_map<uint32_t, ReagentTotals> const& totals, uint32_t depth,
    uint32_t itemId, uint32_t fallback, ReagentAggregate aggregate)
{
    if (!depth)
        return fallback;
    auto found = totals.find(itemId);
    if (found == totals.end())
        return fallback;
    uint32_t treeCount = aggregate == ReagentAggregate::BagShort ? found->second.bagShort
                                                                 : found->second.extShort;
    return TortoiseBots::GuildOrders::WholeTreeCount(treeCount, fallback);
}

// Every reagent the bot still needs for `count` casts of the output, one
// entry per reagent: the need list and its whisper name them all at once
// instead of asking the player again after each delivery.
void CollectMissingReagents(PlayerbotAI* ai, Player* bot, uint32_t itemId, uint32_t count,
    std::vector<NeedItem>& out, SupplySnapshot* snapshot = nullptr, OrderRecord const* order = nullptr)
{
    if (!ai || !bot || !itemId || !count)
        return;
    SupplySnapshot local;
    SupplySnapshot& snap = snapshot ? *snapshot : local;
    if (!snap.nowMs)
        snap.nowMs = WorldTimer::getMSTime();
    // An enchant order's key is its spell: the reagents below are the whole
    // need, never a farm-or-buy stock check on the spell id.
    bool const enchantRoot = order && order->type == OrderType::Enchant && itemId == order->itemId;
    uint32_t spellId = enchantRoot ? itemId : SnapshotCraftSpell(bot, snap, itemId);
    // A farm or buy order's item has no recipe: bags and mailbox are the
    // whole stock, as before.
    if (!spellId || IsUnlimitedVendorItem(itemId))
    {
        uint32_t owned = SnapshotBags(ai, snap, itemId) + SnapshotMail(bot, snap, itemId);
        if (owned < count)
            out.push_back(NeedItem{ itemId, count - owned });
        return;
    }
    // The whole tree, not one level of siblings: a base reagent shared by
    // two intermediates, or sitting three recipes down, is one row with the
    // summed need, and an intermediate in the bags shrinks its subtree.
    for (NeedItem const& need : MissingReagentsFromTree(ai, bot, snap, itemId, count, order))
        out.push_back(need);
}

bool FindSupplyCandidate(PlayerbotAI* ai, Player* bot, uint32_t itemId, uint32_t count,
    std::vector<uint32_t>& path, uint32_t depth, SupplyCandidate& out, OrderRecord const* order = nullptr,
    SupplySnapshot* snapshot = nullptr, std::unordered_map<uint32_t, ReagentTotals> const* totals = nullptr)
{
    if (!ai || !bot || !itemId || !count)
        return false;
    SupplySnapshot local;
    SupplySnapshot& snap = snapshot ? *snapshot : local;
    if (!snap.nowMs)
        snap.nowMs = WorldTimer::getMSTime();
    // One whole-tree aggregate per ladder pass: every leaf candidate below
    // asks for the order's full need, not the first branch's share, and the
    // mailbox leg asks for the larger bag shortfall.
    std::unordered_map<uint32_t, ReagentTotals> treeTotals;
    if (!totals)
    {
        for (ReagentTotals const& row : ReagentTotalsFromTree(ai, bot, snap, itemId, count, order))
            treeTotals[row.itemId] = row;
        totals = &treeTotals;
    }
    if (depth >= TortoiseBots::GuildOrders::kReagentTreeMaxDepth ||
        std::find(path.begin(), path.end(), itemId) != path.end())
    {
        out = { itemId, TreeTotalFor(*totals, depth, itemId, count, ReagentAggregate::ExtShort), false };
        return true;
    }

    // An enchant order's key is its spell: expand its reagents like a
    // recipe's, one cast per ordered enchant. Anything else resolves through
    // the craft spellbook as before.
    bool const enchantRoot = order && order->type == OrderType::Enchant && itemId == order->itemId;
    uint32_t spellId = 0;
    SpellEntry const* spell = nullptr;
    if (enchantRoot)
    {
        spell = sServerFacade.LookupSpellInfo(itemId);
        if (!spell || spell->Effect[0] != SPELL_EFFECT_ENCHANT_ITEM)
        {
            out = { itemId, TreeTotalFor(*totals, depth, itemId, count, ReagentAggregate::ExtShort), false };
            return true;
        }
        spellId = itemId;
    }
    else
    {
        spellId = SnapshotCraftSpell(bot, snap, itemId);
        if (!spellId || IsUnlimitedVendorItem(itemId))
        {
            out = { itemId, TreeTotalFor(*totals, depth, itemId, count, ReagentAggregate::ExtShort), false };
            return true;
        }
        spell = sServerFacade.LookupSpellInfo(spellId);
        if (!spell)
        {
            out = { itemId, TreeTotalFor(*totals, depth, itemId, count, ReagentAggregate::ExtShort), false };
            return true;
        }
    }

    path.push_back(itemId);
    uint32_t casts = enchantRoot ? count : ai::CraftCastsForRemaining(spellId, itemId, count);
    std::vector<MaterialOutcome> outcomes;
    std::vector<SupplyCandidate> results;
    // Recipe tools are minted at the craft (CraftToolsAlways); with it off a
    // missing tool goes down the ladder like a reagent, one each.
    std::vector<NeedItem> materials;
    for (uint8_t i = 0; i < MAX_SPELL_REAGENTS; ++i)
        if (spell->ReagentCount[i] && spell->Reagent[i] > 0)
            materials.push_back(NeedItem{ uint32_t(spell->Reagent[i]), spell->ReagentCount[i] * casts });
    if (!sPlayerbotAIConfig.craftToolsAlways)
        for (uint32_t totem : spell->Totem)
            if (totem)
                materials.push_back(NeedItem{ totem, 1 });
    outcomes.reserve(materials.size());
    results.reserve(materials.size());
    for (NeedItem const& material : materials)
    {
        uint32_t reagentId = material.itemId;
        uint32_t required = material.count;
        uint32_t inBags = SnapshotBags(ai, snap, reagentId);
        // A mailbox no search could reach is not a source: the ladder treats
        // the reagent as bag-only and moves down instead of queueing mail.
        uint32_t inMail = SnapshotMail(bot, snap, reagentId);
        if (order && MailLegSkipped(reagentId, order->mailEmptyItemId, snap.nowMs, order->mailEmptyMs))
            inMail = 0;
        if (inMail && inBags < required)
        {
            path.pop_back();
            // The mailbox leg brings in what the bags do not cover; how much
            // it can move is bounded by the mail, not by its target.
            out = { reagentId,
                TreeTotalFor(*totals, depth + 1, reagentId, required - inBags, ReagentAggregate::BagShort),
                false, true };
            return true;
        }
        uint32_t owned = inBags + inMail;
        if (owned >= required)
        {
            outcomes.push_back(MaterialOutcome::Owned);
            results.push_back(SupplyCandidate{ reagentId, 0, false });
            continue;
        }
        SupplyCandidate nested;
        if (!FindSupplyCandidate(ai, bot, reagentId, required - owned, path, depth + 1, nested, order, &snap,
                totals))
        {
            outcomes.push_back(MaterialOutcome::Owned);
            results.push_back(SupplyCandidate{ reagentId, required - owned, false });
            continue;
        }
        results.push_back(nested);
        // An item the player already owes for this order is a dead end for
        // the ladder: it cannot be sourced again, and the walk must move on.
        if (nested.blocked ||
            (!nested.craft && order && (order->type == OrderType::Craft || order->type == OrderType::Enchant) &&
                NeedListHas(order->needItems, nested.itemId)))
            outcomes.push_back(MaterialOutcome::Blocked);
        else if (!nested.craft)
            outcomes.push_back(MaterialOutcome::Missing);
        else
            outcomes.push_back(MaterialOutcome::Craftable);
    }
    path.pop_back();
    MaterialFold fold = FoldMaterials(outcomes.data(), outcomes.size());
    if (fold.outcome == MaterialOutcome::Missing)
    {
        out = results[fold.index];
        return true;
    }
    // A component the bags can already make comes first: the item itself
    // cannot be cast until it exists (Copper Tube before the Blunderbuss).
    if (fold.outcome == MaterialOutcome::Craftable)
    {
        out = results[fold.index];
        return true;
    }
    if (fold.outcome == MaterialOutcome::Blocked)
    {
        out = { itemId, count, false, false, true };
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
        // Enchant reagents are never farmed: the vendor leg below still buys
        // a vendor-unlimited one, everything else runs bank, auction, need.
        if (order_.type == OrderType::Enchant)
        {
            canFarm_ = IsUnlimitedVendorItem(candidate_.itemId) &&
                order_.reason != OrderReason::NoSource;
            return canFarm_;
        }
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
    // A ticked material is the giver's to bring: the bot never sources it
    // itself, so the ladder names it for the need list straight away. The
    // bank leg stays open for it: the giver delivers by trade, mail or the
    // guild bank, and a banked delivery is collected like any other need
    // item's. Bags and mailbox still count through the normal need path
    // below (the whole-tree shortfall nets them).
    if ((order.type == OrderType::Craft || order.type == OrderType::Enchant) &&
        TortoiseBots::GuildOrders::BringTickedBlocksLeg(order.bringItems, candidate.itemId) &&
        !(order.supplyItemId == candidate.itemId && order.supplyStep == SupplyStep::Bank))
        return SupplyStep::Need;
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
    // No gold of its own and none it may take from the guild: the auction
    // house leg could only walk to a city and back.
    bool const auctionUnaffordable = bot->GetMoney() == 0 &&
        TortoiseBots::GuildOrders::BranchGoldCapRemaining(order.botGuidLow) == 0;
    // A reagent any vendor sells without limit is bought there, never
    // farmed and never delegated: the bank and auction legs never travel
    // for it (SupplyFarmItemFor maps it to 0), so routing it through them
    // parked the order for good. The farm verdict stays out of it: it
    // rates wild sources, while the vendor leg buys from a vendor.
    if (IsUnlimitedVendorItem(candidate.itemId))
    {
        // The vendor leg itself gave up (no vendor in reach, or the buy
        // never moved): the player gets the need list, never a sub-order.
        if (sameItem && current == SupplyStep::Farm && order.reason == OrderReason::NoSource)
            return SupplyStep::Need;
        return SupplyStep::Farm;
    }
    if (sameItem)
    {
        // A delivered sub-order put the item in the guild bank: fetch it
        // there instead of asking for it again.
        if (current == SupplyStep::Sub && !bankSkipped && sPlayerbotAIConfig.guildOrderBankWithdraw &&
            TortoiseBots::GuildOrders::SubOrderDelivered(order.botGuidLow, candidate.itemId))
            return SupplyStep::Bank;
        // The choice below never parks on a Sub row for another item: the
        // candidate here is already the next uncovered one, so a parked row
        // for a delegated component yields to supplying (or delegating) this
        // one.
        if (current == SupplyStep::Sub)
            current = SupplyStep::None;
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
        // A reagent whose counter was already searched does not bounce back
        // to it: the ladder must reach its need stage first, and then the
        // cycle allows exactly one retry. The live auction leg itself is
        // never marked attempted, so it keeps its turn while it runs.
        bool const auctionReentry = TortoiseBots::GuildOrders::AuctionReentryAllowed(
            order.botGuidLow, candidate.itemId);
        options.auctionHouseAttempted = auctionUnaffordable ||
            (!auctionReentry && current != SupplyStep::AuctionHouse);
        options.selfCanCraft = candidate.craft;
        options.selfCanFarm = verdict.CanFarm();
        // Enchant reagents are never delegated: the auction leg hands off to
        // the need list instead of a sub-order.
        options.subAttempted = current == SupplyStep::Need || order.type == OrderType::Enchant;
        return TortoiseBots::GuildOrders::NextSupplyStep(options);
    }
    SupplyOptions options;
    options.bankEnabled = sPlayerbotAIConfig.guildOrderBankWithdraw && !bankSkipped;
    options.bankAttempted = bankSkipped;
    options.auctionHouseAttempted = auctionUnaffordable ||
        !TortoiseBots::GuildOrders::AuctionReentryAllowed(order.botGuidLow, candidate.itemId);
    options.selfCanCraft = candidate.craft;
    options.selfCanFarm = verdict.CanFarm();
    options.subAttempted = order.type == OrderType::Enchant;
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

// One craft need entry: the whole-tree shortage, not the count of the ladder
// branch that named this reagent (siblings sharing a base reagent each carry
// their own shortfall), so the player is asked once for the total.
void AddCraftNeedItem(PlayerbotAI* ai, Player* bot, OrderRecord const& order, SupplyCandidate const& candidate,
    SupplySnapshot* snapshot)
{
    // The walk counts the output the bot still owes, the same shortage the
    // prune uses: delivered plus what is already in bags or mail, not the raw
    // order amount less delivered (which double-counts what is already held).
    SupplySnapshot local;
    SupplySnapshot& snap = snapshot ? *snapshot : local;
    uint32_t outputHave = order.delivered + SnapshotBags(ai, snap, order.itemId) +
        SnapshotMail(bot, snap, order.itemId);
    uint32_t outputMissing = outputHave >= order.amount ? 0 : order.amount - outputHave;
    uint32_t count = candidate.count;
    std::vector<NeedItem> missing;
    CollectMissingReagents(ai, bot, order.itemId, outputMissing, missing, &snap, &order);
    for (NeedItem const& need : missing)
        if (need.itemId == candidate.itemId && need.count > count)
            count = need.count;
    TortoiseBots::GuildOrders::AddNeedItem(order.botGuidLow, candidate.itemId, count,
        order.reason == OrderReason::BankDenied);
}

void SetSupplyStage(PlayerbotAI* ai, Player* bot, OrderRecord const& order, SupplyCandidate const& candidate,
    SupplyStep step, SupplyStep returnStep = SupplyStep::None, SupplySnapshot* snapshot = nullptr)
{
    // A ticked material never spawns a sub-order: it joins the need list
    // even when the ladder otherwise delegates. Belt and braces next to the
    // ChooseSourceStep routing above.
    if (step == SupplyStep::Sub && order.type == OrderType::Craft &&
        TortoiseBots::GuildOrders::BringTickedBlocksLeg(order.bringItems, candidate.itemId))
        step = SupplyStep::Need;
    // Enchant reagents are never delegated or crafted: a leg that still
    // names one joins the need list instead. The farm step stays: a
    // vendor-unlimited reagent is bought there, never farmed, as for craft.
    if (order.type == OrderType::Enchant && (step == SupplyStep::Sub || step == SupplyStep::Craft))
        step = SupplyStep::Need;
    if (step == SupplyStep::Sub)
    {
        if (TortoiseBots::GuildOrders::RequestSupply(order.botGuidLow, candidate.itemId, candidate.count))
        {
            TortoiseBots::GuildOrders::SetSupply(order.botGuidLow, SupplyStep::Sub,
                candidate.itemId, candidate.count, SupplyStep::None, order.reason);
            return;
        }
        // A craft order has more components to try: the item the sub-order
        // could not cover joins the need list and the ladder moves on
        // instead of parking the whole order.
        if (order.type == OrderType::Craft || order.type == OrderType::Enchant)
        {
            AddCraftNeedItem(ai, bot, order, candidate, snapshot);
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
        if (order.type == OrderType::Craft || order.type == OrderType::Enchant)
        {
            AddCraftNeedItem(ai, bot, order, candidate, snapshot);
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
    (void)ai;
    (void)bot;
    (void)snapshot;
    TortoiseBots::GuildOrders::SetSupply(order.botGuidLow, step, candidate.itemId,
        candidate.count, returnStep, order.reason);
}
// Enchant ready edge: every reagent for one cast sits in the bags. Moves a
// working row to ready (with the one whisper), moves a ready row that lost
// a reagent back to working, and parks an unknown spell as norecipe. True
// when it moved the row and the caller has nothing left to do.
bool RefreshEnchantOrderGathering(PlayerbotAI* ai, Player* bot, OrderRecord const& order)
{
    using TortoiseBots::GuildOrders::OrderState;
    SpellEntry const* spell = sServerFacade.LookupSpellInfo(order.itemId);
    if (!spell || spell->Effect[0] != SPELL_EFFECT_ENCHANT_ITEM)
    {
        if (order.state == OrderState::Working)
            TortoiseBots::GuildOrders::SetState(order.botGuidLow, OrderState::Cant, OrderReason::NoRecipe);
        return true;
    }
    std::vector<TortoiseBots::GuildOrders::EnchantMaterialState> materials;
    for (uint8_t i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (!spell->ReagentCount[i] || spell->Reagent[i] <= 0)
            continue;
        uint32_t reagentId = uint32_t(spell->Reagent[i]);
        materials.push_back(TortoiseBots::GuildOrders::EnchantMaterialState{ reagentId,
            uint32_t(spell->ReagentCount[i]), ai->GetInventoryItemsCountWithId(reagentId),
            TortoiseBots::GuildOrders::BringTickedBlocksLeg(order.bringItems, reagentId) });
    }
    bool complete = TortoiseBots::GuildOrders::EnchantMaterialsComplete(materials.data(), materials.size());
    if (complete && order.state == OrderState::Working)
    {
        TortoiseBots::GuildOrders::SetState(order.botGuidLow, OrderState::Ready, OrderReason::None);
        TortoiseBots::GuildOrders::SetSupply(order.botGuidLow, SupplyStep::None, 0, 0);
        Player* giver = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, order.giverGuidLow));
        if (giver && giver->IsInWorld())
        {
            std::string capital = bot->GetTeam() == ALLIANCE ? "Stormwind" : "Orgrimmar";
            std::string enchant = spell->SpellName[0].c_str();
            ai->TellPlayerNoFacing(giver, "I have the materials for " + enchant + ". Come to " + capital +
                " and press Meet.", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true, false);
        }
        return true;
    }
    if (!complete && order.state == OrderState::Ready)
    {
        TortoiseBots::GuildOrders::SetState(order.botGuidLow, OrderState::Working, OrderReason::None);
        TortoiseBots::GuildOrders::SetSupply(order.botGuidLow, SupplyStep::None, 0, 0);
        return true;
    }
    return false;
}


void RefreshGuildOrderSupply(PlayerbotAI* ai, Player* bot, OrderRecord const& order)
{
    if (!ai || !bot ||
        (order.state != TortoiseBots::GuildOrders::OrderState::Working &&
            !(order.type == OrderType::Enchant && order.state == TortoiseBots::GuildOrders::OrderState::Ready)))
        return;
    // The set turns over with the order or supply item, on this map thread
    // (the spell map is safe to read here). Runs at most once per tick: the
    // store no-ops when the keys already match.
    TortoiseBots::GuildOrders::RefreshOrderProtectedSet(bot);
    // Enchant gathering lives here, not in the craft branch below: the spell
    // id is not an item, so the output math there never applies. A complete
    // set of reagents turns the row ready (with its whisper); a Ready row
    // that lost a reagent goes back to working; anything else waits for the
    // capital before the ladder names bank or auction legs.
    if (order.type == OrderType::Enchant)
    {
        if (RefreshEnchantOrderGathering(ai, bot, order))
            return;
        if (order.state != TortoiseBots::GuildOrders::OrderState::Working ||
            !TortoiseBots::EnchantCapital::BotInCapital(bot))
            return;
    }
    uint32_t nowMs = WorldTimer::getMSTime();
    // A leg that cannot reach an auctioneer hands off after two minutes in
    // the city, or after twenty minutes of walking. The reach clock starts on
    // arrival, not on entering the leg, and both clocks clear on an
    // auctioneer the bot can actually interact with.
    if (order.supplyStep == SupplyStep::AuctionHouse)
    {
        if (ai::GuildOrderAuctioneerInReach(ai, bot))
            TortoiseBots::GuildOrders::NoteAuctioneerInReach(order.botGuidLow);
        else if (ai::GuildOrderAuctioneerWithin(ai, bot, ai::kGuildOrderAuctioneerCityYd))
            TortoiseBots::GuildOrders::NoteAuctionZoneArrival(order.botGuidLow, nowMs);
        else if (TortoiseBots::GuildOrders::AuctionLegUnreachable(order.botGuidLow, nowMs))
        {
            AdvanceGuildOrderAfterAuction(ai, bot, order, OrderReason::NoAuctioneer);
            return;
        }
    }
    if (!SupplyRefreshDue(order.botGuidLow, nowMs))
        return;
    NoteSupplyRefreshed(order.botGuidLow, nowMs);
    SupplySnapshot snapshot;
    snapshot.nowMs = nowMs;
    // A stale auction row with nothing left missing never walks back to the
    // counter with a zero count: hand back to the ladder through the empty
    // step, and the pass below routes to delivery or the next source.
    if (order.supplyStep == SupplyStep::AuctionHouse && order.supplyItemId &&
        order.supplyCount == 0)
    {
        TortoiseBots::GuildOrders::SetSupply(order.botGuidLow, SupplyStep::None, 0, 0);
        return;
    }
    uint32_t carried = SnapshotBags(ai, snapshot, order.itemId);
    uint32_t mailed = SnapshotMail(bot, snapshot, order.itemId);
    // The bought oversized stack is in the bags: the auction leg runs again,
    // this time only to relist the surplus. Buying more waits until it is
    // posted (or the relist gives up and the delivery leftover path takes it).
    uint32_t relistItemId = 0, relistCount = 0, relistUnitPrice = 0;
    if (TortoiseBots::GuildOrders::PendingRelist(order.botGuidLow, relistItemId, relistCount,
            relistUnitPrice) && relistItemId && relistCount)
    {
        if (SnapshotBags(ai, snapshot, relistItemId) >= relistCount)
        {
            if (order.supplyStep != SupplyStep::AuctionHouse || order.supplyItemId != relistItemId ||
                order.supplyCount != relistCount)
                TortoiseBots::GuildOrders::SetSupply(order.botGuidLow, SupplyStep::AuctionHouse,
                    relistItemId, relistCount, SupplyStep::None, order.reason);
            return;
        }
        // The bought stack is still in the mailbox: collect it first, even
        // on a craft order whose tree already counts the mail as owned.
        if (OrderMailboxItems(bot, relistItemId) > 0)
        {
            if (order.supplyStep != SupplyStep::Bags || order.supplyItemId != relistItemId)
                TortoiseBots::GuildOrders::SetSupply(order.botGuidLow, SupplyStep::Bags,
                    relistItemId, relistCount, SupplyStep::AuctionHouse, order.reason);
            return;
        }
    }
    if (order.type != OrderType::Craft)
    {
        // Enchant rows skip the farm/buy stock path: their key is the spell
        // id, and the craft ladder below sources its reagents instead.
        if (order.type != OrderType::Enchant)
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
            // A farm or buy order never walks its own item to the bank; that
            // leg is the auction one. A counter that was already searched for
            // this item falls through to the rest of the ladder instead.
            if (step == SupplyStep::Bank)
                step = TortoiseBots::GuildOrders::AuctionReentryAllowed(order.botGuidLow, candidate.itemId) ?
                    SupplyStep::AuctionHouse :
                    TortoiseBots::GuildOrders::NextSupplyStep(
                        SupplyOptions{ false, true, true, false, farmable.CanFarm(), false });
            SupplyStep fallback = SupplyStep::None;
            if (step == SupplyStep::AuctionHouse)
                fallback = TortoiseBots::GuildOrders::NextSupplyStep(
                    SupplyOptions{ false, true, true, false, farmable.CanFarm(), false });
            SetSupplyStage(ai, bot, order, candidate, step, fallback, &snapshot);
            return;
        }
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

    // The player's items are the ones the whole-tree walk still names: a
    // traded, mailed or otherwise gained item leaves the list, and one the
    // walk still names carries its live shortage. Everything the ladder does
    // below works off this list, not the one the row was loaded with.
    OrderRecord current = order;
    if (!current.needItems.empty())
    {
        std::vector<NeedItem> stillMissing;
        CollectMissingReagents(ai, bot, order.itemId, outputMissing, stillMissing, &snapshot, &current);
        std::vector<NeedItem> pruned = current.needItems;
        PruneNeedItems(pruned, stillMissing);
        if (!NeedItemsEqual(pruned, current.needItems))
        {
            // The store only takes the prune while its list still equals the
            // copy the prune started from: a merge or a clear in between wins
            // and the write does not land. Then the working copy is stale, so
            // re-read the live list and let the next refresh re-prune it.
            if (TortoiseBots::GuildOrders::ReplaceNeedItems(order.botGuidLow, current.needItems, pruned))
                current.needItems = pruned;
            else if (std::optional<OrderRecord> live = TortoiseBots::GuildOrders::Find(order.botGuidLow))
                current.needItems = live->needItems;
        }
    }

    // The first missing component the recipe walk names, then the first one
    // still needing work: delivered sub-orders are collected first (their
    // items sit in the guild bank), running or queued ones are covered (a
    // sibling sub-order fetches each), so the parent supplies or delegates
    // the rest instead of re-requesting the same item. Two cheap store
    // reads per refresh while a sub-order could be running; the pure pick
    // itself plans nothing.
    SupplyCandidate firstMissing;
    std::vector<uint32_t> path;
    if (!FindSupplyCandidate(ai, bot, order.itemId, outputMissing, path, 0, firstMissing, &current, &snapshot))
    {
        TortoiseBots::GuildOrders::SetState(order.botGuidLow,
            TortoiseBots::GuildOrders::OrderState::Cant, OrderReason::NoRecipe);
        return;
    }
    if (firstMissing.blocked)
    {
        // Nothing supplyable is left and the item the walk stopped at is the
        // player's: wait on the sub-orders still running (a running sub-order
        // is not a player item), else park on the list itself.
        std::vector<uint32_t> covered = TortoiseBots::GuildOrders::CoveredSubOrderItems(order.botGuidLow);
        if (PartialNeedVerdictFor(false, !covered.empty(), !current.needItems.empty()) ==
            PartialNeedVerdict::Park)
            TortoiseBots::GuildOrders::SetNeedItems(order.botGuidLow, current.needItems,
                order.reason == OrderReason::BankDenied);
        return;
    }
    SupplyCandidate candidate = firstMissing;
    if (!firstMissing.craft)
    {
        // Only while a need item or a sub-order is in play: two cheap store
        // reads, no planner. The reagent walk below repeats the walk above,
        // so it runs only when there is something to skip or collect.
        std::vector<uint32_t> collectable =
            TortoiseBots::GuildOrders::CollectableSubOrderItems(order.botGuidLow);
        std::vector<uint32_t> covered =
            TortoiseBots::GuildOrders::CoveredSubOrderItems(order.botGuidLow);
        if (!collectable.empty() || !covered.empty() || !current.needItems.empty())
        {
            std::vector<NeedItem> missingNow;
            CollectMissingReagents(ai, bot, order.itemId, outputMissing, missingNow, &snapshot, &current);
            if (!missingNow.empty())
            {
                std::vector<uint32_t> missingIds;
                missingIds.reserve(missingNow.size());
                for (NeedItem const& need : missingNow)
                    missingIds.push_back(need.itemId);
                // Delivered siblings first: a finished sub-order's items sit
                // in the guild bank, so the parent fetches those through the
                // bank leg before supplying or delegating the rest.
                bool collected = false;
                if (!collectable.empty())
                    for (NeedItem const& need : missingNow)
                    {
                        for (uint32_t ready : collectable)
                            if (ready && ready == need.itemId)
                            {
                                candidate = { need.itemId, need.count, false };
                                collected = true;
                                break;
                            }
                        if (collected)
                            break;
                    }
                if (!collected)
                {
                    // A need item is skipped like a covered one: the player
                    // brings it, the ladder works around it.
                    std::vector<uint32_t> skip = covered;
                    for (NeedItem const& need : current.needItems)
                        if (need.itemId)
                            skip.push_back(need.itemId);
                    size_t next = TortoiseBots::GuildOrders::NextUncoveredSubOrderItem(missingIds.data(),
                        missingIds.size(), skip.data(), skip.size());
                    // Every component covered or owed by the player: keep the
                    // row and wait for the deliveries instead of farming a
                    // delegated item twice, else park on the player's items.
                    // The collectable path above fetches each one as it lands.
                    if (next >= missingNow.size())
                    {
                        if (PartialNeedVerdictFor(false, !covered.empty(), !current.needItems.empty()) ==
                            PartialNeedVerdict::Park)
                            TortoiseBots::GuildOrders::SetNeedItems(order.botGuidLow, current.needItems,
                                order.reason == OrderReason::BankDenied);
                        return;
                    }
                    candidate = { missingNow[next].itemId, missingNow[next].count, false };
                }
            }
        }
    }
    if (candidate.craft)
    {
        SetSupplyStage(ai, bot, current, candidate, SupplyStep::Craft, SupplyStep::None, &snapshot);
        return;
    }
    if (SnapshotMail(bot, snapshot, candidate.itemId) > 0 &&
        !MailLegSkipped(candidate.itemId, order.mailEmptyItemId, nowMs, order.mailEmptyMs))
    {
        FarmableVerdict mailVerdict(bot, current, candidate);
        SupplyStep afterMail = MailReturnStep(bot, current, candidate, &snapshot, &mailVerdict);
        if (order.supplyStep != SupplyStep::Bags || order.supplyItemId != candidate.itemId)
            TortoiseBots::GuildOrders::SetSupply(order.botGuidLow, SupplyStep::Bags,
                candidate.itemId, candidate.count, afterMail, order.reason);
        return;
    }
    FarmableVerdict farmable(bot, current, candidate);
    SupplyStep step = ChooseSourceStep(bot, current, candidate, &snapshot, &farmable);
    SupplyOptions fallbackOptions;
    fallbackOptions.bankEnabled = false;
    fallbackOptions.bankAttempted = true;
    fallbackOptions.auctionHouseAttempted = false;
    fallbackOptions.selfCanCraft = candidate.craft;
    // The ladder already planned this item above: reuse the verdict instead
    // of planning it again for the auction fallback.
    fallbackOptions.selfCanFarm = farmable.CanFarm();
    SupplyStep fallback = TortoiseBots::GuildOrders::NextSupplyStep(fallbackOptions);
    SetSupplyStage(ai, bot, current, candidate, step, fallback, &snapshot);
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
        // Enchant rows travel and source through the store record, not the
        // AI struct: no farm item, no craft cast, so the type stays None.
        case TortoiseBots::GuildOrders::OrderType::Enchant:
            break;
    }
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
    // The refreshed record already names the supply item, so the check below
    // never re-reads the farm-item value (and its second ladder refresh).
    // The targeted take fits only the order item, so a full bag never parks
    // this leg at the vendor: nothing sellable still lifts the order item.
    // A craft order short on a farmable reagent travels like a farm order.
    // Enchant reagents travel their bank, auction and mail legs the same
    // way; a vendor-unlimited one rides the vendor trip instead. Outside
    // the capital no bank or auction leg starts: the lifecycle teleports
    // the bot first.
    if (order.IsCraftOrder() || current->type == OrderType::Enchant)
    {
        if (current->type == OrderType::Enchant &&
            (current->supplyStep == SupplyStep::Bank || current->supplyStep == SupplyStep::AuctionHouse) &&
            !TortoiseBots::EnchantCapital::BotInCapital(bot))
            return false;
        return SupplyFarmItemFor(current->supplyStep, current->supplyItemId, current->type, current->reason,
            current->supplyItemId != 0 && IsUnlimitedVendorItem(current->supplyItemId)) != 0;
    }
    if (!order.IsTravelOrder())
        return false;
    // No order work while delivering or done with the amount (delivered
    // plus bags plus mailbox). Mail waiting never blocks the leg: the
    // travel code walks to a mailbox and collects it before farming, buying
    // or delivering, since bought items arrive by mail. The targeted take
    // fits only the order item, so a full bag never parks this leg either.
    bool const mailLeg = current->supplyStep == SupplyStep::Bags && ai::OrderMailboxItems(bot,
        TortoiseBots::GuildOrders::SupplyMailItemId(current->supplyStep,
            current->supplyItemId, current->itemId)) > 0;
    // The mailbox counts toward the amount, but delivery needs the items in
    // the bags: a farm or buy order whose last items wait in the mail still
    // walks to the mailbox (live: a sub-order sat 90 min on 4/2 in the mail).
    if (!mailLeg && !OrderStillNeedsWork(bot, order))
        return false;
    return true;
}

uint32 GuildOrderVendorReagentValue::Calculate()
{
    GuildOrder order = AI_VALUE(GuildOrder, "guild order");
    std::optional<TortoiseBots::GuildOrders::OrderRecord> vendorRecord =
        TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
    // Craft reagents and farm-order items alike: anything a vendor sells
    // without limit is bought, never farmed. Enchant reagents share the
    // craft leg through the store record.
    if (!order.IsCraftOrder() && order.type != GuildOrderType::Farm &&
        (!vendorRecord || vendorRecord->type != OrderType::Enchant))
        return 0;

    std::optional<TortoiseBots::GuildOrders::OrderRecord> record = vendorRecord;
    if (record && record->state != TortoiseBots::GuildOrders::OrderState::Working)
        return 0;
    if (!record)
        return 0;
    RefreshGuildOrderSupply(ai, bot, *record);
    record = TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
    if (!record || record->supplyStep != SupplyStep::Farm || !record->supplyItemId ||
        record->reason == OrderReason::NoSource || !IsUnlimitedVendorItem(record->supplyItemId))
        return 0;
    // A farm order buys its own item; a craft order buys the supply reagent.
    if (order.type == GuildOrderType::Farm && record->supplyItemId != record->itemId)
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

bool ai::EnchantOrderMaterialsReady(Player* bot, TortoiseBots::GuildOrders::OrderRecord const& order)
{
    using TortoiseBots::GuildOrders::OrderType;
    if (!bot || order.type != OrderType::Enchant || !order.itemId)
        return false;
    SpellEntry const* spell = sServerFacade.LookupSpellInfo(order.itemId);
    if (!spell || spell->Effect[0] != SPELL_EFFECT_ENCHANT_ITEM)
        return false;
    // Bags only, one cast: the lifecycle calls this after the teleport, and
    // the ladder's ready edge counts the same way.
    for (uint8_t i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (!spell->ReagentCount[i] || spell->Reagent[i] <= 0)
            continue;
        if (bot->GetItemCount(uint32_t(spell->Reagent[i])) < uint32_t(spell->ReagentCount[i]))
            return false;
    }
    return true;
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
