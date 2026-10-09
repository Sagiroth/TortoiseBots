#pragma once
#include "DungeonTriggers.h"

namespace ai
{
    class SapphironStartFightTrigger : public StartBossFightTrigger
    {
    public:
        SapphironStartFightTrigger(PlayerbotAI* ai) : StartBossFightTrigger(ai, "start sapphiron fight", "sapphiron", 15989) {}
    };

    class SapphironEndFightTrigger : public EndBossFightTrigger
    {
    public:
        SapphironEndFightTrigger(PlayerbotAI* ai) : EndBossFightTrigger(ai, "end sapphiron fight", "sapphiron", 15989) {}
    };

    // Air phase: Sapphiron hovers (MOVEFLAG_HOVER — vanilla uses hover,
    // not flying; donor IsFlying does not fire here).
    class SapphironAirTrigger : public Trigger
    {
    public:
        SapphironAirTrigger(PlayerbotAI* ai, std::string name = "sapphiron air hide", int checkInterval = 1)
            : Trigger(ai, name, checkInterval) {}
        std::string GetTargetName() override { return "self target"; }
        bool IsActive() override;
    };

    // Chill on the bot (28534 periodic / 28547 damage): a Blizzard NPC
    // (16474) is on top of the bot.
    class SapphironBlizzardTrigger : public Trigger
    {
    public:
        SapphironBlizzardTrigger(PlayerbotAI* ai, std::string name = "sapphiron blizzard", int checkInterval = 1)
            : Trigger(ai, name, checkInterval) {}
        std::string GetTargetName() override { return "self target"; }
        bool IsActive() override;
    };

    // Ground + non-tank melee in range: flank behind (tail sweep 15847
    // rear cone; breath frontal — flanks via the shared set-behind).
    class SapphironFlankTrigger : public Trigger
    {
    public:
        SapphironFlankTrigger(PlayerbotAI* ai, std::string name = "sapphiron flank", int checkInterval = 1)
            : Trigger(ai, name, checkInterval) {}
        std::string GetTargetName() override { return "current target"; }
        bool IsActive() override;
    };
}
