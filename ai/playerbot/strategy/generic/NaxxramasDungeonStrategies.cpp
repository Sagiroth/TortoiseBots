
#include "playerbot/playerbot.h"
#include "NaxxramasDungeonStrategies.h"
#include "DungeonMultipliers.h"

using namespace ai;

void NaxxramasDungeonStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
	triggers.push_back(new TriggerNode(
		"start four horseman fight",
		NextAction::array(0, new NextAction("enable four horseman fight strategy", 100.0f), NULL)));

	triggers.push_back(new TriggerNode(
		"start gluth fight",
		NextAction::array(0, new NextAction("enable gluth fight strategy", 100.0f), NULL)));

	triggers.push_back(new TriggerNode(
		"start thaddius fight",
		NextAction::array(0, new NextAction("enable thaddius fight strategy", 100.0f), NULL)));

	triggers.push_back(new TriggerNode(
		"start kel'thuzad fight",
		NextAction::array(0, new NextAction("enable kel'thuzad fight strategy", 100.0f), NULL)));

	triggers.push_back(new TriggerNode(
		"start grobbulus fight",
		NextAction::array(0, new NextAction("enable grobbulus fight strategy", 100.0f), NULL)));

	triggers.push_back(new TriggerNode(
		"start loatheb fight",
		NextAction::array(0, new NextAction("enable loatheb fight strategy", 100.0f), NULL)));

	triggers.push_back(new TriggerNode(
		"start sapphiron fight",
		NextAction::array(0, new NextAction("enable sapphiron fight strategy", 100.0f), NULL)));

	triggers.push_back(new TriggerNode(
		"start razuvious fight",
		NextAction::array(0, new NextAction("enable razuvious fight strategy", 100.0f), NULL)));

	triggers.push_back(new TriggerNode(
		"start heigan fight",
		NextAction::array(0, new NextAction("enable heigan fight strategy", 100.0f), NULL)));

	triggers.push_back(new TriggerNode(
		"start anub'rekhan fight",
		NextAction::array(0, new NextAction("enable anub'rekhan fight strategy", 100.0f), NULL)));
}

void FourHorsemanFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
	triggers.push_back(new TriggerNode(
		"void zone too close",
		NextAction::array(0, new NextAction("move away from void zone", 100.0f), NULL)));
}

void FourHorsemanFightStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
	triggers.push_back(new TriggerNode(
		"end four horseman fight",
		NextAction::array(0, new NextAction("disable four horseman fight strategy", 100.0f), NULL)));
}

void FourHorsemanFightStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
	triggers.push_back(new TriggerNode(
		"end four horseman fight",
		NextAction::array(0, new NextAction("disable four horseman fight strategy", 100.0f), NULL)));
}

void MaexxnaRescueStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("maexxna rescue needed",
        NextAction::array(0, new NextAction("maexxna rescue", 80.0f), nullptr)));
}

#include "playerbot/LocustSwarmPolicy.h"
#include "playerbot/strategy/actions/NaxxramasDungeonActions.h"
#include "playerbot/strategy/actions/ReachTargetActions.h"
namespace
{
class LocustSwarmMultiplier : public Multiplier
{
public:
    LocustSwarmMultiplier(PlayerbotAI* ai) : Multiplier(ai, "locust swarm movement") {}
    float GetValue(Action* action) override
    {
        if (!action || !GetActiveLocustSwarm(ai))
            return 1.0f;
        std::string const& name = action->getName();
        bool const escape = name == "locust swarm retreat" || name == "raid bomb runout" ||
            name == "move away from hazard";
        return LocustSwarmBlocksAction(dynamic_cast<MovementAction*>(action) != nullptr,
            dynamic_cast<CastReachTargetSpellAction*>(action) != nullptr, escape) ? 0.0f : 1.0f;
    }
};
}

void LocustSwarmRetreatStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("locust swarm active",
        NextAction::array(0, new NextAction("locust swarm retreat", ACTION_EMERGENCY + 7), NULL)));
}

void LocustSwarmRetreatStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    InitCombatTriggers(triggers);
}

void LocustSwarmRetreatStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new LocustSwarmMultiplier(ai));
}

void LocustSwarmRetreatStrategy::InitReactionMultipliers(std::list<Multiplier*>& multipliers)
{
    InitCombatMultipliers(multipliers);
}