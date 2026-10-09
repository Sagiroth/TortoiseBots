#pragma once
#include "DungeonTriggers.h"
#include "playerbot/OnyxiaBreathPolicy.h"

namespace ai
{
    class OnyxiasLairEnterDungeonTrigger : public EnterDungeonTrigger
    {
    public:
        OnyxiasLairEnterDungeonTrigger(PlayerbotAI* ai) : EnterDungeonTrigger(ai, "enter onyxia's lair", "onyxia's lair", 249) {}
    };

    class OnyxiasLairLeaveDungeonTrigger : public LeaveDungeonTrigger
    {
    public:
        OnyxiasLairLeaveDungeonTrigger(PlayerbotAI* ai) : LeaveDungeonTrigger(ai, "leave onyxia's lair", "onyxia's lair", 249) {}
    };

    class OnyxiaStartFightTrigger : public StartBossFightTrigger
    {
    public:
        OnyxiaStartFightTrigger(PlayerbotAI* ai) : StartBossFightTrigger(ai, "start onyxia fight", "onyxia", 10184) {}
    };

    class OnyxiaEndFightTrigger : public EndBossFightTrigger
    {
    public:
        OnyxiaEndFightTrigger(PlayerbotAI* ai) : EndBossFightTrigger(ai, "end onyxia fight", "onyxia", 10184) {}
    };

    // Deep Breath: Onyxia casting any of the 8 directional Breath spells
    // (mod-playerbots parity). Header-inline; the fight-strategy gate lives
    // in the strategy wiring. Reads the boss's current generic-spell cast
    // (same CURRENT_GENERIC_SPELL shape as GenericTriggers.cpp:673).
    class OnyxiaDeepBreathTrigger : public Trigger
    {
    public:
        OnyxiaDeepBreathTrigger(PlayerbotAI* ai, std::string name = "onyxia deep breath", int checkInterval = 1)
        : Trigger(ai, name, checkInterval) {}
        std::string GetTargetName() override { return "current target"; }

        bool IsActive() override
        {
            if (!bot->IsInWorld() || bot->IsBeingTeleported() || !sServerFacade.IsAlive(bot))
                return false;
            if (!ai->HasStrategy("onyxia", BotState::BOT_STATE_COMBAT))
                return false;
            Unit* target = GetTarget();
            if (!target || !sServerFacade.IsAlive(target) || target->GetEntry() != 10184)
                return false;
            Spell* currentSpell = target->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            if (!currentSpell || !currentSpell->m_spellInfo)
                return false;
            return IsOnyxiaBreathSpell(currentSpell->m_spellInfo->Id);
        }
    };

    // Phase 2: Onyxia takes flight (Hover aura 17131, core boss_onyxia.cpp).
    // Melee cannot reach her; the fight strategy swaps them to wands/shoot
    // and spreads the raid for Fireball splash. Grounds on phase 3.
    class OnyxiaAirborneTrigger : public Trigger
    {
    public:
        OnyxiaAirborneTrigger(PlayerbotAI* ai) : Trigger(ai, "onyxia airborne", 1) {}
        std::string GetTargetName() override { return "current target"; }
        bool IsActive() override;
    };
}
