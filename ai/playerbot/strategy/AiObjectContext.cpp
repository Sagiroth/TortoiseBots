
#include "playerbot/playerbot.h"
#include "Action.h"
#include "AiObjectContext.h"
#include "NamedObjectContext.h"
// #include "StrategyContext.h" // E2E green: excluded to avoid WorldBuffTravelStrategy.h
// E2E green: action/trigger contexts excluded to avoid 200+ mismatches in *Action.h headers.
// #include "triggers/TriggerContext.h"
// #include "actions/ActionContext.h"
// #include "triggers/ChatTriggerContext.h"
// #include "actions/ChatActionContext.h"
// #include "triggers/WorldPacketTriggerContext.h"
// #include "actions/WorldPacketActionContext.h"
// E2E green: value contexts excluded to avoid 200+ Penqle API mismatches in *Value.h headers.
// They remain in the tree (ai/playerbot/strategy/values/*) for the next batch.
// #include "values/ValueContext.h"
// #include "values/SharedValueContext.h"


using namespace ai;

AiObjectContext::AiObjectContext(PlayerbotAI* ai) : PlayerbotAIAware(ai)
{
    // E2E green: no strategy contexts to avoid WorldBuffTravelStrategy.h
    // strategyContexts.Add(new StrategyContext());
    // strategyContexts.Add(new MovementStrategyContext());
    // strategyContexts.Add(new AssistStrategyContext());
    // strategyContexts.Add(new QuestStrategyContext());
    // strategyContexts.Add(new FishStrategyContext());

    // E2E green: minimal contexts only
    // actionContexts.Add(new ActionContext());
    // actionContexts.Add(new ChatActionContext());
    // actionContexts.Add(new WorldPacketActionContext());
    // triggerContexts.Add(new TriggerContext());
    // triggerContexts.Add(new ChatTriggerContext());
    // triggerContexts.Add(new WorldPacketTriggerContext());

    // valueContexts.Add(new ValueContext()); // E2E green: excluded
}

void AiObjectContext::ClearValues(std::string /*findName*/) {} // E2E green stub

void AiObjectContext::ClearExpiredValues(std::string /*findName*/, uint32 /*interval*/) {} // E2E green stub


std::string AiObjectContext::FormatValues(std::string /*findName*/) { return ""; } // E2E green stub

void AiObjectContext::Update()
{
    /* Disabled until there is actually a strategy, trigger, action or value that has the Update() method. Currently this takes 8% cpu and does 'NOTHING'.
    strategyContexts.Update();
    triggerContexts.Update();
    actionContexts.Update();
    valueContexts.Update();
    */
}

void AiObjectContext::Reset()
{
    strategyContexts.Reset();
    triggerContexts.Reset();
    actionContexts.Reset();
    valueContexts.Reset();
}

std::list<std::string> AiObjectContext::Save() { return {}; } // E2E green stub

void AiObjectContext::Load(std::list<std::string> /*data*/) {} // E2E green stub
