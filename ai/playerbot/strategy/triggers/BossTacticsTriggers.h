#pragma once
#include "playerbot/strategy/Trigger.h"

namespace ai
{
    // The "boss tactics" strategy is on for a bot while it is on a map with rows
    // and the gate allows it (BossTacticsWanted: AiPlayerbot.DungeonTactics, or
    // the bot is in a group a remote commander has bound). Same enter/leave shape
    // as the per-dungeon strategies, re-checked every 5 ticks.
    class BossTacticsEnterTrigger : public Trigger
    {
    public:
        BossTacticsEnterTrigger(PlayerbotAI* ai) : Trigger(ai, "enter boss tactics", 5) {}
        bool IsActive() override;
    };

    class BossTacticsLeaveTrigger : public Trigger
    {
    public:
        BossTacticsLeaveTrigger(PlayerbotAI* ai) : Trigger(ai, "leave boss tactics", 5) {}
        bool IsActive() override;
    };

    // A listed spell is being cast and this bot has an interrupt for it.
    class BossTacticsInterruptTrigger : public Trigger
    {
    public:
        BossTacticsInterruptTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics interrupt", 1) {}
        bool IsActive() override;
    };

    // A kill_first creature is alive and this DPS bot is not on it.
    class BossTacticsKillFirstTrigger : public Trigger
    {
    public:
        BossTacticsKillFirstTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics kill first", 2) {}
        bool IsActive() override;
    };

    // The bot stands in the front of a boss with a listed frontal attack.
    class BossTacticsFrontTrigger : public Trigger
    {
    public:
        BossTacticsFrontTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics front", 2) {}
        bool IsActive() override;
    };
}
