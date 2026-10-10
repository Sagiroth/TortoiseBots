
#include "playerbot/playerbot.h"
#include "Trigger.h"
#include "Action.h"
#include "Objects/Unit.h"
#include "Value.h"

using namespace ai;

#include "playerbot/ForceRebuffPolicy.h"

bool Trigger::BypassIntervalForRebuff()
{
    if (!ai || !bot || bot->IsInCombat())
        return false;
    AiObjectContext* rebuffContext = ai->GetAiObjectContext();
    if (!rebuffContext)
        return false;
    uint32 beginMs = rebuffContext->GetValue<uint32>("manual int", "force rebuff begin ms")->Get();
    return beginMs && ai::ForceRebuffPending(beginMs, WorldTimer::getMSTime());
}

TriggerNode::TriggerNode(std::string name, std::initializer_list<NextAction> handlers)
    : name(std::move(name)), trigger(NULL), handlers(NextAction::array(handlers))
{
}

Event Trigger::Check()
{
	if (triggered)
	{
		if (owner)
			return Event(getName(), param, owner);
		else
			return Event(getName());
	}

	if (IsActive())
	{
		triggered = true;
		Event event(getName());
		return event;
	}
	Event event;
	return event;
}

Value<Unit*>* Trigger::GetTargetValue()
{
    return context->GetValue<Unit*>(GetTargetName());
}

Unit* Trigger::GetTarget()
{
    return GetTargetValue()->Get();
}

TriggerNode::~TriggerNode()
{
	NextAction::destroy(handlers);
}

NextAction** TriggerNode::getHandlers()
{
	return NextAction::merge(NextAction::clone(handlers), trigger->getHandlers());
}

float TriggerNode::getFirstRelevance()
{
	return handlers[0] ? handlers[0]->getRelevance() : -1;
}
