
#include "playerbot/playerbot.h"
#include "CastTimeStrategy.h"

#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/actions/GenericSpellActions.h"

using namespace ai;

float CastTimeMultiplier::GetValue(Action* action)
{
    if (action == NULL) return 1.0f;

    std::string name = action->GetName();

    if (action->GetTarget() != AI_VALUE(Unit*, "current target"))
        return 1.0f;

    // mod-playerbots CastTimeMultiplier (LD-7): veto a cast the target will
    // not live to see — cast time vs health / estimated group dps. The old
    // HP% + cast-time ladder only fired below critical health, so a 3 s cast
    // on a 500-HP mob at full health still started and fizzled on a corpse.
    // Lifetime covers the old gate: low HP means short lifetime.
    if (dynamic_cast<CastSpellAction*>(action))
    {
        uint32 spellId = AI_VALUE2(uint32, "spell id", name);
        const SpellEntry* const pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
        if (!pSpellInfo) return 1.0f;

        if (spellId && pSpellInfo->Targets & TARGET_FLAG_DEST_LOCATION)
            return 1.0f;
        else if (spellId && pSpellInfo->Targets & TARGET_FLAG_SOURCE_LOCATION)
            return 1.0f;

        uint32 castTime = GetSpellCastTime(pSpellInfo, bot);
        if (IsChanneledSpell(pSpellInfo))
        {
            int32 duration = std::min(GetSpellDuration(pSpellInfo), (int32)3000);
            if (duration > 0)
                castTime += duration;
        }

        Unit* target = action->GetTarget();
        if (!target || !target->IsAlive())
            return 1.0f;

        float groupDps = AI_VALUE(float, "estimated group dps");
        if (groupDps > 0.0f &&
            castTime > IN_MILLISECONDS * (float)target->GetHealth() / groupDps)
            return 0.1f;
    }

    return 1.0f;
}

void CastTimeStrategy::InitCombatMultipliers(std::list<Multiplier*> &multipliers)
{
    multipliers.push_back(new CastTimeMultiplier(ai));
}
