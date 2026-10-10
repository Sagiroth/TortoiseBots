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

// Tank / ranged anchors (donor coords, same map geometry).
class LoathebPositionAction : public MovementAction
{
public:
    LoathebPositionAction(PlayerbotAI* ai) : MovementAction(ai, "loatheb position") {}
    bool Execute(Event& event) override;
};
}
