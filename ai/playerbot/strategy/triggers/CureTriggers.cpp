
#include "playerbot/playerbot.h"
#include "GenericTriggers.h"
#include "CureTriggers.h"
#include "playerbot/strategy/actions/WorldBuffAction.h"
#include "playerbot/strategy/RaidCurePolicy.h"

using namespace ai;

bool NeedCureTrigger::IsActive()
{
	Unit* target = GetTarget();
	return target && !IsRaidCureInterlocked(target->GetMapId(), target->HasAura(MUTATING_INJECTION_SPELL_ID)) &&
		ai->HasAuraToDispel(target, dispelType);
}

Value<Unit*>* PartyMemberNeedCureTrigger::GetTargetValue()
{
	return context->GetValue<Unit*>("party member to dispel", dispelType);
}

bool NeedWorldBuffTrigger::IsActive()
{
    return !WorldBuffAction::NeedWorldBuffs(bot).empty();
}
