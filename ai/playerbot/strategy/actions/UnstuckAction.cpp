#include <map>

#include "playerbot/playerbot.h"
#include "UnstuckAction.h"
#include "playerbot/LongStuckRescuePolicy.h"
#include "playerbot/TravelRepickPolicy.h"
#include "playerbot/CombatStuckPolicy.h"
#include "playerbot/TravelMgr.h"

using namespace ai;

// A random bot's homebind is usually still its racial starting inn: hearthing a level 40
// bot "unstuck" parked it in Elwynn for good (2042 hearths in 4.7 h on a live realm).
// Hearth only while home is somewhere the bot has a reason to go: an area it has not
// outgrown, and far enough away that the teleport actually moves it.
static bool HearthLeadsSomewhereUseful(PlayerbotAI* ai, Player* bot)
{
    if (ai->HasRealPlayerMaster())
        return true;

    // A level 1 bot's whole starting zone is a few hundred yards wide, so hearthing
    // "unstuck" landed it next to where it already stood - and the 10-minute
    // "move long stuck" timer then called another one (478 hearths/100 min, 95% of them
    // within 300 yd of the bot's own homebind, 2026-09-29). Too close to be a rescue:
    // the unstuck chain repops to the nearest graveyard instead, which costs no
    // hearthstone cooldown. 0 disables the gate.
    if (sPlayerbotAIConfig.unstuckHearthMinDistance > 0.0f)
    {
        WorldPosition const home = bot->GetHomeBindLocation();
        if (home.isValid() && WorldPosition(bot).fDist(home) < sPlayerbotAIConfig.unstuckHearthMinDistance)
            return false;
    }

    if (!sPlayerbotAIConfig.unstuckHearthLevelFit)
        return true;

    int32 homeLevel = 0;
    if (!sTravelMgr.TryGetValidatedAreaLevel(bot->GetHomeBindAreaId(), homeLevel))
        return false;

    return homeLevel + 10 >= (int32)bot->GetLevel();
}

// The long-stuck rescue: hearth when its homebind leads somewhere useful,
// otherwise repop - which relocates the bot and nulls its travel target, so a
// fresh destination can be picked. The hearthstone action returns false
// whenever the cast does not start (blocked item/spell state) and the old
// `return DoSpecificAction("hearthstone")` ended the rescue there. The
// "hearth attempt anchor" that gates the next trip is only recorded once a cast
// starts, so the same non-cast was retried every five seconds forever and the
// repop fallback was unreachable. The same failure hides one level down: the
// repop action runs through the engine gates (MovementAction::isPossible, i.e.
// PlayerbotAI::CanMove), which fails on exactly the bots that need the rescue
// most - rooted, stunned, feared or otherwise immobile - so the action returns
// IMPOSSIBLE/USELESS without moving and LongStuckRescue would fire again
// forever on the same coordinate. Live 2026-10-02 pool: 74 of the 90 bots
// that logged "move long stuck" produced no UseHearthStoneAction and no
// RepopAction row (Crurcar at POINT(-14401.71 6560.12) fired 5 trips over 2 h
// without moving). The last resort below teleports the bot directly instead:
// a teleport needs no movement precondition, so the bot always ends up
// somewhere else - the nearest graveyard, or its homebind when it is already
// standing next to its graveyard.
static bool LongStuckFallbackTeleport(PlayerbotAI* ai, Player* bot, Player* master)
{
    AiObjectContext* context = ai->GetAiObjectContext();

    // Battleground bots and bots that belong to a real player (owned alts and
    // hires, even while the player is offline) are their player's business:
    // never yank them.
    if (bot->InBattleGround() || ai->HasRealPlayerMaster())
        return false;

    WorldPosition const botPos(bot);
    WorldPosition home = bot->GetHomeBindLocation();
    WorldSafeLocsEntry const* grave = sObjectMgr.GetClosestGraveYard(
        bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId(), bot->GetTeam());
    float const graveDist = grave
        ? botPos.fDist(WorldPosition(grave->map_id, grave->x, grave->y, grave->z)) : 0.0f;
    float const homeDist = home.isValid() ? botPos.fDist(home) : 0.0f;
    switch (PickLongStuckFallbackTarget(true, grave != nullptr, graveDist, homeDist))
    {
        case LongStuckFallbackTarget::Graveyard:
            break;
        case LongStuckFallbackTarget::Homebind:
            grave = nullptr;
            break;
        default:
            return false;
    }

    TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target");
    if (travelTarget)
    {
        sTravelMgr.SetNullTravelTarget(travelTarget);
        RESET_AI_VALUE(bool, "travel target active");
    }
    RESET_AI_VALUE(WorldPosition, "current position");

    // "combat long stuck" also lands here: drop the old fight like
    // RepopAction::Execute does, or the bot arrives still chasing it.
    RESET_AI_VALUE(Unit*, "old target");
    RESET_AI_VALUE(Unit*, "current target");
    RESET_AI_VALUE(Unit*, "pull target");
    RESET_AI_VALUE(bool, "combat::self target");
    bot->SetSelectionGuid(ObjectGuid());

    // Leave a transport first: RemovePassenger keeps MOVEFLAG_ONTRANSPORT, and a
    // stale flag with no transport freezes the bot (issue #216).
    if (bot->GetTransport())
    {
        bot->GetTransport()->RemovePassenger(bot);
        bot->m_movementInfo.RemoveMovementFlag(MOVEFLAG_ONTRANSPORT);
    }

    bool moved = false;
    if (grave)
    {
        moved = bot->TeleportTo(grave->map_id, grave->x, grave->y, grave->z, 0.0f);
        if (moved)
            sPlayerbotAIConfig.logEvent(ai, "LongStuckFallback", "graveyard", master ? master->GetName() : "");
    }
    else
    {
        // A rescue, not a hearth: keep the hearthstone cooldown untouched.
        moved = bot->TeleportToHomebind(0, false);
        if (moved)
            sPlayerbotAIConfig.logEvent(ai, "LongStuckFallback", "homebind", master ? master->GetName() : "");
    }
    if (!moved)
        return false;
    bot->SaveToDB();
    ai->TellDebug(master, "Unstuck: long-stuck fallback teleport relocated the bot.", "debug unstuck");
    return true;
}

static bool LongStuckRescue(PlayerbotAI* ai, Event& event, Player* bot, Player* master, bool hearthAttemptLeftBotInPlace)
{
    AiObjectContext* context = ai->GetAiObjectContext();

    if (AI_VALUE2(bool, "action useful", "hearthstone") && bot->IsAlive() &&
        !hearthAttemptLeftBotInPlace && HearthLeadsSomewhereUseful(ai, bot))
    {
        if (ai->DoSpecificAction("hearthstone", event, true))
            return true;

        ai->TellDebug(master, "Unstuck: hearthstone did not cast, falling back to repop.", "debug unstuck");
    }

    if (ai->DoSpecificAction("repop", event, true))
        return true;

    ai->TellDebug(master, "Unstuck: repop did not move the bot, teleporting to a safe spot.", "debug unstuck");
    return LongStuckFallbackTeleport(ai, bot, master);
}

// The combat-stuck give-up (m-stuck section 5.3): a bare reset left the
// combat order armed, so the bot re-engaged the same wedged mob and the
// trip fired again 5 s later. Blacklist the combat target the way
// ReachTargetAction does (guid + creature kind, five minutes) and drop the
// order with it, so target selection and grind picks steer elsewhere. The
// order clear must come after the blacklist snapshot: Reset clears the same
// values, and a "current target" that survives the reset keeps the bot in
// combat with a creature it just gave up on. Corpses and the bot itself are
// never blacklisted; a corpse stays targeted for looting.
static void GiveUpCombatStuckTarget(PlayerbotAI* ai, Player* bot)
{
    AiObjectContext* context = ai->GetAiObjectContext();

    Unit* target = AI_VALUE(Unit*, "current target");
    ObjectGuid attackGuid = AI_VALUE(ObjectGuid, "attack target");
    if (!target && attackGuid)
        target = ai->GetUnit(attackGuid);
    if (!ShouldGiveUpCombatStuckTarget(target != nullptr,
        target && !sServerFacade.UnitIsDead(target), target == bot,
        target && target->GetVictim() == bot))
        return;

    uint32 const nowMs = WorldTimer::getMSTime();
    uint32 const expiresAt = CombatStuckBlacklistExpiry(nowMs);
    context->GetValue<std::map<ObjectGuid, uint32>&>("unreachable targets")->Get()[target->getObjectGuid()] = expiresAt;
    if (ShouldBlacklistCombatStuckEntry(target->IsCreature()))
        context->GetValue<std::map<uint32, uint32>&>("unreachable entries")->Get()[target->GetEntry()] = expiresAt;

    std::string const targetName = target->GetName();
    uint32 const targetEntry = target->IsCreature() ? target->GetEntry() : 0;
    float const distanceToTarget = bot->GetDistance(target);
    bool const inLos = bot->IsWithinLOSInMap(target, true);
    context->GetValue<ObjectGuid>("attack target")->Set(ObjectGuid());
    context->GetValue<ObjectGuid>("explicit attack target")->Set(ObjectGuid());
    context->GetValue<Unit*>("current target")->Set(nullptr);
    bot->AttackStop();

    std::ostringstream giveUpInfo;
    giveUpInfo << CombatStuckGiveUpReason();
    if (targetEntry)
        giveUpInfo << "|entry=" << targetEntry;
    giveUpInfo << "|dist=" << static_cast<int>(distanceToTarget)
        << "|los=" << (inLos ? "1" : "0");
    sPlayerbotAIConfig.logEvent(ai, "ReachGiveUp", targetName, giveUpInfo.str());
    ai->TellDebug(ai->GetMaster(), "Giving up on " + targetName + " - combat stuck, no progress for 5 min", "debug move");
}

// UnstuckTrip volume control. MoveStuckTrigger polls every 5 s and counts a bot
// as stuck while it moves under 50 yd in 10 minutes or stands still for 5, so
// slow-grinding bots keep it active for hours: per-trip logging wrote ~200 rows
// per bot per hour - 915,558 rows, 92 MB of a 125 MB bot_events.csv (74%) in
// 9 h. One row per stuck episode instead: a new episode starts when the bot has
// really moved (50 yd, the trigger's own no-progress bound) and a liveness row
// keeps a marathon episode visible every 30 minutes. Replayed over those 9 h the
// rule writes 7,147 rows. The trigger source is deliberately NOT an episode
// boundary - a wedged bot flaps between "move stuck" and "combat stuck" every
// few ticks, and counting that as a new episode alone left 74k rows.
static constexpr time_t UNSTUCK_LOG_WINDOW = 30 * MINUTE;
static constexpr float UNSTUCK_LOG_PROGRESS = 50.0f;

bool UnstuckAction::Execute(Event& event)
{
    std::string source = event.GetSource();
    Player* bot = ai->GetBot();
    Player* master = ai->GetMaster();

    // UseHearthStoneAction records where a hearthstone cast started. A cast that gets
    // interrupted never moves the bot, and its position timer keeps running, so the
    // "long stuck" rescue below would fire the same cast again seconds later, on the
    // same spot, forever. Once one attempt has been made from here without the bot
    // leaving, the hearth has nothing left to offer and the chain repops instead
    // (which relocates the bot and ends the stuck episode).
    WorldPosition const hearthAnchor = AI_VALUE2(WorldPosition, "custom position", "hearth attempt anchor");
    bool const hearthAttemptLeftBotInPlace = hearthAnchor.isValid() &&
        WorldPosition(bot).fDist(hearthAnchor) < sPlayerbotAIConfig.tooCloseDistance;

    // Trips swallowed since the previous row ride in the second field. State
    // lives in the facade value store, like "stuck keep count".
    time_t const lastLogged = AI_VALUE2(time_t, "manual time", "unstuck trip logged at");
    WorldPosition const lastAnchor = AI_VALUE2(WorldPosition, "custom position", "unstuck trip anchor");
    int32 repeats = AI_VALUE2(int32, "manual int", "unstuck trip repeats");

    bool const newEpisode = lastLogged == 0 ||
        WorldPosition(bot).sqDistance(lastAnchor) > UNSTUCK_LOG_PROGRESS * UNSTUCK_LOG_PROGRESS;

    if (newEpisode || time(0) - lastLogged >= UNSTUCK_LOG_WINDOW)
    {
        sPlayerbotAIConfig.logEvent(ai, "UnstuckTrip", source, std::to_string(repeats));
        SET_AI_VALUE2(WorldPosition, "custom position", "unstuck trip anchor", WorldPosition(bot));
        SET_AI_VALUE2(time_t, "manual time", "unstuck trip logged at", time(0));
        repeats = 0;
    }
    else
        ++repeats;

    SET_AI_VALUE2(int32, "manual int", "unstuck trip repeats", repeats);

    // Default action if no specific source is matched
    if (source.empty())
    {
        ai->TellDebug(master, "Unstuck: No specific source, resetting.", "debug unstuck");
        return ai->DoSpecificAction("reset", event, true);
    }

    // Handle move stuck scenarios
    if (source.find("move stuck") != std::string::npos)
    {
        ai->TellDebug(master, "Unstuck: Move stuck detected, resetting.", "debug unstuck");

        // The reset below nulls the travel target (PlayerbotAI::Reset(true)).
        // A sticky need - e.g. an unvisited class trainer - then re-requests the
        // same destination within seconds while the bot never moves: live bots
        // re-picked 'trainer class' every ~5 s (this trigger's poll interval)
        // instead of walking there. mod-playerbots' stuck reset never touches
        // travel, so keep an active target across the reset - but bounded: a
        // bot wedged in geometry dispatches movement fine (MoveTo true,
        // retries decay), so an unbounded keep runs the same wall forever.
        // After 3 consecutive stuck resets without real progress (30+ yd from
        // where the keep streak started) the target is retired properly -
        // nulled with its purpose blacklisted (see MoveToTravelTargetAction) -
        // so other purposes (vendor/repair/quest/grind) keep working and the
        // long-stuck hearth/repop path can fire. Fresh targets and real
        // progress reset the streak.
        TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target");
        bool const keepTravel = travelTarget && travelTarget->IsActive() &&
            travelTarget->GetDestination() && travelTarget->getPosition() &&
            typeid(*travelTarget->GetDestination()) != typeid(NullTravelDestination);
        int32 stuckKeeps = AI_VALUE2(int32, "manual int", "stuck keep count");
        WorldPosition stuckAnchor = AI_VALUE2(WorldPosition, "custom position", "stuck keep anchor");
        if (!keepTravel)
        {
            SET_AI_VALUE2(int32, "manual int", "stuck keep count", 0);
            return ai->DoSpecificAction("reset", event, true);
        }
        if (stuckKeeps >= 3 && WorldPosition(bot).sqDistance(stuckAnchor) < 30.0f * 30.0f)
        {
            // No progress across 3 keeps (~15+ min stuck): retire the target
            // instead of preserving it. Null + time-boxed blacklist of the
            // purpose for 5 min so the same destination is not re-picked at
            // once; anything else can still be requested immediately. Filed
            // under the park key the request gate reads back (see the drop
            // path in MoveToTravelTargetAction); the park survives picks of
            // other purposes.
            std::string const purpose = AI_VALUE2(std::string, "manual string", "future travel purpose");
            std::string const parkKey = TravelPurposeParkKey(purpose);
            sTravelMgr.SetNullTravelTarget(travelTarget);
            RESET_AI_VALUE(bool, "travel target active");
            SET_AI_VALUE2(bool, "no active travel destinations", parkKey, true);
            SET_AI_VALUE2(time_t, "manual time", "no travel purpose until::" + parkKey, time(0) + 5 * MINUTE);
            SET_AI_VALUE2(int32, "manual int", "stuck keep count", 0);
            ai->TellDebug(master, "Unstuck: retiring travel target after 3 stuck keeps without progress.", "debug unstuck");
            return ai->DoSpecificAction("reset", event, true);
        }
        TravelDestination* dest = travelTarget->GetDestination();
        WorldPosition* pos = travelTarget->getPosition();
        TravelStatus status = travelTarget->GetStatus();
        std::vector<std::string> conditions = travelTarget->GetConditions();
        bool const forced = travelTarget->IsForced();
        uint32 const moveRetry = travelTarget->GetRetryCount(true);
        uint32 const extendRetry = travelTarget->GetRetryCount(false);
        uint32 const relevance = travelTarget->GetRelevance();
        GuidPosition groupCopy = travelTarget->GetGroupmember();
        std::string const keptPurpose = AI_VALUE2(std::string, "manual string", "future travel purpose");

        bool const reset = ai->DoSpecificAction("reset", event, true);

        travelTarget->SetTarget(dest, pos);
        travelTarget->SetStatus(status);
        travelTarget->SetConditions(conditions);
        travelTarget->SetForced(forced);
        travelTarget->SetRetry(true, moveRetry);
        travelTarget->SetRetry(false, extendRetry);
        travelTarget->SetRelevance(relevance);
        if (!keptPurpose.empty())
            SET_AI_VALUE2(std::string, "manual string", "future travel purpose", keptPurpose);
        if (groupCopy)
            travelTarget->SetGroupCopy(groupCopy);
        if (stuckKeeps == 0 || !stuckAnchor.isValid())
            SET_AI_VALUE2(WorldPosition, "custom position", "stuck keep anchor", WorldPosition(bot));
        SET_AI_VALUE2(int32, "manual int", "stuck keep count", stuckKeeps + 1);

        return reset;
    }

    // Handle long move stuck scenarios
    if (source.find("move long stuck") != std::string::npos)
    {
        ai->TellDebug(master, "Unstuck: Long move stuck detected, attempting hearthstone or repop.", "debug unstuck");
        return LongStuckRescue(ai, event, bot, master, hearthAttemptLeftBotInPlace);
    }

    // Handle combat stuck scenarios. A combat-stuck plain reset used to null
    // the travel target while move-stuck preserved it, so a bot wedged
    // mid-fight re-requested and re-picked its quest target every few seconds
    // while standing still (Khapuzarae: a pick every 4-6 s after the combat
    // UnstuckTrip). Keep an active target across this reset exactly like the
    // move-stuck path above: same keep rule, same 3-keep retirement, same
    // save/restore of destination, position, status, conditions, forced flag,
    // retry counters, relevance and group copy. See TravelRepickPolicy.h.
    if (source.find("combat stuck") != std::string::npos)
    {
        ai->TellDebug(master, "Unstuck: Combat stuck detected, resetting position.", "debug unstuck");
        // Give up the wedged mob BEFORE the reset clears the order: the
        // reset alone re-armed the same target and the trip re-fired. A
        // corpse is intentionally left alone (looting), and a missing
        // target is a no-op - travel handling below is unchanged.
        GiveUpCombatStuckTarget(ai, bot);
        TravelTarget* combatTarget = AI_VALUE(TravelTarget*, "travel target");
        TravelDestination* combatDestProbe = combatTarget ? combatTarget->GetDestination() : nullptr;
        bool const keepCombatTravel = ShouldKeepTravelAcrossStuckReset(combatTarget != nullptr,
            combatTarget && combatTarget->IsActive(),
            combatDestProbe != nullptr,
            combatTarget && combatTarget->getPosition() != nullptr,
            combatDestProbe && typeid(*combatDestProbe) == typeid(NullTravelDestination));
        int32 combatKeeps = AI_VALUE2(int32, "manual int", "stuck keep count");
        WorldPosition combatAnchor = AI_VALUE2(WorldPosition, "custom position", "stuck keep anchor");
        if (!keepCombatTravel)
        {
            SET_AI_VALUE2(int32, "manual int", "stuck keep count", 0);
            return ai->DoSpecificAction("reset", event, true);
        }
        if (ShouldRetireStuckTravelKeep(combatKeeps, WorldPosition(bot).sqDistance(combatAnchor)))
        {
            // No progress across 3 keeps: retire the target instead of
            // preserving it, like the move-stuck path. Null + time-boxed
            // blacklist of the purpose for 5 min so the same destination is
            // not re-picked at once; anything else can still be requested.
            // Filed under the park key the request gate reads back (see the
            // drop path in MoveToTravelTargetAction).
            std::string const combatPurpose = AI_VALUE2(std::string, "manual string", "future travel purpose");
            std::string const combatParkKey = TravelPurposeParkKey(combatPurpose);
            sTravelMgr.SetNullTravelTarget(combatTarget);
            RESET_AI_VALUE(bool, "travel target active");
            SET_AI_VALUE2(bool, "no active travel destinations", combatParkKey, true);
            SET_AI_VALUE2(time_t, "manual time", "no travel purpose until::" + combatParkKey, time(0) + 5 * MINUTE);
            SET_AI_VALUE2(int32, "manual int", "stuck keep count", 0);
            ai->TellDebug(master, "Unstuck: retiring travel target after 3 stuck keeps without progress.", "debug unstuck");
            return ai->DoSpecificAction("reset", event, true);
        }
        TravelDestination* combatDest = combatTarget->GetDestination();
        WorldPosition* combatPos = combatTarget->getPosition();
        TravelStatus combatStatus = combatTarget->GetStatus();
        std::vector<std::string> combatConditions = combatTarget->GetConditions();
        bool const combatForced = combatTarget->IsForced();
        uint32 const combatMoveRetry = combatTarget->GetRetryCount(true);
        uint32 const combatExtendRetry = combatTarget->GetRetryCount(false);
        uint32 const combatRelevance = combatTarget->GetRelevance();
        GuidPosition combatGroupCopy = combatTarget->GetGroupmember();
        std::string const combatKeptPurpose = AI_VALUE2(std::string, "manual string", "future travel purpose");
        bool const combatReset = ai->DoSpecificAction("reset", event, true);
        // Reset(true) clears "future travel purpose" while this path keeps the
        // travel target: without the restore the drop would park "" while
        // requests check "quest", so the 5-min park never matched (m-stuck
        // section 5.4, 514 empty fails + 1,658 empty drops). Snapshot before
        // the reset - reading after returns the wiped value.
        combatTarget->SetTarget(combatDest, combatPos);
        combatTarget->SetStatus(combatStatus);
        combatTarget->SetConditions(combatConditions);
        combatTarget->SetForced(combatForced);
        combatTarget->SetRetry(true, combatMoveRetry);
        combatTarget->SetRetry(false, combatExtendRetry);
        combatTarget->SetRelevance(combatRelevance);
        if (!combatKeptPurpose.empty())
            SET_AI_VALUE2(std::string, "manual string", "future travel purpose", combatKeptPurpose);
        if (combatGroupCopy)
            combatTarget->SetGroupCopy(combatGroupCopy);
        if (combatKeeps == 0 || !combatAnchor.isValid())
            SET_AI_VALUE2(WorldPosition, "custom position", "stuck keep anchor", WorldPosition(bot));
        SET_AI_VALUE2(int32, "manual int", "stuck keep count", combatKeeps + 1);
        return combatReset;
    }

    // Handle long combat stuck scenarios
    if (source.find("combat long stuck") != std::string::npos)
    {
        ai->TellDebug(master, "Unstuck: Long combat stuck detected, attempting hearthstone or repop.", "debug unstuck");
        return LongStuckRescue(ai, event, bot, master, hearthAttemptLeftBotInPlace);
    }

    // Fallback to reset if no specific condition is met
    ai->TellDebug(master, "Unstuck: Fallback to reset action.", "debug unstuck");
    return ai->DoSpecificAction("reset", event, true);
}
