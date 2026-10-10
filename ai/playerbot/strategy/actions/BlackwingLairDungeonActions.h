#pragma once
#include "playerbot/PlayerbotAI.h"
#include "DungeonActions.h"
#include "ChangeStrategyAction.h"
#include "MovementActions.h"
#include "UseItemAction.h"
#include "playerbot/strategy/values/GuidPositionValues.h"
#include "playerbot/RazorgorePolicy.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/AiObjectContext.h"

namespace ai
{
    const uint32 SPELL_DISARM_TRAP = 1842;

    class BlackwingLairEnableDungeonStrategyAction : public ChangeAllStrategyAction
    {
    public:
        BlackwingLairEnableDungeonStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable blackwing lair strategy", "+blackwing lair") {}
    };

    class BlackwingLairDisableDungeonStrategyAction : public ChangeAllStrategyAction
    {
    public:
        BlackwingLairDisableDungeonStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable blackwing lair strategy", "-blackwing lair") {}
    };

    class MoveToSuppressionDeviceAction : public MovementAction
    {
    public:
        MoveToSuppressionDeviceAction(PlayerbotAI* ai) : MovementAction(ai, "move to suppression device") {}

        bool Execute(Event& event) override
        {
            std::list<GuidPosition> gos = AI_VALUE(std::list<GuidPosition>, "go usable filter::go trapped filter::entry filter::{gos in sight,suppression devices}");

            if (gos.empty())
                return false;

            WorldPosition botPos(bot);
            GuidPosition closest;
            float closestDist = FLT_MAX;

            for (const GuidPosition& gp : gos)
            {
                float dist = botPos.distance(gp);
                if (dist < closestDist)
                {
                    closestDist = dist;
                    closest = gp;
                }
            }

            if (!closest)
                return false;

            if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
            {
                ai->TellPlayerNoFacing(GetMaster(), "Moving to Suppression Device at " + std::to_string((int)closestDist) + " yards");
            }

            return MoveTo(closest.GetMapId(), closest.getX(), closest.getY(), closest.getZ());
        }

        bool isPossible() override
        {
            return ai->CanMove();
        }

        bool isUseful() override
        {
            std::list<GuidPosition> gos = AI_VALUE(std::list<GuidPosition>, "go usable filter::go trapped filter::entry filter::{gos in sight,suppression devices}");
            return !gos.empty();
        }
    };

    class StealthForSuppressionDeviceAction : public Action
    {
    public:
        StealthForSuppressionDeviceAction(PlayerbotAI* ai) : Action(ai, "stealth for suppression device") {}

        bool Execute(Event& event) override
        {
            if (bot->GetClass() != CLASS_ROGUE)
                return false;

            if (ai->HasAura("stealth", bot))
                return false;

            if (ai->CastSpell("stealth", bot))
            {
                ai->ChangeStrategy("+stealthed", BotState::BOT_STATE_COMBAT);
                ai->ChangeStrategy("+stealthed", BotState::BOT_STATE_NON_COMBAT);
                bot->InterruptSpell(CURRENT_MELEE_SPELL);
                return true;
            }

            return false;
        }

        bool isPossible() override
        {
            return bot->GetClass() == CLASS_ROGUE && !ai->HasAura("stealth", bot);
        }

        bool isUseful() override
        {
            if (ai->HasAura("stealth", bot))
                return false;

            // Core rogue stealth logic had some WSG/EYE flag checks, added in here too just in case
            return !ai->HasAura(23333, bot) && !ai->HasAura(23335, bot);
        }
    };

    class DeactivateSuppressionDeviceAction : public Action
    {
    public:
        DeactivateSuppressionDeviceAction(PlayerbotAI* ai) : Action(ai, "deactivate suppression device") {}

        bool Execute(Event& event) override
        {
            std::list<GuidPosition> gos = AI_VALUE(std::list<GuidPosition>, "entry filter::{gos close,suppression devices}");

            if (gos.empty())
                return false;

            for (const GuidPosition& guidPos : gos)
            {
                GameObject* go = ai->GetGameObject(guidPos);
                if (!go)
                    continue;

                if (go->getLootState() != GO_READY)
                    continue;

                if (!bot->GetGameObjectIfCanInteractWith(go->getObjectGuid(), GAMEOBJECT_TYPE_TRAP))
                    continue;

                std::unique_ptr<WorldPacket> packet(new WorldPacket(CMSG_GAMEOBJ_USE));
                *packet << go->getObjectGuid();
                bot->GetSession()->QueuePacket(packet.release());

                if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
                {
                    ai->TellPlayerNoFacing(GetMaster(), "Deactivating Suppression Device");
                }

                return true;
            }

            return false;
        }

        bool isPossible() override
        {
            return ai->CanMove();
        }
    };

    class DisarmSuppressionDeviceAction : public Action
    {
    public:
        DisarmSuppressionDeviceAction(PlayerbotAI* ai) : Action(ai, "disarm suppression device") {}

        bool Execute(Event& event) override
        {
            if (bot->GetClass() != CLASS_ROGUE)
                return false;

            if (!bot->HasSpell(SPELL_DISARM_TRAP))
                return false;

            std::list<GuidPosition> gos = AI_VALUE(std::list<GuidPosition>, "go usable filter::go trapped filter::entry filter::{gos close,suppression devices}");

            if (gos.empty())
                return false;

            WorldPosition botPos(bot);
            GameObject* closestGo = nullptr;
            float closestDist = FLT_MAX;

            for (const GuidPosition& guidPos : gos)
            {
                GameObject* go = ai->GetGameObject(guidPos);
                if (!go)
                    continue;

                if (go->getLootState() != GO_READY)
                    continue;

                float dist = botPos.distance(WorldPosition(go));
                if (dist < closestDist)
                {
                    closestDist = dist;
                    closestGo = go;
                }
            }

            if (!closestGo)
                return false;

            if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
            {
                ai->TellPlayerNoFacing(GetMaster(), "Casting Disarm Trap on Suppression Device");
            }

            return ai->CastSpell(SPELL_DISARM_TRAP, closestGo);
        }

        bool isPossible() override
        {
            return bot->GetClass() == CLASS_ROGUE &&
                   bot->HasSpell(SPELL_DISARM_TRAP) &&
                   ai->CanMove();
        }

        bool isUseful() override
        {
            std::list<GuidPosition> gos = AI_VALUE(std::list<GuidPosition>, "go usable filter::go trapped filter::entry filter::{gos close,suppression devices}");
            return !gos.empty();
        }
    };

    class RazorgoreEnableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        RazorgoreEnableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable razorgore fight strategy", "+razorgore") {}
    };

    class RazorgoreDisableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        RazorgoreDisableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable razorgore fight strategy", "-razorgore") {}
    };

    // Cone escape: donor AvoidAoe geometry — step to directly behind the
    // boss (melee 3y, ranged 15y) with a small angular fuzz so the raid
    // does not stack one spot. Custom Execute (not MoveAwayFromCreature,
    // which flees radially outward and strands melee at 17y+ with no
    // uptime).
    class RazorgoreEscapeConeAction : public MovementAction
    {
    public:
        RazorgoreEscapeConeAction(PlayerbotAI* ai) : MovementAction(ai, "escape razorgore cone") {}
        bool isPossible() override { return MovementAction::isPossible() && ai->CanMove(); }

        bool Execute(Event& event) override
        {
            (void)event;
            AiObjectContext* context = ai->GetAiObjectContext();
            const std::list<ObjectGuid>& attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
            Unit* boss = nullptr;
            for (const ObjectGuid& attackerGuid : attackers)
            {
                Unit* attacker = ai->GetUnit(attackerGuid);
                if (attacker && attacker->GetEntry() == kRazorgoreEntry)
                {
                    boss = attacker;
                    break;
                }
            }
            if (!boss || !sServerFacade.IsAlive(boss))
                return false;
            // The victim holds — moving rotates the boss into the raid.
            if (boss->GetVictim() && boss->GetVictim()->getObjectGuid() == bot->getObjectGuid())
                return false;
            // Donor geometry: directly behind the boss + small fuzz;
            // melee 3y, ranged 15y (War Stomp spacing).
            float fuzz = frand(-M_PI_F / 4.0f, M_PI_F / 4.0f);
            float moveAngle = boss->GetOrientation() + (float)M_PI + fuzz;
            float radius = ai->IsRanged(bot) ? kRazorgoreRangedDistance : kRazorgoreMeleeDistance;
            float tx = boss->GetPositionX() + radius * cos(moveAngle);
            float ty = boss->GetPositionY() + radius * sin(moveAngle);
            WorldPosition dest(bot->GetMapId(), tx, ty, bot->GetPositionZ());
            dest.setZ(dest.GetHeight());
            if (!bot->IsWithinLOS(dest.getX(), dest.getY(), dest.getZ() + bot->GetCollisionHeight()))
                return false;
            return MoveTo(bot->GetMapId(), dest.getX(), dest.getY(), dest.getZ(), false, IsReaction(), false, true);
        }
    };

    // Off-tank engage: attack Razorgore while eggs live (the donor MarkBoss
    // attack arm; the moon mark itself stays dropped — generic mark rti
    // covers marking).
    class RazorgoreEngageAction : public AttackAction
    {
    public:
        RazorgoreEngageAction(PlayerbotAI* ai) : AttackAction(ai, "razorgore engage") {}
        std::string GetTargetName() override { return "current target"; }

        bool Execute(Event& event) override
        {
            AiObjectContext* context = ai->GetAiObjectContext();
            const std::list<ObjectGuid>& attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
            Unit* boss = nullptr;
            for (const ObjectGuid& attackerGuid : attackers)
            {
                Unit* attacker = ai->GetUnit(attackerGuid);
                if (attacker && attacker->GetEntry() == kRazorgoreEntry && sServerFacade.IsAlive(attacker))
                {
                    boss = attacker;
                    break;
                }
            }
            if (!boss)
                return false;
            // Eggs dead: release back to normal target selection.
            bool eggsAlive = false;
            const std::list<ObjectGuid> nearestGos = AI_VALUE(std::list<ObjectGuid>, "nearest game objects");
            for (const ObjectGuid& goGuid : nearestGos)
            {
                GameObject* go = ai->GetGameObject(goGuid);
                if (go && go->GetEntry() == kBlackDragonEggEntry)
                {
                    eggsAlive = true;
                    break;
                }
            }
            if (!eggsAlive)
                return false;
            Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
            return Attack(requester, boss);
        }
    };

    class RazorgoreBackOffAction : public MoveAwayFromCreature
    {
    public:
        RazorgoreBackOffAction(PlayerbotAI* ai) : MoveAwayFromCreature(ai, "back off razorgore", kRazorgoreEntry, kRazorgoreRangedDistance) {}
    };
}
