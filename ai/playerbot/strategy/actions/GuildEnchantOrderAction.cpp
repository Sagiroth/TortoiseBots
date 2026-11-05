#include "playerbot/playerbot.h"
#include "GuildEnchantOrderAction.h"
#include "TradeStatusAction.h"

#include "playerbot/GroupMembers.h"
#include "playerbot/strategy/values/GuildValues.h"
// (include style mirrors GuildValues.cpp: playerbot/ prefix for AI headers,
// runtime/ for store-adjacent ones).
#include "playerbot/policy/guild/EnchantTradePolicy.h"
#include "playerbot/GuildOrderEnchants.h"
#include "runtime/EnchantCapital.h"
#include "runtime/GuildOrderContract.h"
#include "runtime/GuildOrderEnchantLifecyclePolicy.h"
#include "runtime/GuildOrderEnchantPolicy.h"
#include "runtime/GuildOrderMeetPolicy.h"
#include "runtime/GuildOrderStore.h"

using namespace ai;
using namespace TortoiseBots::GuildOrders;

namespace
{
// True while the bags still hold one cast's reagents: the enchant was not paid yet.
bool HasAllEnchantReagents(Player* bot, uint32_t spellId)
{
    for (ai::GuildOrderEnchantMaterial const& reagent : ai::GuildOrderEnchantReagents(spellId))
        if (bot->GetItemCount(reagent.itemId) < reagent.count)
            return false;
    return true;
}
} // namespace

namespace
{
// Live reads for the policy header; the transitions stay in
// runtime/GuildOrderEnchantLifecyclePolicy.h. The tied-up rule is shared with
// the meet command (runtime/EnchantCapital.h) so the refusals cannot drift.

// Trade window facts for the stranger gate: a wrong-player window is
// cancelled with the reason whisper, whatever state the row is in.
bool EnchantShouldCancelStrangerTrade(Player* bot, OrderRecord const& order, Player* trader)
{
    if (!trader)
        return false;
    ObjectGuid giverGuid(HIGHGUID_PLAYER, order.giverGuidLow);
    return trader->getObjectGuid() != giverGuid;
}

EnchantLifecycleFacts EnchantFactsFor(Player* bot, OrderRecord const& order)
{
    EnchantLifecycleFacts facts;
    facts.liveEnchantOrder = true;
    bool needStillMissing = order.state == OrderState::Waiting && order.reason == OrderReason::NeedItems;
    facts.gathering = order.state == OrderState::Working || order.state == OrderState::Waiting;
    if (order.state == OrderState::Ready)
        facts.gathering = false;
    else if (order.state == OrderState::Waiting)
        facts.gathering = !needStillMissing;
    facts.botTiedUp = TortoiseBots::EnchantCapital::BotBusyForMeet(bot);
    facts.botInCapital = TortoiseBots::EnchantCapital::BotInCapital(bot);
    facts.teleportOk = TortoiseBots::EnchantCapital::WaitReason(bot) == TortoiseBots::EnchantCapital::TeleportWait::Ok;
    std::optional<MeetRequest> meet = PendingMeet(bot->GetGUIDLow());
    facts.meetPending = meet.has_value();
    if (meet)
    {
        Player* giver = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, meet->giverGuidLow));
        facts.giverOnline = giver && giver->IsInWorld();
        if (giver && giver->IsInWorld())
        {
            facts.giverInCapital = GiverInBotCapital(giver->GetZoneId(), bot->GetTeam() == ALLIANCE);
            facts.giverLeft = !facts.giverInCapital;
            facts.atTradeDistance = bot->GetDistance(giver) < 10.0f;
        }
        else
            facts.giverLeft = true;
    }
    Player* trader = bot->GetTrader();
    facts.tradeOpen = trader != nullptr;
    if (trader)
        facts.tradeOpenWithGiver = trader->getObjectGuid() == ObjectGuid(HIGHGUID_PLAYER, order.giverGuidLow);
    return facts;
}

bool EnchantRowNeedsMeetStates(OrderRecord const& order)
{
    return order.state == OrderState::Ready ||
        (order.state == OrderState::Waiting && order.reason == OrderReason::NeedItems) ||
        order.state == OrderState::Coming || order.state == OrderState::Enchanting;
}

} // namespace

bool GuildEnchantOrderAction::isUseful()
{
    if (!bot->GetGuildId() || bot->IsInCombat() || bot->IsDead())
        return false;
    if (!ai::OrderBotMayTravel(ai, bot))
        return false;
    std::optional<OrderRecord> order = Find(bot->GetGUIDLow());
    if (!order || order->guildId != bot->GetGuildId() || order->cancelled)
        return false;
    if (order->type != OrderType::Enchant || IsFinished(order->state))
        return false;
    // Working/Waiting rows gather on the ladder; the action only owns the
    // capital teleport while gathering and the Meet walk/trades after Ready.
    if (TortoiseBots::EnchantCapital::BotBusyForMeet(bot))
        return false;
    // Gathering only needs the action for the capital teleport.
    if (order->state == OrderState::Working ||
        (order->state == OrderState::Waiting && order->reason != OrderReason::NeedItems))
        return !TortoiseBots::EnchantCapital::BotInCapital(bot);
    return EnchantRowNeedsMeetStates(*order);
}

bool GuildEnchantOrderAction::Execute(Event& event)
{
    (void)event;
    std::optional<OrderRecord> stored = Find(bot->GetGUIDLow());
    if (!stored || stored->guildId != bot->GetGuildId() || stored->cancelled)
        return false;
    if (stored->type != OrderType::Enchant || IsFinished(stored->state))
        return false;
    OrderRecord order = *stored;

    // Gathering leg: out of capital and free goes through the inn teleport.
    // The retry flag is memory-only: a failed TeleportTo retries on a later
    // tick, never every tick.
    if (order.state == OrderState::Working ||
        (order.state == OrderState::Waiting && order.reason != OrderReason::NeedItems))
    {
        EnchantLifecycleFacts facts = EnchantFactsFor(bot, order);
        switch (DecideEnchantGatherTeleport(facts))
        {
            case EnchantLifecycleStep::Teleport:
                TortoiseBots::EnchantCapital::TeleportToCapitalInn(bot);
                return true;
            default:
                return false;
        }
    }
    if (!EnchantRowNeedsMeetStates(order))
        return false;

    EnchantLifecycleFacts facts = EnchantFactsFor(bot, order);

    // Whatever the row's meet state, a trade window with anyone but the giver
    // is refused at once: nothing in it is touched, the window is cancelled.
    if (Player* stranger = bot->GetTrader())
    {
        if (EnchantShouldCancelStrangerTrade(bot, order, stranger) &&
            EnchantTradeWithStranger(facts.tradeOpen, facts.tradeOpenWithGiver))
        {
            ai->TellPlayerNoFacing(stranger, kEnchantWrongPlayerText,
                PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            WorldPacket cancel;
            uint32 status = 0;
            cancel << status;
            sLog.outBasic("TBDEBUG enchant action cancel site 0 bot %s", bot->GetName()); bot->GetSession()->HandleCancelTradeOpcode(cancel);
            if (order.state == OrderState::Enchanting || order.state == OrderState::Coming)
            {
                SetState(bot->GetGUIDLow(), order.state == OrderState::Enchanting ?
                    OrderState::Ready : order.state, OrderReason::None);
                ClearMeet(bot->GetGUIDLow());
            }
            return true;
        }
    }

    // The trade completed on the giver's accept, not inside the bot's own:
    // the item the enchant was cast on tells whether it landed.
    if ((order.state == OrderState::Coming || order.state == OrderState::Enchanting) && !bot->GetTrader())
    {
        EnchantTradeSession session = GetEnchantTrade(bot->GetGUIDLow());
        Player* giver = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, order.giverGuidLow));
        Item* done = (giver && session.parkedItemGuidLow) ?
            giver->GetItemByGuid(ObjectGuid(HIGHGUID_ITEM, session.parkedItemGuidLow)) : nullptr;
        SpellEntry const* ordered = sServerFacade.LookupSpellInfo(order.itemId);
        uint32_t want = 0;
        if (ordered)
            for (uint8_t e = 0; e < MAX_EFFECT_INDEX; ++e)
                if (ordered->Effect[e] == SPELL_EFFECT_ENCHANT_ITEM)
                    want = uint32_t(ordered->EffectMiscValue[e]);
        if (done && EnchantDoneVerified(done->GetEnchantmentId(PERM_ENCHANTMENT_SLOT), want) &&
            !HasAllEnchantReagents(bot, order.itemId))
        {
            SetState(bot->GetGUIDLow(), OrderState::Done, OrderReason::None);
            NoteEnchantTradeClosed(bot->GetGUIDLow());
            NoteEnchantParked(bot->GetGUIDLow(), 0);
            ClearMeet(bot->GetGUIDLow());
            ai->TellPlayerNoFacing(giver, "Enchanted, thank you.",
                PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            return true;
        }
    }

    if (order.state == OrderState::Ready ||
        (order.state == OrderState::Waiting && order.reason == OrderReason::NeedItems))
    {
        switch (DecideEnchantMeet(facts))
        {
            case EnchantLifecycleStep::ToComing:
                if (!TortoiseBots::EnchantCapital::BotInCapital(bot) &&
                    TortoiseBots::EnchantCapital::WaitReason(bot) ==
                        TortoiseBots::EnchantCapital::TeleportWait::Ok)
                    TortoiseBots::EnchantCapital::TeleportToCapitalInn(bot);
                SetState(bot->GetGUIDLow(), OrderState::Coming, OrderReason::None);
                return true;
            default:
                return false;
        }
    }

    if (order.state == OrderState::Coming)
    {
        switch (DecideEnchantComing(facts))
        {
            case EnchantLifecycleStep::MeetExpired:
            {
                bool need = order.reason == OrderReason::NeedItems;
                SetState(bot->GetGUIDLow(), need ? OrderState::Waiting : OrderState::Ready,
                    need ? OrderReason::NeedItems : OrderReason::None);
                ClearMeetFailed(bot->GetGUIDLow());
                return true;
            }
            case EnchantLifecycleStep::OpenTrade:
            {
                std::optional<MeetRequest> meet = PendingMeet(bot->GetGUIDLow());
                Player* giver = meet ? sObjectAccessor.FindPlayer(
                    ObjectGuid(HIGHGUID_PLAYER, meet->giverGuidLow)) : nullptr;
                if (giver && giver->IsInWorld())
                {
                    WorldPacket packet(CMSG_INITIATE_TRADE, 8);
                    packet << giver->GetObjectGuid();
                    bot->GetSession()->HandleInitiateTradeOpcode(packet);
                    if (bot->GetTrader() == giver)
                    {
                        SetState(bot->GetGUIDLow(), OrderState::Enchanting, OrderReason::None);
                        return true;
                    }
                }
                return false;
            }
            case EnchantLifecycleStep::Walk:
            {
                std::optional<MeetRequest> meet = PendingMeet(bot->GetGUIDLow());
                Player* giver = meet ? sObjectAccessor.FindPlayer(
                    ObjectGuid(HIGHGUID_PLAYER, meet->giverGuidLow)) : nullptr;
                if (!giver || !giver->IsInWorld())
                    return false;
                if (bot->IsMoving())
                    return true;
                MoveTo(giver, 3.0f);
                return true;
            }
            default:
                return false;
        }
    }

    // An open window with the giver where nothing happens sends no trade
    // packets, so the try budget is checked here every tick too.
    if (order.state == OrderState::Enchanting && bot->GetTrader() && facts.tradeOpenWithGiver)
    {
        // An item put in the window sends no trade status either: run the
        // trade step (checks, cast, accept) from here as well.
        {
            TradeStatusAction step(ai);
            Event tick;
            step.HandleEnchantOrderTrade(tick, bot->GetTrader());
        }
        if (!bot->GetTrader())
            return true;
        Player* trader = bot->GetTrader();
        EnchantTradeSession session = GetEnchantTrade(bot->GetGUIDLow());
        if (session.openedMs)
        {
            bool const active = trader->GetTradeData() && trader->GetTradeData()->IsAccepted();
            TradeTryAction const next = CheckTradeTry(WorldTimer::getMSTime(), session.openedMs,
                session.tries ? session.tries : 1, active);
            if (next == TradeTryAction::KeepWaiting)
                return false;
            if (next == TradeTryAction::GiveUp)
                ai->TellPlayerNoFacing(trader, kTradeGiveUpText,
                    PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            WorldPacket cancel;
            uint32 status = 0;
            cancel << status;
            sLog.outBasic("TBDEBUG enchant action cancel site 1 bot %s", bot->GetName()); bot->GetSession()->HandleCancelTradeOpcode(cancel);
            if (next == TradeTryAction::GiveUp)
            {
                SetState(bot->GetGUIDLow(), OrderState::Ready, OrderReason::None);
                ClearMeetFailed(bot->GetGUIDLow());
            }
            return true;
        }
    }

    // Enchanting without a window: the trade closed (or never opened) without
    // completing. Back to Coming so the walk re-opens it; the try budget on
    // the trade-status path counts real windows only.
    if (order.state == OrderState::Enchanting && !bot->GetTrader())
    {
        SetState(bot->GetGUIDLow(), OrderState::Coming, OrderReason::None);
        return true;
    }

    return false;
}
