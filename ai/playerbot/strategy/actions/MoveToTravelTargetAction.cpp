
#include "playerbot/GroupMembers.h"
#include "playerbot/playerbot.h"
#include "MoveToTravelTargetAction.h"
#include "ChooseTravelTargetAction.h"
#include "TalkToQuestGiverAction.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/RandomBotFacade.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/LootObjectStack.h"
#include "Maps/PathFinder.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/strategy/values/FreeMoveValues.h"
#include <cstdlib>
#include <iomanip>

using namespace ai;

// Stuck-hand-in fallback (TryAutoHandInUnreachableTaker). Several bots on the live
// realm walked to a taker they could never reach - the Tower of Azora's Antonas
// Riftgaze, whose navmesh tile has no walkable route to the NPC; every bot stalls
// 18-29 yd away, at the same height, and none ever handed the quest in. A hand-in
// trip that keeps failing to move is therefore settled here after a short window.
static constexpr float AUTO_HAND_IN_RANGE = 100.0f;           // the bot is at the taker, only the last step fails
static constexpr uint32 AUTO_HAND_IN_FAILS = 3;               // failed moves inside one episode
static constexpr time_t AUTO_HAND_IN_AGE = 90;                // seconds since the episode's first failed move
static constexpr int32 AUTO_HAND_IN_EPISODE = 10 * MINUTE;    // no failed move for this long ends the episode
static constexpr int32 AUTO_HAND_IN_PARK = 30 * MINUTE;       // the quest's hand-in travel park, set when it fires

bool MoveToTravelTargetAction::TryAutoHandInUnreachableTaker(TravelTarget* target, std::string const& purpose, float distance)
{
    if (purpose != "quest")
        return false;

    // Pool bots with no real master only. A hired or player-commanded bot keeps the
    // normal hand-in: its master may be walking it to the taker.
    if (!sPlayerbotAIConfig.botQuestLogUpkeep || ai->HasActivePlayerMaster() || !sRandomBotFacade.IsRandomBot(bot))
        return false;

    QuestTravelDestination* destination = dynamic_cast<QuestTravelDestination*>(target->GetDestination());
    if (!destination || destination->GetPurpose() != TravelDestinationPurpose::QuestTaker)
        return false;

    int32 const takerEntry = destination->GetEntry();
    if (takerEntry <= 0)
        return false;

    uint32 const questId = destination->GetQuestId();
    Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
    if (!quest)
        return false;

    // Only a finished quest the bot can be paid for right now.
    if (bot->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE || bot->GetQuestRewardStatus(questId) || !bot->CanRewardQuest(quest, false))
        return false;

    // The failure must be the last step to a taker the bot is already standing next to.
    if (distance > AUTO_HAND_IN_RANGE)
        return false;

    // Failure episode, keyed per quest: one unreachable taker cannot settle another
    // quest's hand-in, and a repeatable quest gets a fresh episode once a window
    // passes without a failed move. The value counts failed moves, the data holds
    // the episode's first failure.
    std::string const key = "quest hand in fail::" + std::to_string(questId);
    uint32 const fails = sRandomBotFacade.GetValue(bot, key);
    std::string const sinceStr = sRandomBotFacade.GetData(bot->GetGUIDLow(), key);
    time_t const since = sinceStr.empty() ? time(0) : static_cast<time_t>(std::strtoul(sinceStr.c_str(), nullptr, 10));
    sRandomBotFacade.SetValue(bot, key, fails + 1, std::to_string(since), AUTO_HAND_IN_EPISODE);

    if (fails + 1 < AUTO_HAND_IN_FAILS || time(0) - since < AUTO_HAND_IN_AGE)
        return false;

    // Park this quest's hand-in travel (per quest, not the whole purpose): the taker
    // is not walkable to, so the search must stop offering it. Read by
    // RequestQuestTravelTargetAction. The nearby-service hand-in does not read the
    // park, so a bot that does end up next to its taker is still paid normally.
    SET_AI_VALUE2(time_t, "manual time", "no quest hand in until::" + std::to_string(questId), time(0) + AUTO_HAND_IN_PARK);

    Creature* taker = bot->FindNearestCreature((uint32)takerEntry, AUTO_HAND_IN_RANGE * 2);
    if (!taker)
        return false;

    // Same reward path as a talk-in: the reward choice (best item for the class and
    // spec) and the core Player::RewardQuest call, which carries the XP, money and
    // reputation. Returns false when no reward was actually given.
    if (!TalkToQuestGiverAction::RewardFinishedQuest(ai, quest, taker))
        return false;

    sPlayerbotAIConfig.logEvent(ai, "QuestAutoHandIn", quest->GetTitle(),
        std::to_string(questId) + ":" + std::to_string(takerEntry) + ":unreachable");

    return true;
}

bool MoveToTravelTargetAction::Execute(Event& event)
{
    TravelTarget* target = AI_VALUE(TravelTarget*, "travel target");

    if (target->GetStatus() == TravelStatus::TRAVEL_STATUS_READY)
    {
        ai->TellDebug(ai->GetMaster(), "The target is ready to travel start now.", "debug travel");
        target->SetStatus(TravelStatus::TRAVEL_STATUS_TRAVEL);
    }

    target->CheckStatus();

    if (target->GetStatus() != TravelStatus::TRAVEL_STATUS_TRAVEL)
        return true;

    WorldPosition botLocation(bot);
    WorldPosition location = *target->getPosition();

    Group* group = bot->GetGroup();
    if (ai->IsGroupLeader() && !urand(0, 1) && !bot->IsInCombat())
    {
        for (Player* member : LiveGroupMembers(group))
        {
            if (member == bot)
                continue;

            if (!member->IsAlive())
                continue;

            if (!member->IsMoving())
                continue;

            if (PlayerbotAIStorage::Instance().GetAI(member) &&
                !(PlayerbotAIStorage::Instance().GetAI(member)->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) || PlayerbotAIStorage::Instance().GetAI(member)->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT)))
                continue;

            WorldPosition memberPos(member);
            WorldPosition targetPos = *target->getPosition();

            float memberDistance = std::min(botLocation.distance(memberPos), location.distance(memberPos));

            if (memberDistance < 50.0f)
                continue;
            if (memberDistance > sPlayerbotAIConfig.reactDistance * 20)
                continue;

           // float memberAngle = botLocation.GetAngleBetween(targetPos, memberPos);

           // if (botLocation.GetMapId() == targetPos.GetMapId() && botLocation.GetMapId() == memberPos.GetMapId() && memberAngle < M_PI_F / 2) //We are heading that direction anyway.
           //     continue;

            if (!urand(0, 5))
            {
                std::ostringstream out;
                if ((ai->GetMaster() && !bot->GetGroup()->IsMember(ai->GetMaster()->getObjectGuid())) || !ai->HasActivePlayerMaster())
                    out << "Waiting a bit for ";
                else
                    out << "Please hurry up ";

                out << member->GetName();

                if (PlayerbotAIStorage::Instance().GetAI(bot) && !ai->HasActivePlayerMaster())
                {
                    out << " who is " << round(memberDistance) << "y away";
                    if (!memberPos.GetAreaName().empty())
                        out << " in " << memberPos.GetAreaName();
                }

                // "Waiting a bit for X" is AI narration between pool bots while a
                // group travels together: nobody asked to read it. A real player
                // waiting on their own party still gets the nudge.
                if (isRealPlayer_Helper(GetMaster()))
                    ai->TellPlayerNoFacing(GetMaster(), out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            }

            // Introduce a random delay between 80% and 120% of maxWaitForMove to make waiting more natural
            uint32 randomDelay = sPlayerbotAIConfig.maxWaitForMove * (urand(80, 120) / 100.0f);
            target->SetExpireIn(target->GetTimeLeft() + randomDelay);

            SetDuration(randomDelay);

            // Occasionally face the member and perform an emote
            if (urand(0, 3) == 0) { // 25% chance to emote
                bot->SetFacingToObject(member);
                uint32 emoteChoice = urand(0, 2);
                switch (emoteChoice) {
                    case 0:
                        bot->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
                        break;
                    case 1:
                        bot->HandleEmoteCommand(EMOTE_ONESHOT_TALK);
                        break;
                    case 2:
                        bot->HandleEmoteCommand(EMOTE_ONESHOT_EXCLAMATION);
                        break;
                }
            }

            return true;
        }
    }

    float x = location.getX();
    float y = location.getY();
    float z = location.getZ();
    float mapId = location.GetMapId();

    if (botLocation.GetMapId() == location.GetMapId() && botLocation.sqDistance2d(location) < 10000.0f)
    {
        float maxDistance = target->GetDestination()->GetRadiusMin();

        float angle = 2 * M_PI * urand(0, 100) / 100.0;
        float mod = urand(50, 100) / 100.0;

        x += cos(angle) * maxDistance * mod;
        y += sin(angle) * maxDistance * mod;

        if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "Moving to ";
            out << target->GetDestination()->GetTitle();
            if (!(*target->getPosition() == WorldPosition()))
            {
                out << " at " << uint32(target->getPosition()->distance(bot)) << "y";
            }
            if (target->GetStatus() != TravelStatus::TRAVEL_STATUS_EXPIRED)
                out << " for " << (target->GetTimeLeft() / 1000) << "s";
            if (target->GetRetryCount(true))
                out << " (move retry: " << target->GetRetryCount(true) << ")";
            else if (target->GetRetryCount(false))
                out << " (retry: " << target->GetRetryCount(false) << ")";
            ai->TellPlayerNoFacing(GetMaster(), out);
        }
    }

    bool canMove = MoveTo(mapId, x, y, z, false, false);

    if (!canMove)
    {
        target->IncRetry(true);

        std::string const purpose = AI_VALUE2(std::string, "manual string", "future travel purpose");

        // One line per travel target rather than per attempt: a wedged bot
        // re-enters this action every tick, and IncRetry steps by 2, so the
        // first failure is exactly 2. The drop below carries the final depth.
        // Purpose plus remaining distance is what separates "could not path to
        // the taker" from "never tried to move" (no line at all).
        if (target->GetRetryCount(true) == 2)
            sPlayerbotAIConfig.logEvent(ai, "TravelMoveFailed", purpose,
                std::to_string((int32)botLocation.distance(location)));

        // A hand-in trip that keeps failing to move to a taker the bot is standing
        // next to is settled here instead of looping (see the helper). The quest is
        // paid, so there is nothing left to walk to: let the next tick pick again.
        if (TryAutoHandInUnreachableTaker(target, purpose, botLocation.distance(location)))
            return false;

        if (target->IsMaxRetry(true))
        {
            ai->TellDebug(ai->GetMaster(), "The target is unreachable, dropping it so other travel still works.", "debug travel");
            // Six failed moves in a row means this spot is effectively
            // unreachable from here (no path, other continent, geometry).
            // Drop the target outright and blacklist just this purpose for 5
            // min (the per-kind give-up window, ReachTargetActions.h): other
            // purposes (vendor/repair/quest/grind) stay requestable, and the
            // same destination is not re-picked at once. A COOLDOWN on the
            // active target would instead freeze ALL travel (IsActive stays
            // true, requests gate on it) for the whole window.
            sPlayerbotAIConfig.logEvent(ai, "TravelTargetDropped", purpose, std::to_string(target->GetRetryCount(true)));
            target->SetForced(false);
            sTravelMgr.SetNullTravelTarget(target);
            RESET_AI_VALUE(bool, "travel target active");
            if (!purpose.empty())
            {
                // Time-boxed, not permanent: ManualSetValue has no expiry, so
                // record when the blacklist was set and let isUseful clear it
                // after 5 min. Cleared early by any successful pick
                // (setNewTarget clears all blacklists).
                SET_AI_VALUE2(bool, "no active travel destinations", purpose, true);
                SET_AI_VALUE2(time_t, "manual time", "no travel purpose until::" + purpose, time(0) + 5 * MINUTE);
            }
        }
    }
    else
        target->DecRetry(true);

    if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
    {
        WorldPosition* pos = target->getPosition();
        GuidPosition* guidP = dynamic_cast<GuidPosition*>(pos);

        std::string name = (guidP && guidP->GetWorldObject(bot->GetInstanceId())) ? chat->formatWorldobject(guidP->GetWorldObject(bot->GetInstanceId())) : "travel target";

        if (mapId == bot->GetMapId())
        {
            ai->Poi(x, y, name);
        }
        else
        {
            LastMovement& lastMove = *context->GetValue<LastMovement&>("last movement");
            if (!lastMove.lastPath.empty() && lastMove.lastPath.GetBack().distance(location) < 20.0f)
            {
                for (auto& p : lastMove.lastPath.GetPointPath())
                {
                    if (p.GetMapId() == bot->GetMapId())
                        ai->Poi(p.getX(), p.getY(), name);
                }
            }
        }
    }

    return canMove;
}

bool MoveToTravelTargetAction::isUseful()
{
    if (TravelBlockedInsideInstance(ai))
        return false;

    if (!bot->IsAlive()) // a ghost travelling or picking fights instead of going for its corpse
        return false;

    if (!ai->AllowActivity(TRAVEL_ACTIVITY))
        return false;

    if (!AI_VALUE(bool, "travel target traveling") && AI_VALUE(TravelTarget*, "travel target")->GetStatus() != TravelStatus::TRAVEL_STATUS_READY)
        return false;

    if (bot->IsTaxiFlying())
        return false;

    if (MEM_AI_VALUE(WorldPosition, "current position")->LastChangeDelay() < 10)
        if (bot->IsMoving())
            return false;

    if (!AI_VALUE(bool, "can move around"))
        return false;

    TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target");

    if (ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) || ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT))
    {
        auto conditions = travelTarget->GetConditions();
        for (auto& cond : conditions)
        {
            if (cond == "should travel named::guild order")
                return false;
        }
    }

    if (bot->GetGroup() && !bot->GetGroup()->IsLeader(bot->getObjectGuid()))
        if (ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) ||
            ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT) ||
            ai->HasStrategy("guard", BotState::BOT_STATE_NON_COMBAT))
            if (!travelTarget->IsForced())
                return false;

    WorldPosition travelPos(*travelTarget->getPosition());

    if (travelPos.isDungeon() && bot->GetGroup() && bot->GetGroup()->IsLeader(bot->getObjectGuid()) && sTravelMgr.MapTransDistance(bot, travelPos, true) < sPlayerbotAIConfig.sightDistance && !AI_VALUE2(bool, "group and", "near leader"))
        return false;

    if (AI_VALUE(bool, "has available loot"))
    {
        LootObject lootObject = AI_VALUE(LootObjectStack*, "available loot")->GetLoot(sPlayerbotAIConfig.lootDistance);
        if (lootObject.IsLootPossible(bot))
            return false;
    }

    if (!travelTarget->IsForced())
        if (!CanFreeMoveValue::CanFreeMoveTo(ai, *travelTarget->getPosition()))
            return false;

    return true;
}
