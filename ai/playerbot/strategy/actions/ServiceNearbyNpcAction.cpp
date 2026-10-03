#include "playerbot/playerbot.h"
#include "ServiceNearbyNpcAction.h"
#include "SellAction.h"
#include "playerbot/strategy/values/MaintenanceValues.h"
#include "playerbot/strategy/actions/AcceptQuestAction.h"
#include "playerbot/strategy/triggers/RpgTriggers.h"
using namespace ai;

bool ServiceNearbyNpcAction::isUseful()
{
    return AI_VALUE(bool, "should service nearby npc");
}

bool ServiceNearbyNpcAction::Execute(Event& event)
{
    GuidPosition target = AI_VALUE(GuidPosition, "nearby service target");

    if (!target)
    {
        // Stale target (despawned, wandered off, verbs parked): the trigger
        // cache still offers it for up to 5 s, and every tick failed at full
        // speed (ACTION_LOOP). Reset the caches so the next tick re-evaluates
        // instead of failing again on the same stale answer.
        RESET_AI_VALUE(GuidPosition, "nearby service target");
        RESET_AI_VALUE(bool, "should service nearby npc");
        return false;
    }

    Creature* npc = target.GetCreature(bot->GetInstanceId());

    if (!npc || !npc->IsAlive())
    {
        RESET_AI_VALUE(GuidPosition, "nearby service target");
        RESET_AI_VALUE(bool, "should service nearby npc");
        return false;
    }

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
    // report failure so the engine does not treat the walk as success, but
    // reset the caches first so the re-evaluation (which now sees the parks)
    // stands the trigger down instead of failing at tick speed.
    RESET_AI_VALUE(GuidPosition, "nearby service target");
    RESET_AI_VALUE(bool, "should service nearby npc");
    return false;
}

// One verb attempt: false when the verb does not apply here, is parked, or
// failed; true only when it did something. Failures accumulate in the
// fixed-size per-bot fail parks (load/mutate/store - AI_VALUE returns a copy,
// never per-NPC manual values); a success clears the pair.
bool ServiceNearbyNpcAction::TryVerb(Event& event, GuidPosition target, NearbyServiceKind kind, std::string const& verb)
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

        return RunVerb(npcGuid, verbId, kind, verb, "talk to quest giver", Event("rpg action", p), target, now);
    }

    if (kind == NearbyServiceKind::Accept)
    {
        if (AI_VALUE(uint8, "free quest log slots") == 0 || !AI_VALUE2(bool, "can accept quest npc", target.GetEntry()))
            return false;

        WorldPacket p(CMSG_QUESTGIVER_ACCEPT_QUEST);
        p << (ObjectGuid)target;
        p.rpos(0);

        return RunVerb(npcGuid, verbId, kind, verb, "accept all quests", Event("rpg action", p), target, now);
    }

    if (kind == NearbyServiceKind::Vendor)
    {
        // The selector prefers a vendor whenever a sale is due, so a vendor
        // here means "sell first"; the trainer is then picked up on a later
        // tick, once the sale has funded the rank. #404: a flagged-but-stockless NPC is not
        // a vendor for this verb either (SellAction skips it), so fail here and let the NPC's
        // remaining verbs (quest/trainer) still run.
        if (!target.HasNpcFlag(UNIT_NPC_FLAG_VENDOR))
            return false;
        if (Creature* vendorCreature = target.GetCreature(bot->GetInstanceId()))
        {
            if (!SellAction::HasVendorStock(vendorCreature))
                return false;
        }

        return RunVerb(npcGuid, verbId, kind, verb, "sell", Event("rpg action", "vendor"), target, now);
    }

    return RunVerb(npcGuid, verbId, kind, verb, "trainer", Event("rpg action", target), target, now);
}

// Runs the verb through the existing action and records the rule firing. Only a
// verb that did something is logged (the outcomes land as their own events:
// QuestRewarded, AcceptQuestAction, SellAction, TrainerAction), so a bot that
// keeps finding nothing here does not fill bot_events.csv every tick.
// Failures accumulate in the fixed-size fail parks; a success clears the pair.
// A verb that ran but changed nothing (trainer taught nothing, quest still
// not handed in, nothing accepted) counts as a failure, not a success: the
// verb re-evaluates its own applicability after a done==true run and an
// unchanged answer records a fail. Applicability answers are cached values,
// so Reset() them first - otherwise the post-run read returns the pre-run
// answer and progress is never seen. Vendor needs no re-check: SellAction
// already returns false when it sold nothing, so done==true always moved stock.
bool ServiceNearbyNpcAction::RunVerb(uint64_t npcGuid, int verbId, NearbyServiceKind verbKind,
    std::string const& kind, std::string const& action, Event event, GuidPosition target, time_t now)
{
    bool const done = ai->DoSpecificAction(action, event, true);
    AiObjectContext* context = ai->GetAiObjectContext();
    NearbyServiceFailParks parks = AI_VALUE(NearbyServiceFailParks, "nearby service fail parks");
    uint32 const npcEntry = target.GetEntry();

    bool progressed = done;
    if (done && verbKind != NearbyServiceKind::Vendor)
    {
        if (verbKind == NearbyServiceKind::TurnIn)
        {
            RESET_AI_VALUE2(bool, "can turn in quest npc", npcEntry);
            progressed = NearbyServiceVerbMadeProgress(verbKind,
                HasRewardableFinishedQuest(ai) && AI_VALUE2(bool, "can turn in quest npc", npcEntry));
        }
        else if (verbKind == NearbyServiceKind::Accept)
        {
            RESET_AI_VALUE2(bool, "can accept quest npc", npcEntry);
            Creature* npc = target.GetCreature(bot->GetInstanceId());
            progressed = NearbyServiceVerbMadeProgress(verbKind,
                npc && AcceptAllQuestsAction::OffersAcceptableQuest(ai, bot, npc));
        }
        else
        {
            GuidPosition freshTarget(target);
            progressed = NearbyServiceVerbMadeProgress(verbKind,
                RpgTrainTrigger::TeachesAffordableSpell(ai, freshTarget, bot));
        }
    }

    if (progressed)
    {
        sPlayerbotAIConfig.logEvent(ai, "NearbyService", kind, std::to_string(npcEntry));
        parks.Clear(npcGuid, verbId);
    }
    else
        parks.RecordFail(npcGuid, verbId, now);

    SET_AI_VALUE(NearbyServiceFailParks, "nearby service fail parks", parks);
    return progressed;
}
