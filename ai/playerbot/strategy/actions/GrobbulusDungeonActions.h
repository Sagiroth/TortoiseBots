#pragma once
#include "playerbot/PlayerbotAI.h"
#include "DungeonActions.h"
#include "ChangeStrategyAction.h"

namespace ai
{
class GrobbulusEnableFightStrategyAction : public ChangeAllStrategyAction
{
public:
    GrobbulusEnableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable grobbulus fight strategy", "+grobbulus") {}
};

class GrobbulusDisableFightStrategyAction : public ChangeAllStrategyAction
{
public:
    GrobbulusDisableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable grobbulus fight strategy", "-grobbulus") {}
};

// Ranged injection carrier: move 18yd behind Grobbulus (opposite his
// facing) so the dropped cloud lands clear of the raid and the carrier
// keeps DPS uptime (mod-playerbots parity: GrobbulusGoBehindAction).
class GrobbulusGoBehindAction : public MovementAction
{
public:
    GrobbulusGoBehindAction(PlayerbotAI* ai) : MovementAction(ai, "grobbulus go behind") {}
    bool Execute(Event& event) override;
};
}
