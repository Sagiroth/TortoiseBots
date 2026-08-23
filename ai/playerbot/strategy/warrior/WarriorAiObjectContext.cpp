#include "WarriorAiObjectContext.h"
#include "runtime/VerticalSliceObjects.h"
#include "Log.h"

using namespace ai;

WarriorAiObjectContext::WarriorAiObjectContext(PlayerbotAI* ai) : AiObjectContext(ai)
{
    strategyContexts.Add(new WarriorVerticalStrategyContext());
    actionContexts.Add(new WarriorVerticalActionContext());
    triggerContexts.Add(new WarriorVerticalTriggerContext());
    sLog.outString("TortoiseBots AI: real Warrior contexts registered: Arms/Fury/Protection strategies and actions");
}
