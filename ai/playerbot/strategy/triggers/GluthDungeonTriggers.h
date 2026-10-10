#pragma once
#include "DungeonTriggers.h"

namespace ai
{
    class GluthStartFightTrigger : public StartBossFightTrigger
    {
    public:
        GluthStartFightTrigger(PlayerbotAI* ai) : StartBossFightTrigger(ai, "start gluth fight", "gluth", 15932) {}
    };

    class GluthEndFightTrigger : public EndBossFightTrigger
    {
    public:
        GluthEndFightTrigger(PlayerbotAI* ai) : EndBossFightTrigger(ai, "end gluth fight", "gluth", 15932) {}
    };

    // Swap trigger: fires on the off-tank only, when another player
    // (bot or human) tanks Gluth with 5+ Mortal Wound (25646) stacks.
    // Gluth is resolved through the encounter, not the bot's current
    // target - the off-tank may hold chow (or nothing) when the swap
    // fires. Target name stays default ("self target"): the trigger
    // resolves Gluth itself and the action casts on it directly.
    class GluthMortalWoundSwapTrigger : public Trigger
    {
    public:
        GluthMortalWoundSwapTrigger(PlayerbotAI* ai, std::string name = "gluth mortal wound swap", int checkInterval = 1)
            : Trigger(ai, name, checkInterval) {}
        bool IsActive() override;
    };

    // Chow triage trigger: live Zombie Chow (16360) within 30yd for a
    // non-tank whose target is not already the execute chow.
    class GluthChowUpTrigger : public Trigger
    {
    public:
        GluthChowUpTrigger(PlayerbotAI* ai, std::string name = "gluth chow up", int checkInterval = 2)
            : Trigger(ai, name, checkInterval) {}
        std::string GetTargetName() override { return "self target"; }
        bool IsActive() override;
    };

    // Encounter trigger (donor parity: donor runs "gluth choose target"
    // off the continuous "gluth" trigger): Gluth himself on the shared
    // cached lists. Cheap cached check only - the chooser does the one
    // precise world sweep and falls back to the boss, so bots swap back
    // once chow die.
    class GluthTrigger : public Trigger
    {
    public:
        GluthTrigger(PlayerbotAI* ai, std::string name = "gluth", int checkInterval = 2)
            : Trigger(ai, name, checkInterval) {}
        std::string GetTargetName() override { return "self target"; }
        bool IsActive() override;
    };
}
