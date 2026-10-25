#include "playerbot/playerbot.h"
#include "CthunSpreadStrategy.h"
#include "playerbot/strategy/actions/ReachTargetActions.h"
#include "playerbot/strategy/actions/AttackAction.h"
using namespace ai;
namespace
{
class CthunSpreadMultiplier : public Multiplier
{
public:
    explicit CthunSpreadMultiplier(PlayerbotAI* ai) : Multiplier(ai,"cthun sector position") {}
    float GetValue(Action* action) override
    {
        if (!action || !CthunSpreadEnabled(ai)) return 1;
        std::string const name=action->getName();
        if(name=="cthun spread move" || name=="move away from hazard" ||
            name=="set facing" || name=="flee" || name=="runaway" || dynamic_cast<AttackAction*>(action)) return 1;
        CthunPoint point;
        auto* spread=static_cast<CthunSpreadAction*>(ai->GetAiObjectContext()->GetAction("cthun spread move"));
        if(!spread->CanHoldPosition() || !spread->Assigned(point)) return 1; // No valid duty-preserving spot: do not strand a healer/tank.
        return dynamic_cast<MovementAction*>(action) || dynamic_cast<CastReachTargetSpellAction*>(action) ? 0 : 1;
    }
};
}
void CthunSpreadStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("cthun spread needed",
        NextAction::array(0,new NextAction("cthun spread move",ACTION_EMERGENCY+2),nullptr)));
}
void CthunSpreadStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& t) {InitCombatTriggers(t);}
void CthunSpreadStrategy::InitReactionTriggers(std::list<TriggerNode*>& t) {InitCombatTriggers(t);}
void CthunSpreadStrategy::InitCombatMultipliers(std::list<Multiplier*>& m) {m.push_back(new CthunSpreadMultiplier(ai));}
void CthunSpreadStrategy::InitNonCombatMultipliers(std::list<Multiplier*>& m) {InitCombatMultipliers(m);}
void CthunSpreadStrategy::InitReactionMultipliers(std::list<Multiplier*>& m) {InitCombatMultipliers(m);}
