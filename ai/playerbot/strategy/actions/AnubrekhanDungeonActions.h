#pragma once
#include "playerbot/PlayerbotAI.h"
#include "DungeonActions.h"
#include "ChangeStrategyAction.h"
#include "playerbot/strategy/Multiplier.h"

namespace ai
{
class AnubrekhanEnableFightStrategyAction : public ChangeAllStrategyAction
{
public:
    AnubrekhanEnableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable anub'rekhan fight strategy", "+anub'rekhan") {}
};

class AnubrekhanDisableFightStrategyAction : public ChangeAllStrategyAction
{
public:
    AnubrekhanDisableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable anub'rekhan fight strategy", "-anub'rekhan") {}
};

// Adds-first targeting (mod-playerbots parity:
// AnubrekhanChooseTargetAction): tanks stay on the boss, everyone else
// takes the lowest-HP live Crypt Guard, boss when none stand.
class AnubrekhanChooseTargetAction : public AttackAction
{
public:
    AnubrekhanChooseTargetAction(PlayerbotAI* ai) : AttackAction(ai, "anub'rekhan choose target") {}
    bool Execute(Event& event) override;
};

// Locust Swarm: collapse to the room center (donor coords, same map).
class AnubrekhanToCenterAction : public MovementAction
{
public:
    AnubrekhanToCenterAction(PlayerbotAI* ai) : MovementAction(ai, "anub'rekhan to center") {}
    bool Execute(Event& event) override;
};

// Flee suppression during the swarm (donor AnubrekhanGenericMultiplier):
// the raid holds the center instead of scattering.
class AnubrekhanSwarmMultiplier : public Multiplier
{
public:
    AnubrekhanSwarmMultiplier(PlayerbotAI* ai) : Multiplier(ai, "anub'rekhan swarm") {}
    float GetValue(Action* action) override;
};
}
