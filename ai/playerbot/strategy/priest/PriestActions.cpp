
#include "playerbot/playerbot.h"
#include "PriestActions.h"
#include "playerbot/strategy/values/HealerCoverage.h"
#include "playerbot/WandPolicy.h"

using namespace ai;

bool ai::PriestShadowformHealGateOpen(PlayerbotAI* ai, std::string const& spell, Unit* target)
{
    Player* bot = ai->GetBot();
    if (!ai->HasAura("shadowform", bot))
        return true;
    if (!target)
        return false;

    uint32 spellId = ai->GetAiObjectContext()->GetValue<uint32>("spell id", spell)->Get();
    SpellEntry const* spellInfo = spellId ? sServerFacade.LookupSpellInfo(spellId) : nullptr;
    if (!spellInfo)
        return false;

    SpellCastResult result = SPELL_CAST_OK;
    bool const castable = ai->CanCastSpell(spell, target, 0, nullptr, true, false, false, &result);
    SpellCastResult const formError = spellInfo->GetErrorAtShapeshiftedCast(bot->GetShapeshiftForm());
    bool const formOnly = !castable && result == formError && formError != SPELL_CAST_OK;
    FormHealProbe const probe = ClassifyFormHealProbe(castable, formOnly);

    bool blocked = false;
    if (spellInfo->PreventionType == SPELL_PREVENTION_TYPE_SILENCE &&
        (bot->GetUInt32Value(UNIT_FIELD_FLAGS) & UNIT_FLAG_SILENCED) != 0)
        blocked = true;
    if (ai->SpellGlobalCooldownRemainingMs() > 0)
        blocked = true;
    Spell* autoRepeat = bot->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL);
    if (autoRepeat && autoRepeat->m_spellInfo && autoRepeat->m_spellInfo->Category == WAND_SHOOT_CATEGORY &&
        bot->GetAttackTimer(RANGED_ATTACK) > 0)
        blocked = true;

    return ShadowformHealGateOpen(true, probe, blocked);
}

bool CastRenewOnPartyAction::isUseful()
{
    Player* bot = ai->GetBot();
    if (ai->HasAura("shadowform", bot) && !ai->ContainsStrategy(STRATEGY_TYPE_HEAL))
    {
        Unit* target = GetTarget();
        if (target && target->GetHealthPercent() >= sPlayerbotAIConfig.lowHealth &&
            AnotherHealerCoversAlly(ai, target, HealReachLimit(ai)))
            return false;
    }
    return PriestFormGatedHeal<CastRenewOnPartyActionBase>::isUseful();
}
