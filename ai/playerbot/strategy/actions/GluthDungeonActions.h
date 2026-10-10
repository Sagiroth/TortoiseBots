#pragma once
#include "playerbot/PlayerbotAI.h"
#include "DungeonActions.h"
#include "ChangeStrategyAction.h"

namespace ai
{
class GluthEnableFightStrategyAction : public ChangeAllStrategyAction
{
public:
    GluthEnableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable gluth fight strategy", "+gluth") {}
};

class GluthDisableFightStrategyAction : public ChangeAllStrategyAction
{
public:
    GluthDisableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable gluth fight strategy", "-gluth") {}
};

// Chow triage (mod-playerbots parity: GluthChooseTargetAction, DPS leg):
// execute the nearest chow at or under 10% within 30yd, else stay on
// the boss. Tanks never take this (they hold Gluth).
class GluthChooseTargetAction : public AttackAction
{
public:
    GluthChooseTargetAction(PlayerbotAI* ai) : AttackAction(ai, "gluth choose target") {}
    bool Execute(Event& event) override;
};

// Mortal-wound taunt swap (mod-playerbots parity:
// GluthMainTankMortalWoundTrigger + "taunt spell" row). Casts the
// class-correct taunt directly: the "taunt spell" ActionNode alias only
// exists in legacy class strategies that never run (live specs use
// placeholder strategies), so naming it would silently fail for every
// tank. Targets Gluth found through the encounter, not the bot's
// current target — the off-tank may be holding chow when the swap fires.
class GluthTauntSwapAction : public Action
{
public:
    GluthTauntSwapAction(PlayerbotAI* ai) : Action(ai, "gluth taunt swap") {}
    bool Execute(Event& event) override;
};
}
