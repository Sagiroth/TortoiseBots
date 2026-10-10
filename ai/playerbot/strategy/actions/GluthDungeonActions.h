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

// Chow triage (mod-playerbots parity: GluthChooseTargetAction): DPS
// execute the nearest chow at or under 10% within 30yd, else stay on
// the boss. Tanks skip the chow sweep and hold Gluth (donor pins
// MainTank/Assist0 to the boss).
class GluthChooseTargetAction : public AttackAction
{
public:
    GluthChooseTargetAction(PlayerbotAI* ai) : AttackAction(ai, "gluth choose target") {}
    bool Execute(Event& event) override;
};

// Mortal-wound taunt swap (mod-playerbots parity:
// GluthMainTankMortalWoundTrigger + "taunt spell" row). Casts the
// class-correct taunt directly (warrior "taunt", druid "growl"): the
// "taunt spell" ActionNode alias only exists in legacy class strategies
// that never run, so naming it would silently fail for every tank.
// No paladin branch (no 1.12 taunt). Targets Gluth found through the
// encounter, not the bot's current target — the off-tank may hold chow
// when the swap fires. Retargets onto Gluth first (AttackAction base),
// so the new tank builds boss threat even if the taunt itself is on
// cooldown or out of range.
class GluthTauntSwapAction : public AttackAction
{
public:
    GluthTauntSwapAction(PlayerbotAI* ai) : AttackAction(ai, "gluth taunt swap") {}
    bool Execute(Event& event) override;
};
}
