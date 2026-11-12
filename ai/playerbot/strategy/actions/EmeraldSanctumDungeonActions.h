#pragma once
#include "playerbot/strategy/actions/DungeonActions.h"
#include "playerbot/strategy/actions/ChangeStrategyAction.h"
#include "playerbot/strategy/actions/AttackAction.h"

namespace ai
{
class EmeraldSanctumEnableDungeonStrategyAction : public ChangeAllStrategyAction
{
public:
    EmeraldSanctumEnableDungeonStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable emerald sanctum strategy", "+emerald sanctum") {}
};

class EmeraldSanctumDisableDungeonStrategyAction : public ChangeAllStrategyAction
{
public:
    EmeraldSanctumDisableDungeonStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable emerald sanctum strategy", "-emerald sanctum") {}
};

class SolniusEnableFightStrategyAction : public ChangeAllStrategyAction
{
public:
    SolniusEnableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable solnius fight strategy", "+solnius") {}
};

class SolniusDisableFightStrategyAction : public ChangeAllStrategyAction
{
public:
    SolniusDisableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable solnius fight strategy", "-solnius") {}
};

// Call of Nightmare add focus: attack the nearest live major add
// (FindSolniusAdd, from the cached attackers list). Explicit attack
// orders win. The "solnius adds" trigger limits this to DPS bots.
class SolniusAttackAddAction : public AttackAction
{
public:
    SolniusAttackAddAction(PlayerbotAI* ai) : AttackAction(ai, "solnius attack add") {}
    bool Execute(Event& event) override;
};
}
