#include "playerbot/playerbot.h"
#include "ServiceNearbyNpcAction.h"
#include "playerbot/strategy/values/MaintenanceValues.h"

using namespace ai;

bool ServiceNearbyNpcAction::isUseful()
{
    return AI_VALUE(bool, "should service nearby npc");
}

bool ServiceNearbyNpcAction::Execute(Event& event)
{
    GuidPosition target = AI_VALUE(GuidPosition, "nearby service target");

    if (!target)
        return false;

    Creature* npc = target.GetCreature(bot->GetInstanceId());

    if (!npc || !npc->IsAlive())
        return false;

    if (!bot->IsWithinDistInMap(npc, INTERACTION_DISTANCE))
        return MoveNear(npc, INTERACTION_DISTANCE - 1.0f);

    // In range. The selector ranked this NPC in the same order the checks below
    // run - hand in, accept, sell, train (NearbyServicePolicy.h) - so the verb
    // here is the one the rule picked the NPC for. A finished quest comes first:
    // the reward item and its XP are the only organic gear a low-level bot gets,
    // and the hand-in frees the log slot the next accept needs.
    if (HasRewardableFinishedQuest(ai) && AI_VALUE2(bool, "can turn in quest npc", target.GetEntry()))
    {
        WorldPacket p(CMSG_QUESTGIVER_COMPLETE_QUEST);
        p << (ObjectGuid)target;
        p.rpos(0);

        return RunVerb("turn in quest", "talk to quest giver", Event("rpg action", p), target.GetEntry());
    }

    if (AI_VALUE(uint8, "free quest log slots") > 0 && AI_VALUE2(bool, "can accept quest npc", target.GetEntry()))
    {
        WorldPacket p(CMSG_QUESTGIVER_ACCEPT_QUEST);
        p << (ObjectGuid)target;
        p.rpos(0);

        return RunVerb("accept quest", "accept all quests", Event("rpg action", p), target.GetEntry());
    }

    // The selector prefers a vendor whenever a sale is due, so a vendor here
    // means "sell first"; the trainer is then picked up on a later tick, once the
    // sale has funded the rank.
    if (target.HasNpcFlag(UNIT_NPC_FLAG_VENDOR))
        return RunVerb("sell", "sell", Event("rpg action", "vendor"), target.GetEntry());

    return RunVerb("trainer", "trainer", Event("rpg action", target), target.GetEntry());
}

// Runs the verb through the existing action and records the rule firing. Only a
// verb that did something is logged (the outcomes land as their own events:
// QuestRewarded, AcceptQuestAction, SellAction, TrainerAction), so a bot that
// keeps finding nothing here does not fill bot_events.csv every tick.
bool ServiceNearbyNpcAction::RunVerb(std::string const& kind, std::string const& action, Event event, uint32 npcEntry)
{
    bool const done = ai->DoSpecificAction(action, event, true);

    if (done)
        sPlayerbotAIConfig.logEvent(ai, "NearbyService", kind, std::to_string(npcEntry));

    return done;
}
