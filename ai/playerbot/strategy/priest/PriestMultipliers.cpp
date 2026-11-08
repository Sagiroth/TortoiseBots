
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
    // The mana cost of each known Heal rank 2+, priced by the core (Spell::CalculatePowerCost, as the cast probe
    // does). Not the full cast probe: it also fails on a cast in progress and on a cooldown, which let Lesser Heal
    // through while Heal was merely busy (walk 55, Durgrim).
    uint32 costs[sizeof(kHealRank2Plus) / sizeof(kHealRank2Plus[0])];
    size_t known = 0;
    for (uint32 spellId : kHealRank2Plus)
    {
        if (!bot->HasSpell(spellId))
            continue;
        SpellEntry const* spellInfo = sServerFacade.LookupSpellInfo(spellId);
        if (spellInfo)
            costs[known++] = Spell::CalculatePowerCost(spellInfo, bot, nullptr, nullptr);
    }
    if (!known)
        return false;
    return PriestSuppressLesserHeal(true, PriestAnyHealRankAffordable(costs, known, bot->GetPower(POWER_MANA)));
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
