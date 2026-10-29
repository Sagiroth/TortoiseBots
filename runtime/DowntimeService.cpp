// pi-lens-ignore-file: clang:pp_file_not_found,clang:unknown_typename,clang:use_of_undeclared_identifier,clang:unknown_type_name,clang:undeclared_var_use,clang:incomplete_member_access
#include "DowntimeService.h"

#include "AltBotService.h"
#include "BotManager.h"
#include "ClaimLifecycle.h"
#include "HireLifecycle.h"

#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "PlayerbotAIStorage.h"
#include "../ai/playerbot/strategy/Event.h"
#include "../ai/playerbot/strategy/values/BudgetValues.h"
#include "../ai/playerbot/strategy/values/TravelValues.h"
#include "../ai/playerbot/strategy/actions/ChooseTravelTargetAction.h"
#include "../ai/playerbot/TravelMgr.h"

#include "Bag.h"
#include "Chat.h"
#include "DBCStructure.h"
#include "Item.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Timer.h"
#include "WorldSession.h"

using namespace ai;

namespace TortoiseBots
{

namespace
{

char const* kPrefix = "|cff00ff00[TortoiseBots]|r ";

void TellMaster(Player* master, std::string const& text)
{
    if (master && master->GetSession())
        ChatHandler(master).PSendSysMessage("%s %s", kPrefix, text.c_str());
}

char const* ErrandName(DowntimeErrand errand)
{
    switch (errand)
    {
        case DowntimeErrand::Trainer: return "train spells";
        case DowntimeErrand::Vendor:  return "sell junk";
        case DowntimeErrand::Repair:  return "repair gear";
        default:                      return "errand";
    }
}

// The partial-purse rule the pool's trainer trip uses: the cheapest green
// class rank must fit the free-money-for-spells budget.
bool TrainerErrandNeeded(PlayerbotAI* ai)
{
    AiObjectContext* context = ai->GetAiObjectContext();

    uint32 minCost = UINT32_MAX;
    for (TrainerSpell const* trainable :
        AI_VALUE2(std::vector<TrainerSpell const*>, "trainable spells", (uint32)TRAINER_TYPE_CLASS))
        if (trainable && trainable->spellCost < minCost)
            minCost = trainable->spellCost;

    if (minCost == UINT32_MAX)
        return false;

    return AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::spells) >= minCost;
}

// Vendor junk the default sell filter covers: bag greys a vendor pays for.
// The alt-safety item guard stays in the loop so a soulbound grey can never
// count.
bool VendorErrandNeeded(Player* bot)
{
    auto const hasJunk = [&](uint8 bag, uint8 slot) -> bool
    {
        Item* item = bot->GetItemByPos(bag, slot);
        ItemPrototype const* proto = item ? item->GetProto() : nullptr;
        if (!proto || proto->Quality != ITEM_QUALITY_POOR || !proto->SellPrice)
            return false;
        return AltBotMayVendorItem(proto->Quality, item->IsSoulBound(), false);
    };

    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (hasJunk(INVENTORY_SLOT_BAG_0, slot))
            return true;
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
        if (Bag* pBag = dynamic_cast<Bag*>(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag)))
            for (uint8 slot = 0; slot < pBag->GetBagSize(); ++slot)
                if (hasJunk(bag, slot))
                    return true;
    return false;
}

// Anything carried is worn down and the purse covers the repair bill.
bool RepairErrandNeeded(PlayerbotAI* ai)
{
    AiObjectContext* context = ai->GetAiObjectContext();

    if (AI_VALUE(uint8, "durability inventory") >= 100)
        return false;

    return AI_VALUE(uint32, "min repair cost") < AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::repair);
}

// The nearest reachable destination for one errand, searched exactly like the
// pool's request actions: same purpose, the same entry set for class trainers,
// the same possibility gate (the vendor search runs without it, like the pool's
// vendor request, because the destination's own pre-filter never matches one).
bool FindErrandDestination(Player* bot, PlayerbotAI* ai, DowntimeErrand errand, TravelDestination*& outDest,
    WorldPosition*& outPos)
{
    AiObjectContext* context = ai->GetAiObjectContext();

    uint32 purpose = 0;
    std::vector<int32> entries;
    bool onlyPossible = true;
    switch (errand)
    {
        case DowntimeErrand::Trainer:
        {
            purpose = (uint32)TravelDestinationPurpose::Trainer;
            entries = AI_VALUE2(std::vector<int32>, "available trainers", (uint32)TRAINER_TYPE_CLASS);
            if (entries.empty())
                return false;
            break;
        }
        case DowntimeErrand::Vendor:
            purpose = (uint32)TravelDestinationPurpose::Vendor;
            onlyPossible = false;
            break;
        case DowntimeErrand::Repair:
            purpose = (uint32)TravelDestinationPurpose::Repair;
            break;
        default:
            return false;
    }

    PlayerTravelInfo info(bot);
    PartitionedTravelList const list = sTravelMgr.GetPartitions(WorldPosition(bot), ai::travelPartitions, info,
        purpose, entries, onlyPossible);

    for (auto& [partition, points] : list)
        for (auto& [dest, pos, dist] : points)
        {
            if (!dest || !pos || !dest->IsActive(bot, info))
                continue;
            outDest = dest;
            outPos = pos;
            return true;
        }

    return false;
}

// Arms the next needed errand with a reachable destination. Returns the
// errand set, or None when every errand was tried; anyNeed marks whether a
// need existed at all, so "nothing to do" and "nothing reachable" stay
// distinguishable.
DowntimeErrand ArmNextErrand(Player* bot, PlayerbotAI* ai, std::vector<DowntimeErrand>& tried, bool& anyNeed)
{
    for (int guard = 0; guard < 3; ++guard)
    {
        bool const trainerNeeded = TrainerErrandNeeded(ai);
        bool const vendorNeeded = VendorErrandNeeded(bot);
        bool const repairNeeded = RepairErrandNeeded(ai);
        anyNeed = anyNeed || trainerNeeded || vendorNeeded || repairNeeded;

        DowntimeErrand errand = NextDowntimeErrand(tried, trainerNeeded, vendorNeeded, repairNeeded);
        if (errand == DowntimeErrand::None)
            return DowntimeErrand::None;

        TravelDestination* dest = nullptr;
        WorldPosition* pos = nullptr;
        if (!FindErrandDestination(bot, ai, errand, dest, pos))
        {
            tried.push_back(errand);
            continue;
        }

        // An earlier arrival retired the walker (the travel-once strategy
        // removes itself at the destination); re-arm it for this leg.
        if (!ai->HasStrategy("travel once", BotState::BOT_STATE_NON_COMBAT))
            ai->ChangeStrategy("+travel once", BotState::BOT_STATE_NON_COMBAT);

        AiObjectContext* context = ai->GetAiObjectContext();
        TravelTarget* target = AI_VALUE(TravelTarget*, "travel target");
        target->SetTarget(dest, pos);
        // The trip dies with the window: when the service is gone the
        // condition reads false and the machinery expires the target itself.
        target->SetConditions({"manual bool::downtime"});
        SET_AI_VALUE2(bool, "manual bool", "downtime", true);
        RESET_AI_VALUE(bool, "travel target active");
        RESET_AI_VALUE(bool, "travel target traveling");
        return errand;
    }
    return DowntimeErrand::None;
}

constexpr uint32_t kDowntimeTickMs = 2000;
constexpr uint32_t kDowntimeVisitMs = 90 * 1000;
constexpr uint32_t kDowntimeSellMs = 5000;

} // namespace

DowntimeService& DowntimeService::Instance()
{
    static DowntimeService instance;
    return instance;
}

bool DowntimeService::IsOnDowntime(uint32_t guidLow) const
{
    return m_windows.count(guidLow) != 0;
}

void DowntimeService::EnterErrandKit(Player* bot, Window& window)
{
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai)
        return;

    if (window.hadFollow)
        ai->ChangeStrategy("-follow", BotState::BOT_STATE_NON_COMBAT);
    if (window.hadWander)
        ai->ChangeStrategy("-wander", BotState::BOT_STATE_NON_COMBAT);
    ai->ChangeStrategy("+free", BotState::BOT_STATE_NON_COMBAT);
    ai->ChangeStrategy("+travel once", BotState::BOT_STATE_NON_COMBAT);
    // The base rpg strategy pulls in its children; the town errands live in
    // "rpg maintenance" (train, repair). Quest, vendor and explore children
    // stay off: an ordered errand is trainer, junk and repair only, never
    // autonomous questing, AH, mail or flight-master roaming.
    ai->ChangeStrategy("+rpg", BotState::BOT_STATE_NON_COMBAT);
    ai->ChangeStrategy("-rpg quest,-rpg vendor,-rpg explore", BotState::BOT_STATE_NON_COMBAT);
}

void DowntimeService::RestoreMovement(Player* bot, Window& window)
{
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai)
        return;

    AiObjectContext* context = ai->GetAiObjectContext();

    SET_AI_VALUE2(bool, "manual bool", "downtime", false);
    if (TravelTarget* target = AI_VALUE(TravelTarget*, "travel target"))
        sTravelMgr.SetNullTravelTarget(target);

    ai->ChangeStrategy("-rpg", BotState::BOT_STATE_NON_COMBAT);
    ai->ChangeStrategy("-rpg quest,-rpg vendor,-rpg explore,-rpg maintenance", BotState::BOT_STATE_NON_COMBAT);
    ai->ChangeStrategy("-travel once", BotState::BOT_STATE_NON_COMBAT);
    ai->ChangeStrategy("-free", BotState::BOT_STATE_NON_COMBAT);
    RESET_AI_VALUE(GuidPosition, "rpg target");

    if (window.hadFollow)
        ai->ChangeStrategy("+follow", BotState::BOT_STATE_NON_COMBAT);
    if (window.hadWander)
        ai->ChangeStrategy("+wander", BotState::BOT_STATE_NON_COMBAT);
}

bool DowntimeService::Start(Player* bot, Player* master, std::string& reply)
{
    PlayerbotAI* ai = bot ? PlayerbotAIStorage::Instance().GetAI(bot) : nullptr;
    if (!bot || !ai || !master)
    {
        reply = "has no AI yet.";
        return false;
    }

    uint32_t const guidLow = bot->GetGUIDLow();
    if (m_windows.count(guidLow))
    {
        reply = "is already out on its errands.";
        return true;
    }

    BotRecord* record = BotManager::Instance().FindBot(bot->GetObjectGuid());
    bool const hired = HireLifecycle::Instance().IsHired(bot->GetObjectGuid());
    bool const randomPool = !record || record->random;
    bool const claimed = (!randomPool && !hired) && ClaimLifecycle::Instance().IsClaimed(guidLow);
    bool const autonomous = AltBotService::Instance().IsAutonomous(bot->GetObjectGuid());
    bool const ownedAlt = record && !record->random && !hired && !claimed;

    switch (CheckDowntimeStart(bot->IsAlive(), ownedAlt, hired, randomPool, autonomous))
    {
        case DowntimeStartRefusal::Dead:
            reply = "is dead.";
            return false;
        case DowntimeStartRefusal::Autonomous:
            reply = "already plays on its own and runs its own errands.";
            return false;
        case DowntimeStartRefusal::NotOwnedAlt:
            reply = hired ? "is a hired companion, not one of your own alts."
                          : (randomPool ? "is a pool bot, not one of your own alts."
                                        : "is not one of your own alts.");
            return false;
        default:
            break;
    }

    // Probe the errands before touching the bot: an order that cannot start
    // changes nothing.
    Window window;
    window.masterGuid = master->GetObjectGuid();
    window.startedAt = time(nullptr);
    window.hadFollow = ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT);
    window.hadWander = ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT);

    bool anyNeed = false;
    DowntimeErrand first = ArmNextErrand(bot, ai, window.tried, anyNeed);
    if (first == DowntimeErrand::None)
    {
        reply = anyNeed ? "cannot reach a class trainer or vendor from here: nothing in range."
                        : "needs nothing: no affordable spell rank, no junk, no repairs.";
        return false;
    }
    window.current = first;

    EnterErrandKit(bot, window);
    m_windows[guidLow] = window;

    // Plain list of what the window will try (each leg re-probes reachability).
    std::string errands = ErrandName(first);
    std::vector<DowntimeErrand> preview = window.tried;
    for (DowntimeErrand next = NextDowntimeErrand(preview, TrainerErrandNeeded(ai), VendorErrandNeeded(bot),
             RepairErrandNeeded(ai));
         next != DowntimeErrand::None;
         next = NextDowntimeErrand(preview, TrainerErrandNeeded(ai), VendorErrandNeeded(bot),
             RepairErrandNeeded(ai)))
    {
        preview.push_back(next);
        errands += ", ";
        errands += ErrandName(next);
    }

    reply = "is off on its errands: " + errands + ". It comes back to you afterwards.";
    return true;
}

void DowntimeService::End(uint32_t guidLow, Window& window, char const* note)
{
    if (Player* bot = sObjectAccessor.FindPlayer(::ObjectGuid(HIGHGUID_PLAYER, guidLow)))
    {
        RestoreMovement(bot, window);
        if (Player* master = sObjectAccessor.FindPlayer(window.masterGuid))
            TellMaster(master, std::string(bot->GetName()) + " " + note + " and is on its way back to you.");
    }
}

void DowntimeService::Update()
{
    uint32_t const nowMs = WorldTimer::getMSTime();
    time_t const now = time(nullptr);

    for (auto it = m_windows.begin(); it != m_windows.end();)
    {
        uint32_t const guidLow = it->first;
        Window& window = it->second;

        Player* bot = sObjectAccessor.FindPlayer(::ObjectGuid(HIGHGUID_PLAYER, guidLow));
        PlayerbotAI* ai = bot ? PlayerbotAIStorage::Instance().GetAI(bot) : nullptr;
        if (!bot || !ai || !bot->IsInWorld())
        {
            // The bot is gone (logout, reclaim): there is nothing left to restore.
            it = m_windows.erase(it);
            continue;
        }

        if (nowMs - window.lastTickMs < kDowntimeTickMs)
        {
            ++it;
            continue;
        }
        window.lastTickMs = nowMs;

        Player* master = sObjectAccessor.FindPlayer(window.masterGuid);
        bool const masterGone = !master || !master->IsInWorld() || !master->GetSession() ||
            !master->GetSession()->HasNetworkTransport();
        bool const expired = DowntimeWindowExpired(window.startedAt, now);
        // The owner retargeted the bot (a follow order or a strategy change):
        // explicit orders beat the errand, so hand it back.
        bool const retaken = !ai->HasStrategy("free", BotState::BOT_STATE_NON_COMBAT) ||
            ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) ||
            ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT);
        if (masterGone || !bot->IsAlive() || bot->InBattleGround() || expired || retaken)
        {
            End(guidLow, window,
                expired ? "gave up on the rest of its errands"
                        : (retaken ? "broke off its errands" : "had to break off its errands"));
            it = m_windows.erase(it);
            continue;
        }
        if (bot->IsInCombat())
        {
            ++it;
            continue;
        }

        AiObjectContext* context = ai->GetAiObjectContext();
        TravelTarget* target = AI_VALUE(TravelTarget*, "travel target");

        if (window.current == DowntimeErrand::None)
        {
            bool anyNeed = false;
            DowntimeErrand next = ArmNextErrand(bot, ai, window.tried, anyNeed);
            if (next == DowntimeErrand::None)
            {
                End(guidLow, window, "finished its errands");
                it = m_windows.erase(it);
                continue;
            }
            window.current = next;
            ++it;
            continue;
        }

        target->CheckStatus();

        if (target->GetStatus() == TravelStatus::TRAVEL_STATUS_TRAVEL)
        {
            ++it;
            continue;
        }

        if (target->GetStatus() == TravelStatus::TRAVEL_STATUS_NONE)
        {
            // Arrived: the walker retired itself and cleared the target. The
            // visit runs on the rpg layer (trainer, repair); the service sells
            // at the vendor. Done when the need is gone or the visit times out.
            bool const trainerNeeded = TrainerErrandNeeded(ai);
            bool const vendorNeeded = VendorErrandNeeded(bot);
            bool const repairNeeded = RepairErrandNeeded(ai);

            if (!window.visitDeadlineMs)
                window.visitDeadlineMs = nowMs + kDowntimeVisitMs;

            bool const done = DowntimeErrandDone(window.current, trainerNeeded, vendorNeeded, repairNeeded);
            if (done || nowMs >= window.visitDeadlineMs)
            {
                window.visitDeadlineMs = 0;
                window.tried.push_back(window.current);
                window.current = DowntimeErrand::None;
                ++it;
                continue;
            }

            if (window.current == DowntimeErrand::Vendor && vendorNeeded &&
                nowMs - window.lastArrivalMs >= kDowntimeSellMs)
            {
                window.lastArrivalMs = nowMs;
                ai->DoSpecificAction("sell", Event("downtime", "", master), true);
            }
            ++it;
            continue;
        }

        // EXPIRED or COOLDOWN: the walk failed. Park this errand and try the next.
        window.visitDeadlineMs = 0;
        window.tried.push_back(window.current);
        window.current = DowntimeErrand::None;
        ++it;
    }
}

} // namespace TortoiseBots
