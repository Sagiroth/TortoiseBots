#include "playerbot/playerbot.h"
#include "SarturaWhirlwindStrategy.h"
#include "playerbot/strategy/actions/ReachTargetActions.h"

using namespace ai;

namespace
{
    class SarturaWhirlwindMultiplier : public Multiplier
    {
    public:
        explicit SarturaWhirlwindMultiplier(PlayerbotAI* ai) : Multiplier(ai, "sartura whirlwind movement") {}
        float GetValue(Action* action) override
        {
            if (!action)
                return 1.0f;
            std::string const& name = action->getName();
            bool const escape = name == "avoid aoe" || name == "sartura whirlwind escape" || name == "move away from hazard" ||
                name == "raid bomb runout" || name == "flee" || name == "runaway";
            if (escape)
                return 1.0f;
            bool const movement = dynamic_cast<MovementAction*>(action) ||
                dynamic_cast<CastReachTargetSpellAction*>(action);
            return movement && SarturaWhirlwindTooClose(ai) ? 0.0f : 1.0f;
        }
    };
}

void SarturaWhirlwindStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("sartura whirlwind nearby",
        NextAction::array(0, new NextAction("sartura whirlwind escape", ACTION_EMERGENCY + 7), nullptr)));
}

void SarturaWhirlwindStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    InitCombatTriggers(triggers);
}

void SarturaWhirlwindStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new SarturaWhirlwindMultiplier(ai));
}

void SarturaWhirlwindStrategy::InitReactionMultipliers(std::list<Multiplier*>& multipliers)
{
    InitCombatMultipliers(multipliers);
}
