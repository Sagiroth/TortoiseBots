#include "playerbot/playerbot.h"
#include "KriCloudStrategy.h"
#include "playerbot/strategy/actions/ReachTargetActions.h"

using namespace ai;

namespace
{
    class KriCloudMultiplier : public Multiplier
    {
    public:
        explicit KriCloudMultiplier(PlayerbotAI* ai) : Multiplier(ai, "kri cloud movement") {}
        float GetValue(Action* action) override
        {
            if (!action)
                return 1.0f;
            std::string const& name = action->getName();
            bool const escape = name == "kri cloud escape" || name == "move away from hazard" ||
                name == "raid bomb runout" || name == "flee" || name == "runaway";
            if (escape)
                return 1.0f;
            bool const movement = dynamic_cast<MovementAction*>(action) ||
                dynamic_cast<CastReachTargetSpellAction*>(action);
            return movement && KriCloudTooClose(ai) ? 0.0f : 1.0f;
        }
    };
}

void KriCloudStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("kri cloud nearby",
        NextAction::array(0, new NextAction("kri cloud escape", ACTION_EMERGENCY + 7), nullptr)));
}

void KriCloudStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    InitCombatTriggers(triggers);
}

void KriCloudStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new KriCloudMultiplier(ai));
}

void KriCloudStrategy::InitReactionMultipliers(std::list<Multiplier*>& multipliers)
{
    InitCombatMultipliers(multipliers);
}

void KriCloudStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    InitCombatTriggers(triggers);
}

void KriCloudStrategy::InitNonCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    InitCombatMultipliers(multipliers);
}
