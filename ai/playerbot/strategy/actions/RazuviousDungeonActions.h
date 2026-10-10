#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/Multiplier.h"
#include "DungeonActions.h"
#include "ChangeStrategyAction.h"

namespace ai
{
class RazuviousEnableFightStrategyAction : public ChangeAllStrategyAction
{
public:
    RazuviousEnableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable razuvious fight strategy", "+razuvious") {}
};

class RazuviousDisableFightStrategyAction : public ChangeAllStrategyAction
{
public:
    RazuviousDisableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable razuvious fight strategy", "-razuvious") {}
};

// Without a charm: walk into range of a free Understudy (not charmed, no
// Mind Exhaustion) and Mind Control it. With one: send it at Razuvious,
// keep Bone Barrier up and taunt whenever the boss is on someone else
// (donor RazuviousUseObedienceCrystalAction, charm half).
class RazuviousMindControlAction : public MovementAction
{
public:
    RazuviousMindControlAction(PlayerbotAI* ai) : MovementAction(ai, "razuvious mind control") {}
    bool Execute(Event& event) override;

private:
    uint32 m_tauntAt = 0;
};

// Donor InstructorRazuviousGenericMultiplier: no player taunts on the boss;
// a priest holding an Understudy runs nothing but the control action, since
// any other cast or move breaks the Mind Control channel.
class RazuviousFightMultiplier : public Multiplier
{
public:
    RazuviousFightMultiplier(PlayerbotAI* ai) : Multiplier(ai, "razuvious fight") {}
    float GetValue(Action* action) override;
};
}
