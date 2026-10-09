#pragma once
#include "playerbot/PlayerbotAI.h"
#include "DungeonActions.h"
#include "ChangeStrategyAction.h"

namespace ai
{
    class OnyxiasLairEnableDungeonStrategyAction : public ChangeAllStrategyAction
    {
    public:
        OnyxiasLairEnableDungeonStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable onyxia's lair strategy", "+onyxia's lair") {}
    };

    class OnyxiasLairDisableDungeonStrategyAction : public ChangeAllStrategyAction
    {
    public:
        OnyxiasLairDisableDungeonStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable onyxia's lair strategy", "-onyxia's lair") {}
    };

    class OnyxiaEnableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        OnyxiaEnableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable onyxia fight strategy", "+onyxia") {}
    };

    class OnyxiaDisableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        OnyxiaDisableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable onyxia fight strategy", "-onyxia") {}
    };

    // Deep Breath safe-zone dodge: run to the nearest of the 2 safe spots
    // for the breath direction currently casting (mod-playerbots parity).
    // Inherits MovementAction; Execute reads the boss cast, picks the
    // nearest zone of the matching pair, holds when already inside.
    class OnyxiaBreathSafeZoneAction : public MovementAction
    {
    public:
        OnyxiaBreathSafeZoneAction(PlayerbotAI* ai) : MovementAction(ai, "onyxia breath safe zone") {}
        bool Execute(Event& event) override;
        bool isPossible() override { return MovementAction::isPossible() && ai->CanMove(); }
    };
}
