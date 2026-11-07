#pragma once

#include <string>

#include "playerbot/strategy/Multiplier.h"
#include "playerbot/policy/class/PriestHealLinePolicy.h"

class PlayerbotAI;

namespace ai
{
    // Bot-state reads for the Lesser Heal suppression: knows Heal rank 2+
    // and could pay for the Heal cast now. Lives in PriestMultipliers.cpp;
    // the multiplier and the wand probe share it so they never disagree.
    bool PriestShouldSuppressLesserHeal(PlayerbotAI* ai);

    class PriestLesserHealMultiplier : public Multiplier
    {
    public:
        PriestLesserHealMultiplier(PlayerbotAI* ai) : Multiplier(ai, "lesser heal") {}
        float GetValue(Action* action) override;
    };
}
