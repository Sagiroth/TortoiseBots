#pragma once
#include "DungeonTriggers.h"
#include "GenericTriggers.h"

namespace ai
{
    class BlackwingLairEnterDungeonTrigger : public EnterDungeonTrigger
    {
    public:
        BlackwingLairEnterDungeonTrigger(PlayerbotAI* ai) : EnterDungeonTrigger(ai, "enter blackwing lair", "blackwing lair", 469) {}
    };

    class BlackwingLairLeaveDungeonTrigger : public LeaveDungeonTrigger
    {
    public:
        BlackwingLairLeaveDungeonTrigger(PlayerbotAI* ai) : LeaveDungeonTrigger(ai, "leave blackwing lair", "blackwing lair", 469) {}
    };

    class RazorgoreStartFightTrigger : public StartBossFightTrigger
    {
    public:
        RazorgoreStartFightTrigger(PlayerbotAI* ai) : StartBossFightTrigger(ai, "start razorgore fight", "razorgore", 12435) {}
    };

    class RazorgoreEndFightTrigger : public EndBossFightTrigger
    {
    public:
        RazorgoreEndFightTrigger(PlayerbotAI* ai) : EndBossFightTrigger(ai, "end razorgore fight", "razorgore", 12435) {}
    };

    // Razorgore cone escape: non-victims inside the 15y frontal half-circle
    // step behind the boss (mod-playerbots parity). Header-inline; the
    // fight-strategy gate lives in the strategy wiring.
    class RazorgoreConeTrigger : public Trigger
    {
    public:
        RazorgoreConeTrigger(PlayerbotAI* ai, std::string name = "razorgore cone", int checkInterval = 1)
        : Trigger(ai, name, checkInterval) {}

        bool IsActive() override
        {
            if (!bot->IsInWorld() || bot->IsBeingTeleported() || !sServerFacade.IsAlive(bot))
                return false;
            if (!ai->HasStrategy("razorgore", BotState::BOT_STATE_COMBAT))
                return false;
            AiObjectContext* context = ai->GetAiObjectContext();
            const std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
            for (const ObjectGuid& attackerGuid : attackers)
            {
                Unit* attacker = ai->GetUnit(attackerGuid);
                if (!attacker || attacker->GetEntry() != 12435)
                    continue;
                // Controlled phase (orb): the controller drives; raid holds.
                if (ai->HasAura(19832, attacker))
                    return false;
                // The victim holds — moving rotates the boss into the raid.
                if (attacker->GetVictim() && attacker->GetVictim()->getObjectGuid() == bot->getObjectGuid())
                    return false;
                if (bot->GetDistance2d(attacker) > 15.0f)
                    return false;
                return attacker->HasInArc(M_PI_F, bot);
            }
            return false;
        }
    };

    // Ranged War Stomp spacing: ranged non-victims outside the cone but
    // inside 15y back off (mod-playerbots parity).
    class RazorgoreRangedTrigger : public Trigger
    {
    public:
        RazorgoreRangedTrigger(PlayerbotAI* ai, std::string name = "razorgore ranged", int checkInterval = 1)
        : Trigger(ai, name, checkInterval) {}

        bool IsActive() override
        {
            if (!bot->IsInWorld() || bot->IsBeingTeleported() || !sServerFacade.IsAlive(bot))
                return false;
            if (!ai->HasStrategy("razorgore", BotState::BOT_STATE_COMBAT))
                return false;
            if (!ai->IsRanged(bot))
                return false;
            AiObjectContext* context = ai->GetAiObjectContext();
            const std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
            for (const ObjectGuid& attackerGuid : attackers)
            {
                Unit* attacker = ai->GetUnit(attackerGuid);
                if (!attacker || attacker->GetEntry() != 12435)
                    continue;
                if (ai->HasAura(19832, attacker))
                    return false;
                if (attacker->GetVictim() && attacker->GetVictim()->getObjectGuid() == bot->getObjectGuid())
                    return false;
                if (bot->GetDistance2d(attacker) > 15.0f)
                    return false;
                return !attacker->HasInArc(M_PI_F, bot);
            }
            return false;
        }
    };

    class SuppressionDeviceNeedStealthTrigger : public Trigger
    {
    public:
        SuppressionDeviceNeedStealthTrigger(PlayerbotAI* ai) : Trigger(ai, "suppression device need stealth", 1) {}

        bool IsActive() override
        {
            if (bot->GetClass() != CLASS_ROGUE)
                return false;

            if (ai->HasAura("stealth", bot))
                return false;

            std::list<GuidPosition> gos = AI_VALUE(std::list<GuidPosition>, "go usable filter::go trapped filter::entry filter::{gos in sight,suppression devices}");
            return !gos.empty();
        }
    };

    class SuppressionDeviceInSightTrigger : public Trigger
    {
    public:
        SuppressionDeviceInSightTrigger(PlayerbotAI* ai) : Trigger(ai, "suppression device in sight", 1) {}

        bool IsActive() override
        {
            if (bot->GetClass() != CLASS_ROGUE)
                return false;

            std::list<GuidPosition> gosInSight = AI_VALUE(std::list<GuidPosition>, "go usable filter::go trapped filter::entry filter::{gos in sight,suppression devices}");
            std::list<GuidPosition> gosClose = AI_VALUE(std::list<GuidPosition>, "entry filter::{gos close,suppression devices}");

            return !gosInSight.empty() && gosClose.empty();
        }
    };

    class SuppressionDeviceCloseTrigger : public Trigger
    {
    public:
        SuppressionDeviceCloseTrigger(PlayerbotAI* ai) : Trigger(ai, "suppression device close", 1) {}

        bool IsActive() override
        {
            if (bot->GetClass() != CLASS_ROGUE)
                return false;

            std::list<GuidPosition> gos = AI_VALUE(std::list<GuidPosition>, "go usable filter::go trapped filter::entry filter::{gos close,suppression devices}");
            return !gos.empty();
        }
    };
}