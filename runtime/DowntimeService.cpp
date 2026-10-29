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

#include <sstream>
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

// The trainer park key the pool consults before walking to a class trainer.
char const* kTrainerClassParkKey = "no travel purpose until::trainer class";

// One shared vendorable-junk rule for the need probe and the sale: a bag-only
// grey that is neither soulbound nor equipped. A soulbound or equipped piece
// is never counted and never sold, so probe and sale cannot disagree.
bool IsDowntimeJunk(Item* item, Player* bot)
{
    ItemPrototype const* proto = item ? item->GetProto() : nullptr;
    if (!proto || proto->Quality != ITEM_QUALITY_POOR || !proto->SellPrice)
        return false;
    if (!item || item->IsSoulBound())
        return false;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot) == item)
            return false;
    return true;
}

// Vendor junk the default sell filter covers. The alt-safety item guard stays
// in the loop so a soulbound grey can never count.
bool VendorErrandNeeded(Player* bot)
{
    auto const hasJunk = [&](uint8 bag, uint8 slot) -> bool
    {
        return IsDowntimeJunk(bot->GetItemByPos(bag, slot), bot);
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

// The pool's trainer need (NeedTravelPurposeValue "trainer class"): nothing to
// train, a parked trainer need, or a purse that cannot cover the cheapest
// green rank all refuse before the destination walk.
bool TrainerErrandNeeded(PlayerbotAI* ai)
{
    AiObjectContext* context = ai->GetAiObjectContext();

    if (AI_VALUE2(uint32, "train cost", (uint32)TRAINER_TYPE_CLASS) == 0)
        return false;

    std::vector<int32> entries =
        AI_VALUE2(std::vector<int32>, "available trainers", (uint32)TRAINER_TYPE_CLASS);
    if (entries.empty())
        return false;

    // Respect the fruitless-visit park the trainer arrival writes: a park set
    // because the purse was short lifts the moment it covers the cheapest
    // rank, a teaching park holds until a level-up clears it. Same timestamps
    // the pool checks, so the window never re-walks a refused trainer.
    std::string const parkKey = kTrainerClassParkKey;
    if (AI_VALUE2(time_t, "manual time", parkKey) > time(0))
    {
        bool const moneyPark = AI_VALUE2(bool, "manual bool", "trainer park needs money");
        uint32 minCost = UINT32_MAX;
        for (TrainerSpell const* trainable :
            AI_VALUE2(std::vector<TrainerSpell const*>, "trainable spells", (uint32)TRAINER_TYPE_CLASS))
            if (trainable && trainable->spellCost < minCost)
                minCost = trainable->spellCost;
        if (minCost == UINT32_MAX)
            return false;
        bool const canAfford =
            AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::spells) >= minCost;
        if (!canAfford || !moneyPark)
            return false;
        RESET_AI_VALUE2(time_t, "manual time", parkKey);
    }

    uint32 minCost = UINT32_MAX;
    for (TrainerSpell const* trainable :
        AI_VALUE2(std::vector<TrainerSpell const*>, "trainable spells", (uint32)TRAINER_TYPE_CLASS))
        if (trainable && trainable->spellCost < minCost)
            minCost = trainable->spellCost;

    if (minCost == UINT32_MAX)
        return false;

    return AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::spells) >= minCost;
}

// Sells bag junk matching the shared predicate through the vendor at hand,
// one item per visit tick. The pool path's per-item hooks (the sale row, the
// random cap, the claimed/bot filters) do not run for an owned bot, so this
// stays a plain guarded sale: the predicate above is the soulbound/equipped
// veto, and bound or worn stock is never offered to the vendor at all.
void SellDowntimeJunk(PlayerbotAI* ai, Player* bot, Player* master)
{
    AiObjectContext* context = ai->GetAiObjectContext();

    std::list<ObjectGuid> vendors = AI_VALUE(std::list<ObjectGuid>, "nearest npcs");
    for (std::list<ObjectGuid>::iterator i = vendors.begin(); i != vendors.end(); ++i)
    {
        ObjectGuid vendorguid = *i;
        Creature* vendor = bot->GetNPCIfCanInteractWith(vendorguid, UNIT_NPC_FLAG_VENDOR);
        if (!vendor || !SellAction::HasVendorStock(vendor))
            continue;

        Item* junk = nullptr;
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; !junk && slot < INVENTORY_SLOT_ITEM_END; ++slot)
        {
            Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (IsDowntimeJunk(item, bot))
                junk = item;
        }
        for (uint8 bag = INVENTORY_SLOT_BAG_START; !junk && bag < INVENTORY_SLOT_BAG_END; ++bag)
        {
            Bag* pBag = dynamic_cast<Bag*>(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag));
            if (!pBag)
                continue;
            for (uint8 slot = 0; slot < pBag->GetBagSize(); ++slot)
            {
                Item* item = bot->GetItemByPos(bag, slot);
                if (IsDowntimeJunk(item, bot))
                {
                    junk = item;
                    break;
                }
            }
        }
        if (!junk || !junk->GetProto() || !junk->GetProto()->SellPrice)
            return;

        WorldPacket p;
        p << vendorguid << junk->getObjectGuid() << junk->GetCount();
        bot->GetSession()->HandleSellItemOpcode(p);
        sPlayerbotAIConfig.logEvent(ai, "SellAction", junk->GetProto()->Name1,
            std::to_string(junk->GetProto()->ItemId));
        if (master && master->GetSession())
        {
            std::ostringstream out;
            out << "Selling " << ai->GetChatHelper()->formatItem(junk);
            ai->TellPlayer(master, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        }
        return;
    }
}

// The pool's repair need: worn gear plus the free-money-for-repair purse.
// Asking the pool values instead of re-scanning keeps the window in step with
// the pool rule it borrows.
bool RepairErrandNeeded(PlayerbotAI* ai)
{
    AiObjectContext* context = ai->GetAiObjectContext();

    return AI_VALUE(bool, "should repair") && AI_VALUE(bool, "can repair");
}

// The pool's route verdict, same call the pick runs: waypoints plus the legs
// between them, hostile-town cells, area levels, cross-continent transfers.
// Declared on ChooseTravelTargetAction so both paths share one five-minute
// verdict cache.

// The nearest pool-picked destination for one errand: same purpose, the same
// class-trainer entry set, the same possibility gate the pool's vendor request
// skips, and then the same per-point verdict SetBestTarget applies - hostile
// town and area-level gates, the route check, same-map only.
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
            if (pos->GetMapId() != bot->GetMapId())
                continue;
            if (pos->IsEnemyHomeZoneFor(bot->GetTeam()))
                continue;
            if (sPlayerbotAIConfig.avoidHostileTowns &&
                pos->IsGuardedHostileTownFor(bot, 60.0f, false))
                continue;
            if (!TravelMgr::IsLocationLevelValid(*pos, info, purpose))
                continue;
            std::string blocker;
            if (!ai::SharedRouteIsSurvivable(bot, pos, blocker))
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

DowntimeEngineKit SnapshotEngineKit(PlayerbotAI* ai, BotState state)
{
    DowntimeEngineKit kit;
    kit.follow = ai->HasStrategy("follow", state);
    kit.wander = ai->HasStrategy("wander", state);
    kit.stay = ai->HasStrategy("stay", state);
    kit.guard = ai->HasStrategy("guard", state);
    kit.free = ai->HasStrategy("free", state);
    kit.travelOnce = ai->HasStrategy("travel once", state);
    kit.rpgMaintenance = ai->HasStrategy("rpg maintenance", state);
    return kit;
}

bool EngineKitRetaken(PlayerbotAI* ai, DowntimeEngineKit const& before, BotState state)
{
    return ai->HasStrategy("follow", state) != before.follow ||
        ai->HasStrategy("wander", state) != before.wander ||
        ai->HasStrategy("stay", state) != before.stay ||
        ai->HasStrategy("guard", state) != before.guard ||
        ai->HasStrategy("free", state) != before.free;
}

DowntimeStrategySnapshot DowntimeService::SnapshotStrategies(PlayerbotAI* ai)
{
    DowntimeStrategySnapshot snapshot;
    snapshot.reaction = SnapshotEngineKit(ai, BotState::BOT_STATE_REACTION);
    snapshot.nonCombat = SnapshotEngineKit(ai, BotState::BOT_STATE_NON_COMBAT);
    snapshot.combat = SnapshotEngineKit(ai, BotState::BOT_STATE_COMBAT);
    return snapshot;
}

bool DowntimeService::MovementModeRetaken(PlayerbotAI* ai, DowntimeStrategySnapshot const& before)
{
    // Any owner movement-mode change ends the window: follow/wander/stay/guard
    // deltas mean an explicit order landed, and a changed free bit means the
    // owner (or another command path) touched the movement mode too. All three
    // movement engines are watched: the setter writes them together.
    return EngineKitRetaken(ai, before.reaction, BotState::BOT_STATE_REACTION) ||
        EngineKitRetaken(ai, before.nonCombat, BotState::BOT_STATE_NON_COMBAT) ||
        EngineKitRetaken(ai, before.combat, BotState::BOT_STATE_COMBAT);
}

void ParkEngineKit(PlayerbotAI* ai, DowntimeEngineKit const& before, BotState state)
{
    if (before.follow)
        ai->ChangeStrategy("-follow", state);
    if (before.wander)
        ai->ChangeStrategy("-wander", state);
    if (before.stay)
        ai->ChangeStrategy("-stay", state);
    if (before.guard)
        ai->ChangeStrategy("-guard", state);
    if (!before.free)
        ai->ChangeStrategy("+free", state);
    if (!before.travelOnce)
        ai->ChangeStrategy("+travel once", state);
    if (!before.rpgMaintenance)
        ai->ChangeStrategy("+rpg maintenance", state);
}

void RestoreEngineKit(PlayerbotAI* ai, DowntimeEngineKit const& before, BotState state)
{
    if (!before.rpgMaintenance)
        ai->ChangeStrategy("-rpg maintenance", state);
    if (!before.travelOnce)
        ai->ChangeStrategy("-travel once", state);
    if (!before.free)
        ai->ChangeStrategy("-free", state);
    if (before.follow)
        ai->ChangeStrategy("+follow", state);
    if (before.wander)
        ai->ChangeStrategy("+wander", state);
    if (before.stay)
        ai->ChangeStrategy("+stay", state);
    if (before.guard)
        ai->ChangeStrategy("+guard", state);
}

void DowntimeService::EnterErrandKit(Player* bot, Window& window)
{
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai)
        return;

    // The town errands live in "rpg maintenance" (train, repair) without the
    // base rpg layer, so the "service nearby npc" trigger and its quest verbs
    // never arm: trainer, junk and repair only, never autonomous questing.
    ParkEngineKit(ai, window.before.reaction, BotState::BOT_STATE_REACTION);
    ParkEngineKit(ai, window.before.nonCombat, BotState::BOT_STATE_NON_COMBAT);
    ParkEngineKit(ai, window.before.combat, BotState::BOT_STATE_COMBAT);
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

    RESET_AI_VALUE(GuidPosition, "rpg target");

    RestoreEngineKit(ai, window.before.reaction, BotState::BOT_STATE_REACTION);
    RestoreEngineKit(ai, window.before.nonCombat, BotState::BOT_STATE_NON_COMBAT);
    RestoreEngineKit(ai, window.before.combat, BotState::BOT_STATE_COMBAT);
}

DowntimeStartVerdict DowntimeService::Start(Player* bot, Player* master, std::string& reply)
{
    PlayerbotAI* ai = bot ? PlayerbotAIStorage::Instance().GetAI(bot) : nullptr;
    if (!bot || !ai || !master)
    {
        reply = "has no AI yet.";
        return DowntimeStartVerdict::Refused;
    }

    uint32_t const guidLow = bot->GetGUIDLow();
    if (m_windows.count(guidLow))
    {
        reply = "is already out on its errands.";
        return DowntimeStartVerdict::AlreadyOut;
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
            return DowntimeStartVerdict::Refused;
        case DowntimeStartRefusal::Autonomous:
            reply = "already plays on its own and runs its own errands.";
            return DowntimeStartVerdict::Refused;
        case DowntimeStartRefusal::NotOwnedAlt:
            reply = hired ? "is a hired companion, not one of your own alts."
                          : (randomPool ? "is a pool bot, not one of your own alts."
                                        : "is not one of your own alts.");
            return DowntimeStartVerdict::Refused;
        default:
            break;
    }

    // Probe the errands before touching the bot: an order that cannot start
    // changes nothing.
    Window window;
    window.masterGuid = master->GetObjectGuid();
    window.startedAt = time(nullptr);
    window.before = SnapshotStrategies(ai);

    bool anyNeed = false;
    DowntimeErrand first = ArmNextErrand(bot, ai, window.tried, anyNeed);
    if (first == DowntimeErrand::None)
    {
        // ArmNextErrand re-arms the walker for the leg it picked; a refusal
        // must leave the bot untouched.
        if (TravelTarget* target = AI_VALUE(TravelTarget*, "travel target"))
            sTravelMgr.SetNullTravelTarget(target);
        if (!window.before.travelOnce && ai->HasStrategy("travel once", BotState::BOT_STATE_NON_COMBAT))
            ai->ChangeStrategy("-travel once", BotState::BOT_STATE_NON_COMBAT);
        reply = anyNeed ? "cannot reach a class trainer or vendor from here: nothing in range."
                        : "needs nothing: no affordable spell rank, no junk, no repairs.";
        return DowntimeStartVerdict::Refused;
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
    return DowntimeStartVerdict::Ok;
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

void DowntimeService::OnBotLogout(Player* bot)
{
    if (!bot)
        return;
    DowntimeService& service = DowntimeService::Instance();
    auto it = service.m_windows.find(bot->GetGUIDLow());
    if (it == service.m_windows.end())
        return;
    service.End(it->first, it->second, "had to break off its errands");
    service.m_windows.erase(it);
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
        if (!ai)
        {
            // The AI is gone (logout, reclaim): there is nothing left to restore.
            it = m_windows.erase(it);
            continue;
        }
        if (!bot || !bot->IsInWorld())
        {
            // Transiently out of world (teleport, map transfer): restore the
            // kit while the AI still exists instead of leaking the window's
            // strategies across it.
            End(guidLow, window, "had to break off its errands");
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
        // An explicit owner movement order ends the window immediately.
        bool const retaken = MovementModeRetaken(ai, window.before);
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
                SellDowntimeJunk(ai, bot, master);
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
