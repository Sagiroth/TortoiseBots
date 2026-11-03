#include "playerbot/playerbot.h"
#include "DebugAction.h"
#include "playerbot/PlayerbotAIConfig.h"
#include <playerbot/TravelNode.h>
#include "ChooseTravelTargetAction.h"
#include "playerbot/strategy/values/SharedValueContext.h"
#include "playerbot/strategy/values/FreeMoveValues.h"
#include "playerbot/strategy/actions/RpgSubActions.h"
#include "playerbot/LootObjectStack.h"
#include "GameEventMgr.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/PlayerbotHelpMgr.h"
#include "Transports/Transport.h"
#include "Maps/PathFinder.h"

#include "vmap/VMapFactory.h"

#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

#include <iomanip>
#include <typeindex>
#include "Maps/MoveMap.h"

using namespace ai;
using namespace MaNGOS;

bool DebugAction::HandleValues(Event& event, Player* requester, const std::string& text)
{
    //    ai->TellPlayerNoFacing(requester, ai->GetAiObjectContext()->FormatValues(text.substr(7)));

    std::string param = "";
    if (text.length() > 6)
    {
        param = text.substr(7);
    }

    std::set<std::string> names = context->GetValues();
    std::vector<std::string> values;
    for (auto name : names)
    {
        UntypedValue* value = context->GetUntypedValue(name);
        if (!value)
        {
            continue;
        }

        if (!param.empty() && name.find(param) == std::string::npos)
        {
            continue;
        }

        std::string text = value->Format();
        if (text == "?")
        {
            continue;
        }

        values.push_back(name + "=" + text);
    }

    std::string valuestring = sPlayerbotHelpMgr.makeList(values, "[<part>]");

    std::vector<std::string> lines = Qualified::getMultiQualifiers(valuestring, "\n");
    for (auto& line : lines)
    {
        ai->TellPlayerNoFacing(requester, line, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true, false);
    }

    return true;
}

bool DebugAction::HandleSetValueUInt32(Event& event, Player* requester, const std::string& text)
{
    std::vector<std::string> args = Qualified::getMultiQualifiers(text.substr(14), ",");

    if (args.size() == 1)
    {
        RESET_AI_VALUE(uint32, args[0]);
        return true;
    }
    else if (args.size() == 2)
    {
        SET_AI_VALUE(uint32, args[0], stoi(args[1]));
        return true;
    }
    else if (args.size() == 3)
    {
        SET_AI_VALUE2(uint32, args[0], args[1], stoi(args[2]));
        return true;
    }

    return false;
}

bool DebugAction::HandleDo(Event& event, Player* requester, const std::string& text)
{
    return ai->DoSpecificAction(text.substr(3), Event(), true);
}

bool DebugAction::HandleNC(Event& event, Player* requester, const std::string& text)
{
    std::ostringstream out;

    std::string name;
    if (text.size() > 3)
        name = text.substr(3);

    std::list<TriggerNode*> triggerNodes;
    std::set<std::string> strategies;
    context->GetSupportedStrategies(strategies);
    for (auto& strategyName : strategies)
    {
        if (!name.empty() && strategyName.find(name) == std::string::npos)
            continue;

        Strategy* strategy = context->GetStrategy(strategyName);

        ai->TellPlayerNoFacing(requester, "s:" + strategyName + (ai->HasStrategy(strategyName, BotState::BOT_STATE_NON_COMBAT) ? " [active]" : ""), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false, false);

        strategy->InitTriggers(triggerNodes, BotState::BOT_STATE_NON_COMBAT);

        //Loop over all triggers of this strategy.
        for (auto& triggerNode : triggerNodes)
        {
            Trigger* trigger = context->GetTrigger(triggerNode->GetName());

            if (trigger)
            {
                ai->TellPlayerNoFacing(requester, "t: " + triggerNode->GetName() + (trigger->IsActive() ? " [active]" : ""), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false, false);
                triggerNode->setTrigger(trigger);

                Trigger* trigger = triggerNode->GetTrigger();

                NextAction** nextActions = triggerNode->GetHandlers();

                bool isRpg = false;

                //Loop over all actions triggered by this trigger and check if any is an 'rpg action'.
                for (int32 i = 0; i < NextAction::size(nextActions); i++)
                {
                    NextAction* nextAction = nextActions[i];

                    Action* action = ai->GetAiObjectContext()->GetAction(nextAction->GetName());

                    if (action)
                    {
                        std::ostringstream out;
                        out << "a:  ";
                        out << nextAction->GetName();
                        out << triggerNode->GetName();
                        out << (action->IsUseful() ? " [usefull]" : "");
                        out << (action->IsPossible() ? " [possible]" : "");
                        out << "(";
                        out << std::setprecision(3);
                        out << nextAction->GetRelevance();
                        out << ")";

                        ai->TellPlayerNoFacing(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false, false);
                    }
                    else
                    {
                        std::ostringstream out;
                        out << "a:  [unknown]";
                        out << nextAction->GetName();
                        out << triggerNode->GetName();
                        out << " (";
                        out << std::setprecision(3);
                        out << nextAction->GetRelevance();
                        out << ")";

                        ai->TellPlayerNoFacing(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false, false);
                    }

                }
                NextAction::destroy(nextActions);
            }
        }

        for (std::list<TriggerNode*>::iterator i = triggerNodes.begin(); i != triggerNodes.end(); i++)
        {
            TriggerNode* trigger = *i;
            delete trigger;
        }

        triggerNodes.clear();
    }

    return true;
}

bool DebugAction::HandleStuck(Event& event, Player* requester, const std::string& text)
{
    bool shouldReset = (text.find("reset") != std::string::npos);

    ai->TellPlayer(requester, "=== Stuck Bot Diagnostic ===");

    WorldPosition botPos(bot);
    std::ostringstream posOut;
    posOut << "Pos: " << botPos.getX() << "," << botPos.getY() << "," << botPos.getZ() << " (" << botPos.GetAreaName() << ")";
    ai->TellPlayer(requester, posOut.str());

    bool isMoving = bot->IsMoving();
    ai->TellPlayer(requester, std::string("IsMoving: ") + (isMoving ? "yes" : "no"));

    bool isMounted = bot->IsMounted();
    ai->TellPlayer(requester, std::string("IsMounted: ") + (isMounted ? "yes" : "no"));

    bool isTaxiFlying = bot->IsTaxiFlying();
    ai->TellPlayer(requester, std::string("IsTaxiFlying: ") + (isTaxiFlying ? "yes" : "no"));

    bool isInCombat = bot->IsInCombat();
    ai->TellPlayer(requester, std::string("IsInCombat: ") + (isInCombat ? "yes" : "no"));

    bool isDead = !bot->IsAlive();
    ai->TellPlayer(requester, std::string("IsDead: ") + (isDead ? "yes" : "no"));

    TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target");
    bool canFreeMove = false;
    if (travelTarget)
    {
        std::ostringstream ss;
        ss << "Travel Target: ";
        switch (travelTarget->GetStatus())
        {
            case TravelStatus::TRAVEL_STATUS_NONE: ss << "NONE"; break;
            case TravelStatus::TRAVEL_STATUS_PREPARE: ss << "PREPARE"; break;
            case TravelStatus::TRAVEL_STATUS_WORK: ss << "WORK"; break;
            case TravelStatus::TRAVEL_STATUS_TRAVEL: ss << "TRAVEL"; break;
            case TravelStatus::TRAVEL_STATUS_READY: ss << "READY"; break;
            case TravelStatus::TRAVEL_STATUS_EXPIRED: ss << "EXPIRED"; break;
            case TravelStatus::TRAVEL_STATUS_COOLDOWN: ss << "COOLDOWN"; break;
            default: ss << "UNKNOWN"; break;
        }
        ss << " (" << travelTarget->GetTimeLeft() / 1000 << "s left)";
        ai->TellPlayer(requester, ss.str());

        if (travelTarget->getPosition())
        {
            std::ostringstream ss2;
            ss2 << "Target: " << travelTarget->getPosition()->getX() << ","
                << travelTarget->getPosition()->getY() << "," << travelTarget->getPosition()->getZ()
                << " (" << travelTarget->getPosition()->GetAreaName() << ")";
            ai->TellPlayer(requester, ss2.str());

            float dist = botPos.distance(*travelTarget->getPosition());
            std::ostringstream ss3;
            ss3 << "Distance to target: " << uint32(dist) << "y";
            ai->TellPlayer(requester, ss3.str());
        }

        std::ostringstream ss4;
        ss4 << "Retry: " << travelTarget->GetRetryCount(true) << " (move), " << travelTarget->GetRetryCount(false) << " (target)";
        ai->TellPlayer(requester, ss4.str());

        canFreeMove = CanFreeMoveValue::CanFreeMoveTo(ai, travelTarget->GetPosStr());
    }

    bool canMoveAround = AI_VALUE(bool, "can move around");
    ai->TellPlayer(requester, std::string("can move around: ") + (canMoveAround ? "true" : "FALSE!"));

    bool travelTargetActive = AI_VALUE(bool, "travel target active");
    ai->TellPlayer(requester, std::string("travel target active: ") + (travelTargetActive ? "true" : "false"));

    bool travelTargetTraveling = AI_VALUE(bool, "travel target traveling");
    ai->TellPlayer(requester, std::string("travel target traveling: ") + (travelTargetTraveling ? "true" : "FALSE!"));

    if (travelTarget && travelTarget->getPosition())
    {
        ai->TellPlayer(requester, std::string("can free move to target: ") + (canFreeMove ? "true" : "FALSE!"));

        bool differentMap = (travelTarget->getPosition()->GetMapId() != bot->GetMapId());
        if (differentMap)
        {
            ai->TellPlayer(requester, ">>> TARGET IS ON DIFFERENT MAP! Needs taxi/transport.");
            ai->TellPlayer(requester, "    Bot map: " + std::to_string(bot->GetMapId()) + ", Target map: " + std::to_string(travelTarget->getPosition()->GetMapId()));
        }
    }

    uint32 posLastChange = MEM_AI_VALUE(WorldPosition, "current position")->LastChangeDelay();
    std::ostringstream ss;
    ss << "Position last changed: " << posLastChange << "s ago";
    ai->TellPlayer(requester, ss.str());

    bool isStuck = (posLastChange > 60 && travelTargetActive);
    if (isStuck)
    {
        ai->TellPlayer(requester, ">>> BOT IS STUCK! Position unchanged for > 60s with active travel target!");
    }

    Group* group = bot->GetGroup();
    if (group)
    {
        bool isLeader = group->IsLeader(bot->getObjectGuid());
        ai->TellPlayer(requester, std::string("In group, is leader: ") + (isLeader ? "yes" : "no"));

        if (!isLeader)
        {
            bool hasFollow = ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT);
            bool hasStay = ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT);
            ai->TellPlayer(requester, std::string("Has follow strategy: ") + (hasFollow ? "yes" : "no"));
            ai->TellPlayer(requester, std::string("Has stay strategy: ") + (hasStay ? "yes" : "no"));
        }
    }

    if (shouldReset && travelTarget)
    {
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, ">>> Resetting travel target...");
        travelTarget->SetStatus(TravelStatus::TRAVEL_STATUS_NONE);
        travelTarget->SetForced(false);
        RESET_AI_VALUE(TravelTarget*, "travel target");
        ai->TellPlayer(requester, ">>> Travel target reset! Bot should find new target.");
    }

    ai->TellPlayer(requester, "");
    ai->TellPlayer(requester, "=== Movement Blockers ===");

    bool isTrading = bot->GetTradeData() != nullptr;
    ai->TellPlayer(requester, std::string("IsTrading: ") + (isTrading ? "TRUE - BLOCKS MOVEMENT!" : "no"));

    bool groupReady = AI_VALUE(bool, "group ready");
    ai->TellPlayer(requester, std::string("Group ready: ") + (groupReady ? "yes" : "NO - BLOCKS MOVEMENT!"));

    bool castNcActive = AI_VALUE2(bool, "trigger active", "castnc");
    ai->TellPlayer(requester, std::string("CastNC active: ") + (castNcActive ? "TRUE - BLOCKS MOVEMENT!" : "no"));

    bool hasWander = ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT);
    if (hasWander)
    {
        float dist = AI_VALUE2(float, "distance", "master target");
        float wanderMax = ai->GetRange("wandermax");
        bool wanderTooFar = dist > wanderMax;
        ai->TellPlayer(requester, std::string("Wander strategy: yes (dist=") + std::to_string((int)dist) + ", max=" + std::to_string((int)wanderMax) + ") " + (wanderTooFar ? "- BLOCKS MOVEMENT!" : ""));
    }

    ai->TellPlayer(requester, "");
    ai->TellPlayer(requester, "=== Path/Rout e Status ===");
    LastMovement& lastMove = *context->GetValue<LastMovement&>("last movement");
    bool pathEmpty = lastMove.lastPath.empty();
    ai->TellPlayer(requester, std::string("lastPath empty: ") + (pathEmpty ? "YES" : "no"));
    if (!pathEmpty)
    {
        ai->TellPlayer(requester, "lastPath points: " + std::to_string(lastMove.lastPath.GetPath().size()));
        ai->TellPlayer(requester, "lastPath end: " + std::to_string(lastMove.lastPath.GetBack().getX()) + "," +
            std::to_string(lastMove.lastPath.GetBack().getY()) + "," +
            std::to_string(lastMove.lastPath.GetBack().getZ()));
    }

    ai->TellPlayer(requester, "");
    ai->TellPlayer(requester, "=== Other Status ===");

    bool hasStay = ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT);
    ai->TellPlayer(requester, std::string("Has stay strategy: ") + (hasStay ? "yes" : "no"));

    bool hasFollow = ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT);
    ai->TellPlayer(requester, std::string("Has follow strategy: ") + (hasFollow ? "yes" : "no"));

    bool hasGuard = ai->HasStrategy("guard", BotState::BOT_STATE_NON_COMBAT);
    ai->TellPlayer(requester, std::string("Has guard strategy: ") + (hasGuard ? "yes" : "no"));

    //uint32 auraCount = bot->GetAuraCount();
    //ai->TellPlayer(requester, "Aura count: " + std::to_string(auraCount));

    ai->TellPlayer(requester, "");
    ai->TellPlayer(requester, "=== Suggested Actions ===");
    if (!canMoveAround)
    {
        ai->TellPlayer(requester, "- Check: is bot in combat? Is bot dead? Is bot rooted?");
    }
    if (travelTarget && travelTarget->GetStatus() == TravelStatus::TRAVEL_STATUS_READY && !isMoving)
    {
        ai->TellPlayer(requester, "- Try: .rndbot cmd <bot> reset travel target");
    }
    if (posLastChange > 120)
    {
        ai->TellPlayer(requester, "- Bot may need teleportation: .rndbot debug <bot> position teleport");
    }
    if (pathEmpty && travelTargetActive)
    {
        ai->TellPlayer(requester, "- Try: .rndbot debug <bot> position route <destination>");
    }

    return true;
}

bool DebugAction::HandleCombat(Event& event, Player* requester, const std::string& text)
{
    ai->TellPlayer(requester, "=== Combat State Diagnostic ===");

    bool unitFlagInCombat = bot->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IN_COMBAT);
    ai->TellPlayer(requester, std::string("UNIT_FLAG_IN_COMBAT: ") + (unitFlagInCombat ? "SET" : "clear"));

    bool isInCombat = bot->IsInCombat();
    ai->TellPlayer(requester, std::string("IsInCombat(): ") + (isInCombat ? "true" : "false"));

    ai->TellPlayer(requester, "CMaNGOS attackers: " + std::to_string(bot->GetAttackers().size()));

    Unit* victim = bot->GetVictim();
    ai->TellPlayer(requester, std::string("victim: ") + (victim ? victim->GetName() : "none"));

    ai->TellPlayer(requester, "--- BotAI State ---");

    Unit* aiTarget = *ai->GetAiObjectContext()->GetValue<Unit*>("current target");
    if (aiTarget)
    {
        std::ostringstream targetOut;
        targetOut << "current target: " << aiTarget->GetName()
                  << " (" << aiTarget->getObjectGuid().GetCounter() << ")";
        ai->TellPlayer(requester, targetOut.str());

        bool isInvalid = ai->GetAiObjectContext()->GetValue<bool>("invalid target", "current target")->Get();
        ai->TellPlayer(requester, std::string("invalid: ") + (isInvalid ? "YES" : "no"));
    }
    else
    {
        ai->TellPlayer(requester, "current target: none");
    }

    bool hasAttackers = ai->GetAiObjectContext()->GetValue<bool>("has attackers")->Get();
    ai->TellPlayer(requester, std::string("has attackers: ") + (hasAttackers ? "true" : "false"));

    ai->TellPlayer(requester, "Selection: " + std::to_string(bot->GetSelectionGuid().GetCounter()));

    return true;
}

bool DebugAction::HandleActivity(Event& event, Player* requester, const std::string& text)
{
    std::string action = ai->HandleRemoteCommand("action");

    bool isActive = ai->AllowActivity();

    std::string stateName;
    switch (ai->GetState())
    {
        case BotState::BOT_STATE_COMBAT: stateName = "combat"; break;
        case BotState::BOT_STATE_NON_COMBAT: stateName = "non-combat"; break;
        case BotState::BOT_STATE_DEAD: stateName = "dead"; break;
        case BotState::BOT_STATE_REACTION: stateName = "reaction"; break;
        default: stateName = "unknown"; break;
    }

    std::string priorityTypeName;
    switch (ai->GetPriorityType())
    {
        case ActivePiorityType::IS_REAL_PLAYER: priorityTypeName = "real_player"; break;
        case ActivePiorityType::HAS_REAL_PLAYER_MASTER: priorityTypeName = "has_master"; break;
        case ActivePiorityType::IN_GROUP_WITH_REAL_PLAYER: priorityTypeName = "group_with_player"; break;
        case ActivePiorityType::IN_BATTLEGROUND: priorityTypeName = "battleground"; break;
        case ActivePiorityType::IN_INSTANCE: priorityTypeName = "instance"; break;
        case ActivePiorityType::VISIBLE_FOR_PLAYER: priorityTypeName = "visible_for_player"; break;
        case ActivePiorityType::IS_ALWAYS_ACTIVE: priorityTypeName = "always_active"; break;
        case ActivePiorityType::IN_COMBAT: priorityTypeName = "combat"; break;
        case ActivePiorityType::IN_BG_QUEUE: priorityTypeName = "bg_queue"; break;
        case ActivePiorityType::NEARBY_PLAYER: priorityTypeName = "nearby_player"; break;
        case ActivePiorityType::PLAYER_FRIEND: priorityTypeName = "friend"; break;
        case ActivePiorityType::PLAYER_GUILD: priorityTypeName = "guild"; break;
        case ActivePiorityType::NO_PATH: priorityTypeName = "no_path"; break;
        case ActivePiorityType::IN_ACTIVE_AREA: priorityTypeName = "active_area"; break;
        case ActivePiorityType::IN_ACTIVE_MAP: priorityTypeName = "active_map"; break;
        case ActivePiorityType::IN_INACTIVE_MAP: priorityTypeName = "inactive_map"; break;
        case ActivePiorityType::IN_EMPTY_SERVER: priorityTypeName = "empty_server"; break;
        default: priorityTypeName = "unknown"; break;
    }

    auto bracket = ai->GetPriorityBracket(ai->GetPriorityType());
    constexpr float activityPct = 100.0f;

    std::ostringstream out;
    out << "State: " << stateName << ", Active: " << (isActive ? "yes" : "no");
    out << ", PriorityType: " << priorityTypeName;
    out << ", Bracket: " << bracket.first << "-" << bracket.second;
    out << ", Activity%: " << std::fixed << std::setprecision(1) << activityPct;
    if (!action.empty())
        out << ", Action: " << action;

    ai->TellPlayer(requester, out.str());
    return true;
}
