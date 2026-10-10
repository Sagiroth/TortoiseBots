#pragma once
#include "DungeonTriggers.h"
#include "GenericTriggers.h"
#include "playerbot/GolemaggPolicy.h"

namespace ai
{
    class MoltenCoreEnterDungeonTrigger : public EnterDungeonTrigger
    {
    public:
        MoltenCoreEnterDungeonTrigger(PlayerbotAI* ai) : EnterDungeonTrigger(ai, "enter molten core", "molten core", 409) {}
    };

    class MoltenCoreLeaveDungeonTrigger : public LeaveDungeonTrigger
    {
    public:
        MoltenCoreLeaveDungeonTrigger(PlayerbotAI* ai) : LeaveDungeonTrigger(ai, "leave molten core", "molten core", 409) {}
    };

    class MagmadarStartFightTrigger : public StartBossFightTrigger
    {
    public:
        MagmadarStartFightTrigger(PlayerbotAI* ai) : StartBossFightTrigger(ai, "start magmadar fight", "magmadar", 11982) {}
    };

    class MagmadarEndFightTrigger : public EndBossFightTrigger
    {
    public:
        MagmadarEndFightTrigger(PlayerbotAI* ai) : EndBossFightTrigger(ai, "end magmadar fight", "magmadar", 11982) {}
    };

    class GeddonStartFightTrigger : public StartBossFightTrigger
    {
    public:
        GeddonStartFightTrigger(PlayerbotAI* ai) : StartBossFightTrigger(ai, "start geddon fight", "geddon", 12056) {}
    };

    class GeddonEndFightTrigger : public EndBossFightTrigger
    {
    public:
        GeddonEndFightTrigger(PlayerbotAI* ai) : EndBossFightTrigger(ai, "end geddon fight", "geddon", 12056) {}
    };

    // Baron Geddon Inferno: while Geddon carries the Inferno aura (19695)
    // everyone runs 20y out (mod-playerbots parity). Living Bomb needs no
    // trigger: the universal "raid bomb debuff" runout already covers it.
    class GeddonInfernoTrigger : public Trigger
    {
    public:
        GeddonInfernoTrigger(PlayerbotAI* ai, std::string name = "geddon inferno", int checkInterval = 1)
        : Trigger(ai, name, checkInterval) {}
        bool IsActive() override;
    };

    class MagmadarLavaBombTrigger : public CloseToGameObjectHazardTrigger
    {
    public:
        MagmadarLavaBombTrigger(PlayerbotAI* ai) : CloseToGameObjectHazardTrigger(ai, "magmadar lava bomb", 177704, 5.0f, 60) {}
    };

    class GolemaggStartFightTrigger : public StartBossFightTrigger
    {
    public:
        GolemaggStartFightTrigger(PlayerbotAI* ai) : StartBossFightTrigger(ai, "start golemagg fight", "golemagg", 11988) {}
    };

    class GolemaggEndFightTrigger : public EndBossFightTrigger
    {
    public:
        GolemaggEndFightTrigger(PlayerbotAI* ai) : EndBossFightTrigger(ai, "end golemagg fight", "golemagg", 11988) {}
    };

    // Magma Splash back-off: non-tanks at 20+ stacks step 12y out unless
    // the burn phase (<10%) has started (mod-playerbots parity).
    class GolemaggSplashTrigger : public Trigger
    {
    public:
        GolemaggSplashTrigger(PlayerbotAI* ai, std::string name = "golemagg splash", int checkInterval = 1)
        : Trigger(ai, name, checkInterval) {}

        bool IsActive() override
        {
            if (!bot->IsInWorld() || bot->IsBeingTeleported() || !sServerFacade.IsAlive(bot))
                return false;
            if (!ai->HasStrategy("golemagg", BotState::BOT_STATE_COMBAT))
                return false;
            if (ai->IsTank(bot))
                return false;
            AiObjectContext* context = ai->GetAiObjectContext();
            const std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
            for (const ObjectGuid& attackerGuid : attackers)
            {
                Unit* attacker = ai->GetUnit(attackerGuid);
                if (!attacker || attacker->GetEntry() != 11988)
                    continue;
                if (attacker->GetHealthPercent() <= 10.0f)
                    return false;
                Aura* splash = ai->GetAura(13880, bot);
                int stacks = splash ? (int)splash->GetStackAmount() : 0;
                if (!ShouldBackOffSplash(false, stacks, (float)attacker->GetHealthPercent()))
                    return false;
                // Donor shape: strict 2D ground distance. IsWithinDist adds
                // combat-reach padding (Golemagg is huge), which fires the
                // trigger where the 12y move search finds nothing.
                return bot->GetDistance2d(attacker) < kMagmaSplashBackOffDistance;
            }
            return false;
        }
    };

    // Healer midpoint: healers work from the camp midpoint so both tank
    // camps stay in range (mod-playerbots parity).
    class GolemaggHealerTrigger : public Trigger
    {
    public:
        GolemaggHealerTrigger(PlayerbotAI* ai, std::string name = "golemagg healer", int checkInterval = 5)
        : Trigger(ai, name, checkInterval) {}

        bool IsActive() override
        {
            if (!bot->IsInWorld() || bot->IsBeingTeleported() || !sServerFacade.IsAlive(bot))
                return false;
            if (!ai->HasStrategy("golemagg", BotState::BOT_STATE_COMBAT))
                return false;
            if (!ai->IsHeal(bot))
                return false;
            return bot->GetDistance2d(821.2f, -1007.0f) > 8.0f;
        }
    };

    // Tank camp hold: fires for tanks while the fight lives and Trust
    // is up on a rager (the split is still on). The action itself holds
    // position when already at camp.
    class GolemaggTankHoldTrigger : public Trigger
    {
    public:
        GolemaggTankHoldTrigger(PlayerbotAI* ai, std::string name = "golemagg tank hold", int checkInterval = 5)
        : Trigger(ai, name, checkInterval) {}

        bool IsActive() override
        {
            if (!bot->IsInWorld() || bot->IsBeingTeleported() || !sServerFacade.IsAlive(bot))
                return false;
            if (!ai->HasStrategy("golemagg", BotState::BOT_STATE_COMBAT))
                return false;
            if (!ai->IsTank(bot))
                return false;
            AiObjectContext* context = ai->GetAiObjectContext();
            const std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
            for (const ObjectGuid& attackerGuid : attackers)
            {
                Unit* attacker = ai->GetUnit(attackerGuid);
                if (!attacker || attacker->GetEntry() != 11672)
                    continue;
                if (ai->HasAura(20553, attacker))
                    return true;
            }
            return false;
        }
    };

    class MagmadarTooCloseTrigger : public CloseToCreatureTrigger
    {
    public:
        MagmadarTooCloseTrigger(PlayerbotAI* ai) : CloseToCreatureTrigger(ai, "magmadar too close", 11982, 30.0f) {}
    };

    class FireProtectionPotionReadyTrigger : public ItemBuffReadyTrigger
    {
    public:
        FireProtectionPotionReadyTrigger(PlayerbotAI* ai) : ItemBuffReadyTrigger(ai, "fire protection potion ready", 13457, 17543) {}
    };

    class MCRuneInSightTrigger : public ValueTrigger
    {
    public:
        MCRuneInSightTrigger(PlayerbotAI* ai) : ValueTrigger(ai, "mc rune in sight", 1)
        {
            qualifier = "and::{"
                "action possible::use id::17333,"
                "has object::go usable filter::go trapped filter::entry filter::{gos in sight,mc runes},"
                "not::has object::entry filter::{gos close,mc runes}"
                "}";
        }
    };

    class MCRuneCloseTrigger : public ValueTrigger
    {
    public:
        MCRuneCloseTrigger(PlayerbotAI* ai) : ValueTrigger(ai, "mc rune close", 1) { qualifier = "has object::go usable filter::entry filter::{gos close,mc runes}"; }
    };
}