#pragma once
#include "playerbot/PlayerbotAI.h"
#include "DungeonActions.h"
#include "ChangeStrategyAction.h"

namespace ai
{
class KelthuzadEnableFightStrategyAction : public ChangeAllStrategyAction
{
public:
    KelthuzadEnableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable kel'thuzad fight strategy", "+kel'thuzad") {}
};

class KelthuzadDisableFightStrategyAction : public ChangeAllStrategyAction
{
public:
    KelthuzadDisableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable kel'thuzad fight strategy", "-kel'thuzad") {}
};

// Role-split add targeting (mod-playerbots parity:
// KelthuzadChooseTargetAction): ranged take soldier > weaver > abom,
// tanks abom > guardian, melee abom — KT himself last. Retargets via
// the shared Attack() helper.
class KelthuzadChooseTargetAction : public AttackAction
{
public:
    KelthuzadChooseTargetAction(PlayerbotAI* ai) : AttackAction(ai, "kel'thuzad choose target") {}
    bool Execute(Event& event) override;
};

// Phase-2 positioning (donor KelthuzadPositionAction, center verified vs
// core pullPortal 3716.38/-5106.78): phase 1 gathers center; phase 2
// ranged take the 20yd ring, tanks the anchors, fissures fled via the
// hazard action.
class KelthuzadPositionAction : public MovementAction
{
public:
    KelthuzadPositionAction(PlayerbotAI* ai) : MovementAction(ai, "kel'thuzad position") {}
    bool Execute(Event& event) override;
};
}
