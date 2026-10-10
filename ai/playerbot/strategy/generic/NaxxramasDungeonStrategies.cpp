
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
