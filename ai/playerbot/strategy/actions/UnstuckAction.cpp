#include "playerbot/playerbot.h"
#include "UnstuckAction.h"
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

    // One line per trip, not per tick: the stuck triggers poll every 5 s, so a
    // wedged bot produces a line per poll until it moves. This is the only
    // record of a bot that dispatches movement and stays put.
    sPlayerbotAIConfig.logEvent(ai, "UnstuckTrip", source);

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
            // once; anything else can still be requested immediately.
            std::string const purpose = AI_VALUE2(std::string, "manual string", "future travel purpose");
            sTravelMgr.SetNullTravelTarget(travelTarget);
            RESET_AI_VALUE(bool, "travel target active");
            if (!purpose.empty())
            {
                SET_AI_VALUE2(bool, "no active travel destinations", purpose, true);
                SET_AI_VALUE2(time_t, "manual time", "no travel purpose until::" + purpose, time(0) + 5 * MINUTE);
            }
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

        bool const reset = ai->DoSpecificAction("reset", event, true);

        travelTarget->SetTarget(dest, pos);
        travelTarget->SetStatus(status);
        travelTarget->SetConditions(conditions);
        travelTarget->SetForced(forced);
        travelTarget->SetRetry(true, moveRetry);
        travelTarget->SetRetry(false, extendRetry);
        travelTarget->SetRelevance(relevance);
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
        if (AI_VALUE2(bool, "action useful", "hearthstone") && bot->IsAlive() && !hearthAttemptLeftBotInPlace && HearthLeadsSomewhereUseful(ai, bot))
        {
            return ai->DoSpecificAction("hearthstone", event, true);
        }
        else
        {
            return ai->DoSpecificAction("repop", event, true);
        }
    }

    // Handle combat stuck scenarios
    if (source.find("combat stuck") != std::string::npos)
    {
        ai->TellDebug(master, "Unstuck: Combat stuck detected, resetting position.", "debug unstuck");
        return ai->DoSpecificAction("reset", event, true);
    }

    // Handle long combat stuck scenarios
    if (source.find("combat long stuck") != std::string::npos)
    {
        ai->TellDebug(master, "Unstuck: Long combat stuck detected, attempting hearthstone or repop.", "debug unstuck");
        if (AI_VALUE2(bool, "action useful", "hearthstone") && bot->IsAlive() && !hearthAttemptLeftBotInPlace && HearthLeadsSomewhereUseful(ai, bot))
        {
            return ai->DoSpecificAction("hearthstone", event, true);
        }
        else
        {
            return ai->DoSpecificAction("repop", event, true);
        }
    }

    // Fallback to reset if no specific condition is met
    ai->TellDebug(master, "Unstuck: Fallback to reset action.", "debug unstuck");
    return ai->DoSpecificAction("reset", event, true);
}
