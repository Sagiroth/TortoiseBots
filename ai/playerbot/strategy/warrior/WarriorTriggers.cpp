
#include "playerbot/playerbot.h"
#include "WarriorTriggers.h"
#include "WarriorActions.h"

using namespace ai;

bool BloodrageBuffTrigger::IsActive()
{
    return AI_VALUE2(uint8, "health", "self target") >= sPlayerbotAIConfig.mediumHealth &&
        AI_VALUE2(uint8, "rage", "self target") < 20;
}

bool SunderArmorDebuffTrigger::IsActive()
{
    Unit* target = GetTarget();
    if (!target || !target->IsAlive())
        return false;

    if (bot->GetPower(POWER_RAGE) < 15)
        return false;

    Aura* aura = ai->GetAura("sunder armor", target);
    if (!aura || aura->GetStackAmount() < 5 || aura->GetAuraDuration() <= 6000)
        return !HasMaxDebuffs();

    return false;
}
