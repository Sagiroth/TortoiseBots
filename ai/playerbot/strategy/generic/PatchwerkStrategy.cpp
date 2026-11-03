#include "playerbot/playerbot.h"
#include "PatchwerkStrategy.h"
#include "playerbot/strategy/actions/PatchwerkSoakerActions.h"
#include "playerbot/strategy/actions/AttackAction.h"
#include "playerbot/strategy/actions/ReachTargetActions.h"
#include "playerbot/EbonrocSwapPolicy.h"

using namespace ai;

namespace
{
class PatchwerkAssignmentMultiplier : public Multiplier
{
public:
    PatchwerkAssignmentMultiplier(PlayerbotAI* ai) : Multiplier(ai, "patchwerk assignment") {}
    float GetValue(Action* action) override
    {
        if (!action)
            return 1.0f;
        std::string const name = action->getName();
        // Suspend only the saved movement orders, without deleting them or
        // their anchors. They resume after Patchwerk dies/evades or opt-out.
        if ((name == "stay" || name == "follow" || name == "return to stay position" || name == "sit") &&
            GetPatchwerkSoakBoss(ai))
            return 0.0f;
        bool const taunt = EbonrocTauntAction(name);
        bool const approach = dynamic_cast<MovementAction*>(action) ||
            dynamic_cast<CastReachTargetSpellAction*>(action) || dynamic_cast<AttackAction*>(action);
        if (!taunt && !approach)
            return 1.0f;
        Unit* boss = GetPatchwerkBoss(ai);
        if (!boss)
            return 1.0f;
        // Soakers never take Patchwerk off the main tank.
        if (taunt && PatchwerkSoakerAssigned(ai))
        {
            bool const aoe = name == "challenging shout" || name == "challenging roar";
            if (aoe || action->GetTarget() == boss ||
                (name == "righteous defense" && action->GetTarget() == boss->GetVictim()))
                return 0.0f;
        }
        bool const escape = name == "patchwerk melee retreat" || name == "move away from hazard" ||
            name == "raid bomb runout" || name == "flee" || name == "runaway";
        return approach && !escape && PatchwerkNeedsMeleeHold(ai, boss) ? 0.0f : 1.0f;
    }
};
}

void PatchwerkAssignmentStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    if (name == "patchwerk melee safety")
        triggers.push_back(new TriggerNode("patchwerk melee unsafe",
            NextAction::array(0, new NextAction("patchwerk melee retreat", ACTION_EMERGENCY + 7), NULL)));
}

void PatchwerkAssignmentStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    InitCombatTriggers(triggers);
}

void PatchwerkAssignmentStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new PatchwerkAssignmentMultiplier(ai));
}

void PatchwerkAssignmentStrategy::InitReactionMultipliers(std::list<Multiplier*>& multipliers)
{
    InitCombatMultipliers(multipliers);
}
