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

    class BroodlordStartFightTrigger : public StartBossFightTrigger
    {
    public:
        BroodlordStartFightTrigger(PlayerbotAI* ai) : StartBossFightTrigger(ai, "start broodlord fight", "broodlord", 12017) {}
    };

    class BroodlordEndFightTrigger : public EndBossFightTrigger
    {
    public:
        BroodlordEndFightTrigger(PlayerbotAI* ai) : EndBossFightTrigger(ai, "end broodlord fight", "broodlord", 12017) {}
    };

    class NefarianStartFightTrigger : public StartBossFightTrigger
    {
    public:
        NefarianStartFightTrigger(PlayerbotAI* ai) : StartBossFightTrigger(ai, "start nefarian fight", "nefarian", 11583) {}
    };

    class NefarianEndFightTrigger : public EndBossFightTrigger
    {
    public:
        NefarianEndFightTrigger(PlayerbotAI* ai) : EndBossFightTrigger(ai, "end nefarian fight", "nefarian", 11583) {}
    };

    // Broodlord Blast Wave: ranged non-victims inside 18y step out
    // (mod-playerbots parity). Victims hold — no kiting through the room.
    class BroodlordRangedTrigger : public Trigger
    {
    public:
        BroodlordRangedTrigger(PlayerbotAI* ai, std::string name = "broodlord ranged", int checkInterval = 1)
        : Trigger(ai, name, checkInterval) {}

        bool IsActive() override
        {
            if (!bot->IsInWorld() || bot->IsBeingTeleported() || !sServerFacade.IsAlive(bot))
                return false;
            if (!ai->HasStrategy("broodlord", BotState::BOT_STATE_COMBAT))
                return false;
            if (!ai->IsRanged(bot))
                return false;
            AiObjectContext* context = ai->GetAiObjectContext();
            const std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
            for (const ObjectGuid& attackerGuid : attackers)
            {
                Unit* attacker = ai->GetUnit(attackerGuid);
                if (!attacker || attacker->GetEntry() != 12017)
                    continue;
                if (attacker->GetVictim() && attacker->GetVictim()->getObjectGuid() == bot->getObjectGuid())
                    return false;
                return bot->IsWithinDist(attacker, 18.0f);
            }
            return false;
        }
    };

    // Nefarian Wild Magic on a mage → Ice Block (mod-playerbots parity).
    class NefarianWildMagicTrigger : public Trigger
    {
    public:
        NefarianWildMagicTrigger(PlayerbotAI* ai, std::string name = "nefarian wild magic", int checkInterval = 1)
        : Trigger(ai, name, checkInterval) {}

        bool IsActive() override
        {
            if (!bot->IsInWorld() || bot->IsBeingTeleported() || !sServerFacade.IsAlive(bot))
                return false;
            if (!ai->HasStrategy("nefarian", BotState::BOT_STATE_COMBAT))
                return false;
            if (bot->GetClass() != CLASS_MAGE)
                return false;
            return bot->HasAura(23410);
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