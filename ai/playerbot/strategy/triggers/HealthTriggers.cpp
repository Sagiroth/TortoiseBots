
#include "playerbot/playerbot.h"
#include "HealthTriggers.h"
#include "playerbot/HealingCastPolicy.h"

using namespace ai;

float HealthInRangeTrigger::GetValue()
{
    return AI_VALUE2(uint8, "health", GetTargetName());
}

bool HealthInRangeTrigger::IsActive()
{
    bool healthCheck = false;
    if (ai->HasStrategy("preheal", BotState::BOT_STATE_COMBAT))
    {
        Unit* target = GetTarget();
        if (target)
        {
            uint32 incDmg = AI_VALUE2(uint32, "incoming damage", GetTargetName());
            if (incDmg)
            {
                float healthPercent = GetValue();
                float incDmgPercent = float(float(incDmg) / float(target->GetMaxHealth())) * 100;
                float healthPredict = healthPercent;
                if (incDmgPercent >= healthPercent)
                    healthPredict = 0.f;
                else
                    healthPredict = healthPercent - incDmgPercent;

                healthCheck = healthPredict < maxValue && healthPredict >= minValue;
                if (healthCheck && ai->HasStrategy("debug", BotState::BOT_STATE_NON_COMBAT))
                {
                    std::string msg = GetTargetName() + " hp: " + std::to_string(healthPercent) + ", predicted: " + std::to_string(healthPredict);
                    ai->TellPlayerNoFacing(GetMaster(), msg);
                }
            }
            else
                healthCheck = ValueInRangeTrigger::IsActive();
        }
    }
    else
        healthCheck = ValueInRangeTrigger::IsActive();

    return healthCheck
        && !AI_VALUE2(bool, "dead", GetTargetName())
        && (!isTankRequired || (GetTarget()->IsPlayer() && ai->IsTank((Player*)GetTarget(), false)));
}

// This is the top-up tier - a party member between mediumHealth and
// almostFullHealth, so between 70 and 90 percent by default. It is the tier that
// fires most often by a wide margin, and a heal cast at 85 percent costs the same
// mana as the one that saves somebody at 20. Once the healer's own mana is no
// longer comfortable, that trade stops being worth making: skip the tier and keep
// what is left for the ones below it, which are deliberately untouched and will
// still fire at any mana level.
bool PartyMemberAlmostFullHealthTrigger::IsActive()
{
    if (AI_VALUE2(uint8, "mana", "self target") < sPlayerbotAIConfig.mediumMana)
        return false;

    return PartyMemberLowHealthTrigger::IsActive();
}

bool PartyMemberDeadTrigger::IsActive()
{
	return GetTarget();
}

bool CombatPartyMemberDeadTrigger::IsActive()
{
    return GetTarget();
}

bool DeadTrigger::IsActive()
{
    return AI_VALUE2(bool, "dead", GetTargetName());
}

bool AoeHealTrigger::IsActive()
{
    return AI_VALUE2(uint8, "aoe heal", type) >= count;
}

bool HealTargetFullHealthTrigger::IsActive()
{
    Spell* currentSpell = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    // Native Spell::Update counts down normal cast bars in PREPARING. CASTING
    // is the channel phase, after mana was spent; the old check missed normal heals.
    if (!currentSpell || currentSpell->getState() != SPELL_STATE_PREPARING || !currentSpell->GetCastedTime())
        return false;

    SpellEntry const* info = currentSpell->m_spellInfo;
    if (!info || info->IsChanneledSpell() || info->IsAreaOfEffectSpell())
        return false;

    bool directHeal = false;
    for (uint32 effect = 0; effect < MAX_EFFECT_INDEX; ++effect)
    {
        if (!info->Effect[effect])
            continue;
        if (info->EffectChainTarget[effect] > 1 ||
            (info->Effect[effect] != SPELL_EFFECT_HEAL &&
             info->Effect[effect] != SPELL_EFFECT_HEAL_MAX_HEALTH &&
             info->Effect[effect] != SPELL_EFFECT_HEAL_MECHANICAL))
            return false;
        directHeal = true;
    }
    // Hybrid heals such as Regrowth may still supply a wanted HoT at full health.
    if (!directHeal)
        return false;

    Unit* target = currentSpell->m_targets.getUnitTarget();
    if (!target || !target->IsInWorld() || !target->IsAlive() || target->GetMap() != bot->GetMap())
        return false;

    if (target->GetHealthPercent() <= 90.0f)
        return false;

    uint64 incomingDamage = 0;
    if (ai->HasStrategy("preheal", BotState::BOT_STATE_COMBAT))
    {
        // Use the actual in-flight target, not the selector's next cached target.
        for (Unit* attacker : target->GetAttackers())
            if (attacker->CanReachWithMeleeAutoAttack(target))
                incomingDamage += uint32((attacker->GetFloatValue(UNIT_FIELD_MINDAMAGE) +
                    attacker->GetFloatValue(UNIT_FIELD_MAXDAMAGE)) / 2);
    }

    uint64 healValue = 0;
    if (target->GetHealth() < target->GetMaxHealth() || incomingDamage)
    {
        for (uint32 effect = 0; effect < MAX_EFFECT_INDEX; ++effect)
        {
            if (!info->Effect[effect])
                continue;
            int32 const amount = currentSpell->CalculateDamage(SpellEffectIndex(effect), target);
            if (amount > 0)
                healValue += static_cast<uint32>(amount);
        }
    }

    HealingCastState const cast{true, directHeal, currentSpell->GetCastedTime(),
        target->GetHealth(), target->GetMaxHealth(), incomingDamage, healValue};
    if (!ShouldCancelWastefulHeal(cast))
        return false;
    if (ai->HasStrategy("debug", BotState::BOT_STATE_NON_COMBAT))
    {
        std::string msg = "target healed, can save " + std::to_string(currentSpell->GetPowerCost()) +
            " mana, cast left: " + std::to_string(currentSpell->GetCastedTime()) + "ms";
        ai->TellPlayerNoFacing(GetMaster(), msg);
    }
    return true;
}
