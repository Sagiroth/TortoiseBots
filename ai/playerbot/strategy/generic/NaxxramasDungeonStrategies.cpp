
#include "playerbot/playerbot.h"
#include "NaxxramasDungeonStrategies.h"
#include "DungeonMultipliers.h"
#include "playerbot/strategy/actions/ThaddiusPolarityActions.h"
#include "playerbot/strategy/actions/ReachTargetActions.h"

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
#include "playerbot/strategy/values/GrobbulusCloudHazards.h"
namespace
{
class GrobbulusCloudMultiplier : public Multiplier
{
public:
    GrobbulusCloudMultiplier(PlayerbotAI* ai) : Multiplier(ai,"grobbulus cloud movement") {}
    float GetValue(Action* action) override
    {
        if (!action || !InsideGrobbulusCloud(ai)) return 1.0f;
        std::string const name=action->getName();
        if (name=="grobbulus cloud escape" || name=="raid bomb runout" || name=="move away from hazard") return 1.0f;
        return ShouldHoldThaddiusPolarityPosition(ai) ? 0.0f : 1.0f;
    }
};
}
void GrobbulusCloudStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("grobbulus cloud nearby",
        NextAction::array(0,new NextAction("grobbulus cloud escape",ACTION_EMERGENCY+8),nullptr)));
}
void GrobbulusCloudStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers) { InitCombatTriggers(triggers); }
void GrobbulusCloudStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers) { InitCombatTriggers(triggers); }
void GrobbulusCloudStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new GrobbulusCloudMultiplier(ai));
}
void GrobbulusCloudStrategy::InitNonCombatMultipliers(std::list<Multiplier*>& multipliers) { InitCombatMultipliers(multipliers); }
void GrobbulusCloudStrategy::InitReactionMultipliers(std::list<Multiplier*>& multipliers) { InitCombatMultipliers(multipliers); }

#include "playerbot/strategy/actions/KelThuzadInterruptActions.h"
#include "playerbot/strategy/actions/GenericSpellActions.h"
namespace
{
class KelThuzadInterruptMultiplier : public Multiplier
{
public:
    KelThuzadInterruptMultiplier(PlayerbotAI* ai) : Multiplier(ai, "kt interrupt reservation") {}
    float GetValue(Action* action) override
    {
        auto spell = dynamic_cast<CastSpellAction*>(action);
        if (!spell)
            return 1.0f;
        Unit* boss = GetKelThuzadInterruptBoss(ai);
        if (!boss || action->GetTarget() != boss)
            return 1.0f;
        uint32 id = spell->GetSpellID();
        return IsKelThuzadReservedInterrupt(ai, id) ? 0.0f : 1.0f;
    }
};
}
void KelThuzadInterruptStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("kt frostbolt ready",
        NextAction::array(0, new NextAction("kt frostbolt interrupt", ACTION_EMERGENCY + 9), NULL)));
}
void KelThuzadInterruptStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    InitCombatTriggers(triggers);
}
void KelThuzadInterruptStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new KelThuzadInterruptMultiplier(ai));
}
void KelThuzadInterruptStrategy::InitReactionMultipliers(std::list<Multiplier*>& multipliers)
{
    InitCombatMultipliers(multipliers);
}

#include "playerbot/strategy/actions/PatchwerkSoakerActions.h"
#include "playerbot/strategy/actions/SapphironBlizzardHazards.h"
#include "playerbot/strategy/actions/AttackAction.h"
#include "playerbot/EbonrocSwapPolicy.h"
namespace
{
class PatchwerkAssignmentMultiplier : public Multiplier
{
public:
    PatchwerkAssignmentMultiplier(PlayerbotAI* ai) : Multiplier(ai,"patchwerk assignment") {}
    float GetValue(Action* action) override
    {
        if (!action)
            return 1.0f;
        std::string const name = action->getName();
        bool const taunt = EbonrocTauntAction(name);
        bool const approach = dynamic_cast<MovementAction*>(action) ||
            dynamic_cast<CastReachTargetSpellAction*>(action) || dynamic_cast<AttackAction*>(action);
        if (!taunt && !approach)
            return 1.0f;
        Unit* boss = GetPatchwerkBoss(ai);
        if (!boss)
            return 1.0f;
        if (taunt && PatchwerkSoakerAssigned(ai))
        {
            bool const aoe = name == "challenging shout" || name == "challenging roar";
            if (aoe || action->GetTarget() == boss ||
                (name == "righteous defense" && action->GetTarget() == boss->GetVictim()))
                return 0.0f;
        }
        bool const escape = name == "patchwerk melee retreat" || name == "move away from hazard" ||
            name == "raid bomb runout" || name == "grobbulus cloud escape" ||
            name == "flee" || name == "runaway";
        return approach && !escape && PatchwerkNeedsMeleeHold(ai,boss) ? 0.0f : 1.0f;
    }
};
}
#include "playerbot/GarrAoePolicy.h"
#include "playerbot/strategy/actions/GenericSpellActions.h"
namespace
{
class FaerlinaWorshipperGuardMultiplier : public Multiplier
{
public:
    FaerlinaWorshipperGuardMultiplier(PlayerbotAI* ai) : Multiplier(ai, "faerlina worshipper protection") {}
    float GetValue(Action* action) override
    {
        if (!action)
            return 1.0f;
        bool const incidental = GarrIncidentalDamageAction(action->getName(),
            action->getThreatType() == ActionThreatType::ACTION_THREAT_AOE,
            dynamic_cast<CastHealingSpellAction*>(action) != nullptr,
            action->IsCrowdControlAction());
        if (incidental)
            return GetActiveFaerlinaWorshipperGuard(ai) ? 0.0f : 1.0f;
        if (action->IsCrowdControlAction() || action->getName() == "attack my target" ||
            action->getName() == "attack rti target")
            return 1.0f;
        bool const offensive = dynamic_cast<AttackAction*>(action) ||
            dynamic_cast<CastSpellAction*>(action) || action->getName() == "pet attack";
        return offensive && IsFaerlinaWorshipperTargetExcluded(ai, action->GetTarget()) ? 0.0f : 1.0f;
    }
};
}

void FaerlinaWorshipperGuardStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("raid dps target excluded",
        NextAction::array(0, new NextAction("raid dps retarget", ACTION_HIGH + 5), nullptr)));
}
void FaerlinaWorshipperGuardStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    InitCombatTriggers(triggers);
}
void FaerlinaWorshipperGuardStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new FaerlinaWorshipperGuardMultiplier(ai));
}
void FaerlinaWorshipperGuardStrategy::InitReactionMultipliers(std::list<Multiplier*>& multipliers)
{
    InitCombatMultipliers(multipliers);
}

namespace
{
class LoathebSporeGuardMultiplier : public Multiplier
{
public:
    LoathebSporeGuardMultiplier(PlayerbotAI* ai) : Multiplier(ai, "loatheb spore protection") {}
    float GetValue(Action* action) override
    {
        if (!action || !GetActiveLoathebSporeGuard(ai))
            return 1.0f;
        if (GarrIncidentalDamageAction(action->getName(),
                action->getThreatType() == ActionThreatType::ACTION_THREAT_AOE,
                dynamic_cast<CastHealingSpellAction*>(action) != nullptr,
                action->IsCrowdControlAction()))
            return 0.0f;
        if (action->IsCrowdControlAction() || action->getName() == "attack my target" ||
            action->getName() == "attack rti target")
            return 1.0f;
        bool const offensive = dynamic_cast<AttackAction*>(action) ||
            dynamic_cast<CastSpellAction*>(action) || action->getName() == "pet attack";
        return offensive && IsLoathebSporeTargetExcluded(ai, action->GetTarget()) ? 0.0f : 1.0f;
    }
};
}

void LoathebSporeGuardStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("raid dps target excluded",
        NextAction::array(0, new NextAction("raid dps retarget", ACTION_HIGH + 5), nullptr)));
}
void LoathebSporeGuardStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    InitCombatTriggers(triggers);
}
void LoathebSporeGuardStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new LoathebSporeGuardMultiplier(ai));
}
void LoathebSporeGuardStrategy::InitReactionMultipliers(std::list<Multiplier*>& multipliers)
{
    InitCombatMultipliers(multipliers);
}

void PatchwerkAssignmentStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    if (name == "patchwerk melee safety")
        triggers.push_back(new TriggerNode("patchwerk melee unsafe",
            NextAction::array(0,new NextAction("patchwerk melee retreat",ACTION_EMERGENCY+7),NULL)));
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

namespace
{
class ThaddiusPolarityMultiplier : public Multiplier
{
public:
    ThaddiusPolarityMultiplier(PlayerbotAI* ai) : Multiplier(ai, "thaddius polarity movement") {}
    float GetValue(Action* action) override
    {
        if (!action || (!dynamic_cast<MovementAction*>(action) &&
                        !dynamic_cast<CastReachTargetSpellAction*>(action)))
            return 1.0f;
        std::string const& name = action->getName();
        bool const escape = name == "thaddius polarity stack" || name == "move away from hazard" ||
            name == "raid bomb runout" || name == "grobbulus cloud escape" ||
            name == "locust swarm retreat" || name == "flee" || name == "runaway";
        if (escape)
            return 1.0f;
        return ShouldHoldThaddiusPolarityPosition(ai) ? 0.0f : 1.0f;
    }
};
}
void ThaddiusPolarityStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("thaddius polarity split",
        NextAction::array(0, new NextAction("thaddius polarity stack", ACTION_EMERGENCY + 9), nullptr)));
}
void ThaddiusPolarityStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    InitCombatTriggers(triggers);
}
void ThaddiusPolarityStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new ThaddiusPolarityMultiplier(ai));
}
void ThaddiusPolarityStrategy::InitReactionMultipliers(std::list<Multiplier*>& multipliers)
{
    InitCombatMultipliers(multipliers);
}


void SapphironBlizzardStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("sapphiron blizzard nearby",
        NextAction::array(0, new NextAction("move away from hazard", ACTION_EMERGENCY + 7), nullptr)));
}

void SapphironBlizzardStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    InitCombatTriggers(triggers);
}

namespace
{
class SapphironBlizzardMultiplier : public Multiplier
{
public:
    SapphironBlizzardMultiplier(PlayerbotAI* ai) : Multiplier(ai, "sapphiron blizzard movement") {}
    float GetValue(Action* action) override
    {
        if (!action)
            return 1.0f;
        std::string const& name = action->getName();
        bool const escape = name == "move away from hazard" || name == "raid bomb runout" ||
            name == "grobbulus cloud escape" || name == "flee" || name == "runaway";
        if (escape)
            return 1.0f;
        bool const movement = dynamic_cast<MovementAction*>(action) ||
            dynamic_cast<CastReachTargetSpellAction*>(action);
        return movement && SapphironBlizzardTooClose(ai) ? 0.0f : 1.0f;
    }
};
}

void SapphironBlizzardStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new SapphironBlizzardMultiplier(ai));
}

void SapphironBlizzardStrategy::InitReactionMultipliers(std::list<Multiplier*>& multipliers)
{
    InitCombatMultipliers(multipliers);
}

void KelThuzadFissureStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("kt fissure nearby",
        NextAction::array(0, new NextAction("kt fissure escape", ACTION_EMERGENCY + 2), nullptr)));
}
void KelThuzadFissureStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    InitCombatTriggers(triggers);
}
