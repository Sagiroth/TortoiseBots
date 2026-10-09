#pragma once
#include "playerbot/PlayerbotAI.h"
#include "DungeonActions.h"
#include "AttackAction.h"
#include "ChangeStrategyAction.h"
#include "playerbot/strategy/Multiplier.h"

namespace ai
{
class ThaddiusEnableFightStrategyAction : public ChangeAllStrategyAction
{
public:
    ThaddiusEnableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable thaddius fight strategy", "+thaddius") {}
};

class ThaddiusDisableFightStrategyAction : public ChangeAllStrategyAction
{
public:
    ThaddiusDisableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable thaddius fight strategy", "-thaddius") {}
};

// Pet phase: attack the nearest live add (mod-playerbots parity:
// ThaddiusAttackNearestPetAction — nearest of Stalagg/Feugen by entry
// scan, no donor boss-helper needed). Extends AttackAction for the
// shared Attack() retarget helper.
class ThaddiusAttackNearestPetAction : public AttackAction
{
public:
    ThaddiusAttackNearestPetAction(PlayerbotAI* ai) : AttackAction(ai, "thaddius attack nearest pet") {}
    bool Execute(Event& event) override;
};

// Transition: run to the platform edge, then jump down to the floor
// (donor ThaddiusMoveToPlatformAction via our physics JumpAction:
// qualifier carries "map x y z" like other jump callers).
class ThaddiusMoveToPlatformAction : public MovementAction
{
public:
    ThaddiusMoveToPlatformAction(PlayerbotAI* ai) : MovementAction(ai, "thaddius move to platform") {}
    bool Execute(Event& event) override;
};

// Thaddius phase: polarity sides — negative charge left, positive
// charge right, uncharged center; melee/ranged spots per side (donor
// ThaddiusMovePolarityAction coords, same map geometry).
class ThaddiusMovePolarityAction : public MovementAction
{
public:
    ThaddiusMovePolarityAction(PlayerbotAI* ai) : MovementAction(ai, "thaddius move polarity") {}
    bool Execute(Event& event) override;
};

// Even-HP DPS gate (mod-playerbots parity: ThaddiusGenericMultiplier
// pet-phase rule): while both adds live, damage stops on a pet at <=40%
// that trails its twin by 3%+ so both die together.
class ThaddiusEvenHpMultiplier : public Multiplier
{
public:
    ThaddiusEvenHpMultiplier(PlayerbotAI* ai) : Multiplier(ai, "thaddius even hp") {}
    float GetValue(Action* action) override;
};
}
