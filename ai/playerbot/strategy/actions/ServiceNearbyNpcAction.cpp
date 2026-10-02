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

    // In range. Verbs run strongest-first - hand in, accept, sell, train
    // (NearbyServicePolicy.h) - and a verb parked after repeated failures is
    // skipped in place, so the NPC's remaining verbs still run (issue #407
    // review: a parked hand-in must never hide a vendor on the same NPC).
    // A finished quest comes first: the reward item and its XP are the only
    // organic gear a low-level bot gets, and the hand-in frees the log slot
    // the next accept needs.
    if (TryVerb(event, target, NearbyServiceKind::TurnIn, "turn in quest") ||
        TryVerb(event, target, NearbyServiceKind::Accept, "accept quest") ||
        TryVerb(event, target, NearbyServiceKind::Vendor, "sell") ||
        TryVerb(event, target, NearbyServiceKind::Trainer, "trainer"))
        return true;

    // In range of a real target but every live verb is parked or inapplicable:
    // report failure so the engine does not treat the walk as success.
    return false;
}

// One verb attempt: false when the verb does not apply here, is parked, or
// failed; true only when it did something. Failures accumulate in the
// fixed-size per-bot fail parks (load/mutate/store - AI_VALUE returns a copy,
// never per-NPC manual values); a success clears the pair.
bool ServiceNearbyNpcAction::TryVerb(Event& event, GuidPosition const& target, NearbyServiceKind kind, std::string const& verb)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    NearbyServiceFailParks parks = AI_VALUE(NearbyServiceFailParks, "nearby service fail parks");
    uint64_t const npcGuid = target.GetRawValue();
    int const verbId = NearbyServiceRankOf(kind);
    time_t const now = time(0);

    if (parks.Parked(npcGuid, verbId, now))
        return false;

    if (kind == NearbyServiceKind::TurnIn)
    {
        if (!HasRewardableFinishedQuest(ai) || !AI_VALUE2(bool, "can turn in quest npc", target.GetEntry()))
            return false;

        WorldPacket p(CMSG_QUESTGIVER_COMPLETE_QUEST);
        p << (ObjectGuid)target;
        p.rpos(0);

        return RunVerb(npcGuid, verbId, verb, "talk to quest giver", Event("rpg action", p), target.GetEntry(), now);
    }

    if (kind == NearbyServiceKind::Accept)
    {
        if (AI_VALUE(uint8, "free quest log slots") == 0 || !AI_VALUE2(bool, "can accept quest npc", target.GetEntry()))
            return false;

        WorldPacket p(CMSG_QUESTGIVER_ACCEPT_QUEST);
        p << (ObjectGuid)target;
        p.rpos(0);

        return RunVerb(npcGuid, verbId, verb, "accept all quests", Event("rpg action", p), target.GetEntry(), now);
    }

    if (kind == NearbyServiceKind::Vendor)
    {
        // The selector prefers a vendor whenever a sale is due, so a vendor
        // here means "sell first"; the trainer is then picked up on a later
        // tick, once the sale has funded the rank.
        if (!target.HasNpcFlag(UNIT_NPC_FLAG_VENDOR))
            return false;

        return RunVerb(npcGuid, verbId, verb, "sell", Event("rpg action", "vendor"), target.GetEntry(), now);
    }

    return RunVerb(npcGuid, verbId, verb, "trainer", Event("rpg action", target), target.GetEntry(), now);
}

// Runs the verb through the existing action and records the rule firing. Only a
// verb that did something is logged (the outcomes land as their own events:
// QuestRewarded, AcceptQuestAction, SellAction, TrainerAction), so a bot that
// keeps finding nothing here does not fill bot_events.csv every tick.
// Failures accumulate in the fixed-size fail parks; a success clears the pair.
bool ServiceNearbyNpcAction::RunVerb(uint64_t npcGuid, int verbId,
    std::string const& kind, std::string const& action, Event event, uint32 npcEntry, time_t now)
{
    bool const done = ai->DoSpecificAction(action, event, true);
    AiObjectContext* context = ai->GetAiObjectContext();
    NearbyServiceFailParks parks = AI_VALUE(NearbyServiceFailParks, "nearby service fail parks");

    if (done)
    {
        sPlayerbotAIConfig.logEvent(ai, "NearbyService", kind, std::to_string(npcEntry));
        parks.Clear(npcGuid, verbId);
    }
    else
        parks.RecordFail(npcGuid, verbId, now);

    SET_AI_VALUE(NearbyServiceFailParks, "nearby service fail parks", parks);
    return done;
}
