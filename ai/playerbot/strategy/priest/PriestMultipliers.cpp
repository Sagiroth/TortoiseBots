
#include "playerbot/playerbot.h"
#include "PriestMultipliers.h"
#include "PriestActions.h"

using namespace ai;

namespace
{
    // Heal ranks 2-4 (levels 22, 28, 34); rank 1 stays on Lesser Heal's downranking ladder.
    constexpr uint32 kHealRank2Plus[] = {2055, 6063, 6064};
}

bool ai::PriestShouldSuppressLesserHeal(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    if (!bot)
        return false;
    bool knowsRank2Plus = false;
    for (uint32 spellId : kHealRank2Plus)
        knowsRank2Plus = knowsRank2Plus || bot->HasSpell(spellId);
    if (!knowsRank2Plus)
        return false;
    // Price the rank the "heal" action probes: the "spell id" value resolves "heal" to the top known rank, and
    // "heal on party" clears the rank override, so that is the spell CanCastSpell("heal") checks the mana of
    // (GenericSpellActions.cpp isUseful/isPossible). The cost is the core's price without a spell object, as the cast
    // probe prices it. Mana only: a cast in progress or a cooldown on Heal no longer lets Lesser Heal through (walk 55,
    // Durgrim), but a priest that cannot pay the probed rank keeps Lesser Heal as its low-mana fallback.
    uint32 const probedId = ai->GetAiObjectContext()->GetValue<uint32>("spell id", "heal")->Get();
    SpellEntry const* spellInfo = probedId ? sServerFacade.LookupSpellInfo(probedId) : nullptr;
    if (!spellInfo)
        return false;
    uint32 const cost = Spell::CalculatePowerCost(spellInfo, bot, nullptr, nullptr);
    return PriestSuppressLesserHeal(true, PriestProbedHealAffordable(cost, bot->GetPower(POWER_MANA)));
}

float PriestLesserHealMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;
    std::string const& actionName = action->getName();
    if (actionName != "lesser heal" && actionName != "lesser heal on party")
        return 1.0f;
    return PriestShouldSuppressLesserHeal(ai) ? 0.0f : 1.0f;
}
