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

    // A protected aura was applied after an area cure started: the bot-cast Abolish aura or the bot's own cleansing
    // totem of that type would remove it on the next tick or pulse.
    class BossTacticsDispelSweepTrigger : public Trigger
    {
    public:
        BossTacticsDispelSweepTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics dispel sweep", 1) {}
        bool IsActive() override;
    };

    // A kill_first creature is alive and this DPS bot is not on it.
    class BossTacticsKillFirstTrigger : public Trigger
    {
    public:
        BossTacticsKillFirstTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics kill first", 2) {}
        bool IsActive() override;
    };

    // This DPS bot's current target is a boss entry a hold_kill row holds (its blocker still lives).
    class BossTacticsHoldKillTrigger : public Trigger
    {
    public:
        BossTacticsHoldKillTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics hold kill", 1) {}
        bool IsActive() override;
    };

    // A linked_death group is held and this free DPS bot is not on the equalise pick (the highest member, no member at
    // or below param while another is above it).
    class BossTacticsLinkedDeathTrigger : public Trigger
    {
    public:
        BossTacticsLinkedDeathTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics linked death", 1) {}
        bool IsActive() override;
    };

    // A creature in the fight casts a spell an avoid_cast row lists and this bot (not a tank) stands inside its radius.
    class BossTacticsAvoidCastTrigger : public Trigger
    {
    public:
        BossTacticsAvoidCastTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics avoid cast", 1) {}
        bool IsActive() override;
    };

    // A stand set applies to this bot (ai_boss_stand: the boss is at an anchor casting the row's window spell)
    // and the bot is not at its seat.
    class BossTacticsStandTrigger : public Trigger
    {
    public:
        BossTacticsStandTrigger(PlayerbotAI* ai) : Trigger(ai, "boss tactics stand", 1) {}
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
