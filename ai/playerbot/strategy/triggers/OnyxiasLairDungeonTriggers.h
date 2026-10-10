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

    // Deep Breath: Onyxia hovering stopped with no target (mod-playerbots
    // parity, 1.18.1 detection). The 8 breath spells cast TRIGGERED, which
    // never populates CURRENT_GENERIC_SPELL (Spell.cpp:3641), so cast-slot
    // polling cannot work here. Instead: during the 5s breath window the
    // core holds position (IsStopped) with a cleared target (SetTargetGuid
    // empty — boss_onyxia.cpp DoMovement), while normal phase-2 flight
    // keeps a victim + motion. Header-inline; the fight-strategy gate lives
    // in the strategy wiring. Boss resolved via the attacker scan so whelp
    // tanks, melee, and healers all react regardless of their own target.
    class OnyxiaDeepBreathTrigger : public Trigger
    {
    public:
        OnyxiaDeepBreathTrigger(PlayerbotAI* ai, std::string name = "onyxia deep breath", int checkInterval = 1)
        : Trigger(ai, name, checkInterval) {}
        std::string GetTargetName() override { return "self target"; }

        bool IsActive() override
        {
            if (!bot->IsInWorld() || bot->IsBeingTeleported() || !sServerFacade.IsAlive(bot))
                return false;
            if (!ai->HasStrategy("onyxia", BotState::BOT_STATE_COMBAT))
                return false;
            AiObjectContext* context = ai->GetAiObjectContext();
            const std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
            for (const ObjectGuid& attackerGuid : attackers)
            {
                Unit* attacker = ai->GetUnit(attackerGuid);
                if (!attacker || attacker->GetEntry() != 10184)
                    continue;
                if (!sServerFacade.IsAlive(attacker))
                    continue;
                // Airborne breath window only: grounded phases always hold
                // a victim and keep moving.
                if (!attacker->HasAura(17131))
                    return false;
                if (!attacker->IsStopped())
                    return false;
                return attacker->GetTargetGuid().IsEmpty();
            }
            return false;
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
