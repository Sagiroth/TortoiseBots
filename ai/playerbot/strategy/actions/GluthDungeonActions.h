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
}
