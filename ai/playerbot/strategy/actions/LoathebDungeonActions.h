#pragma once
#include "playerbot/PlayerbotAI.h"
#include "DungeonActions.h"
#include "ChangeStrategyAction.h"

namespace ai
{
class LoathebEnableFightStrategyAction : public ChangeAllStrategyAction
{
public:
    LoathebEnableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable loatheb fight strategy", "+loatheb") {}
};

class LoathebDisableFightStrategyAction : public ChangeAllStrategyAction
{
public:
    LoathebDisableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable loatheb fight strategy", "-loatheb") {}
};

// Spore assignment (mod-playerbots parity: LoathebChooseTargetAction):
// kill the spore under the bot, else stay on Loatheb.
class LoathebChooseTargetAction : public AttackAction
{
public:
    LoathebChooseTargetAction(PlayerbotAI* ai) : AttackAction(ai, "loatheb choose target") {}
    bool Execute(Event& event) override;
};

// Spore hold (donor LoathebGenericMultiplier, assist leg): once a bot
// has switched onto its spore, the 60-bid assists/AoE would yank it back
// to the boss the next tick and the keep-away flee would step it off the
// 1yd spore. Tanks also ignore tank-assist while Loatheb is on threat so
// a loose spore never drags the main tank off the boss.
class LoathebSporeHoldMultiplier : public Multiplier
{
public:
    LoathebSporeHoldMultiplier(PlayerbotAI* ai) : Multiplier(ai, "loatheb spore hold") {}
    float GetValue(Action* action) override;
};

// Tank / ranged anchors (donor coords, same map geometry).
class LoathebPositionAction : public MovementAction
{
public:
    LoathebPositionAction(PlayerbotAI* ai) : MovementAction(ai, "loatheb position") {}
    bool Execute(Event& event) override;
};
}
