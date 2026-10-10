#pragma once
#include "playerbot/PlayerbotAI.h"
#include "DungeonActions.h"
#include "ChangeStrategyAction.h"

namespace ai
{
class SapphironEnableFightStrategyAction : public ChangeAllStrategyAction
{
public:
    SapphironEnableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable sapphiron fight strategy", "+sapphiron") {}
};

class SapphironDisableFightStrategyAction : public ChangeAllStrategyAction
{
public:
    SapphironDisableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable sapphiron fight strategy", "-sapphiron") {}
};

// Air phase: hide behind the nearest group member carrying Icebolt
// (28522) — the ice block breaks line of sight to Frost Breath
// (mod-playerbots parity: SapphironFlightPositionAction/MoveToNearestIcebolt).
class SapphironHideAction : public MovementAction
{
public:
    SapphironHideAction(PlayerbotAI* ai) : MovementAction(ai, "sapphiron hide") {}
    bool Execute(Event& event) override;
};

// Chill on the bot: step 10yd clear of the nearest Blizzard NPC (16474).
class SapphironAvoidBlizzardAction : public MovementAction
{
public:
    SapphironAvoidBlizzardAction(PlayerbotAI* ai) : MovementAction(ai, "sapphiron avoid blizzard") {}
    bool Execute(Event& event) override;
};
}
