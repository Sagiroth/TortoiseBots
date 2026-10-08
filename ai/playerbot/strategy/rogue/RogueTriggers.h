#pragma once
#include "playerbot/strategy/triggers/GenericTriggers.h"

namespace ai
{
    class KickInterruptSpellTrigger : public InterruptSpellTrigger
    {
    public:
        KickInterruptSpellTrigger(PlayerbotAI* ai) : InterruptSpellTrigger(ai, "kick") {}
    };

    CAN_CAST_TRIGGER_A(RiposteCastTrigger, "riposte");
    CAN_CAST_TRIGGER_A(SurpriseAttackTrigger, "surprise attack");
    CAN_CAST_TRIGGER(BackstabTrigger, "backstab");
    CAN_CAST_TRIGGER(NoxiousAssaultTrigger, "noxious assault");

    class EnvenomTrigger : public NoBuffAndComboPointsAvailableTrigger
    {
    public:
        EnvenomTrigger(PlayerbotAI* ai, uint8 comboPoints = 3) : NoBuffAndComboPointsAvailableTrigger(ai, "envenom", comboPoints) {}
    };

    class ShadowOfDeathTrigger : public SpellCanBeCastedTrigger
    {
    public:
        ShadowOfDeathTrigger(PlayerbotAI* ai) : SpellCanBeCastedTrigger(ai, "shadow of death") {}
        bool IsActive() override;
    };

    class MarkForDeathTrigger : public SpellCanBeCastedTrigger
    {
    public:
        MarkForDeathTrigger(PlayerbotAI* ai) : SpellCanBeCastedTrigger(ai, "mark for death") {}
        bool IsActive() override;
    };

    class SliceAndDiceTrigger : public NoBuffAndComboPointsAvailableTrigger
    {
    public:
        SliceAndDiceTrigger(PlayerbotAI* ai, uint8 comboPoints = 3) : NoBuffAndComboPointsAvailableTrigger(ai, "slice and dice", comboPoints) {}
    };

    class RogueBoostBuffTrigger : public BoostTrigger
    {
    public:
        RogueBoostBuffTrigger(PlayerbotAI* ai, std::string spellName) : BoostTrigger(ai, spellName, 200.0f) {}
        virtual bool IsActive() override { if (ai->HasAura("stealth", bot)) return false; return BoostTrigger::IsActive(); }
    };

    class RuptureTrigger : public NoDebuffAndComboPointsAvailableTrigger
    {
    public:
        RuptureTrigger(PlayerbotAI* ai, uint8 comboPoints = 4) : NoDebuffAndComboPointsAvailableTrigger(ai, "rupture", comboPoints) {}
    };

    class EviscerateTrigger : public ComboPointsAvailableTrigger
    {
    public:
        EviscerateTrigger(PlayerbotAI* ai, uint8 comboPoints = 4) : ComboPointsAvailableTrigger(ai, comboPoints) {}
    };

    // Almost-dead finisher dump (mod-playerbots DpsRogueStrategy "target with
    // combo points almost dead" -> eviscerate HIGH+2, no CP gate): a leveling
    // mob that will die in the next swing should eat whatever combo points
    // are banked instead of dying with them. Any CP (1+) qualifies; the
    // regular 4CP eviscerate node below still owns healthy targets.
    class AlmostDeadFinisherTrigger : public Trigger
    {
    public:
        AlmostDeadFinisherTrigger(PlayerbotAI* ai) : Trigger(ai, "target with combo points almost dead") {}
        bool IsActive() override;
    };


    class ExposeArmorTrigger : public NoDebuffAndComboPointsAvailableTrigger
    {
    public:
        ExposeArmorTrigger(PlayerbotAI* ai, uint8 comboPoints = 3) : NoDebuffAndComboPointsAvailableTrigger(ai, "expose armor", comboPoints) {}
    };

    class KickInterruptEnemyHealerSpellTrigger : public InterruptEnemyHealerTrigger
    {
    public:
        KickInterruptEnemyHealerSpellTrigger(PlayerbotAI* ai) : InterruptEnemyHealerTrigger(ai, "kick") {}
    };

    class InStealthTrigger : public HasAuraTrigger
    {
    public:
        InStealthTrigger(PlayerbotAI* ai) : HasAuraTrigger(ai, "stealth") {}
    };

    class NoStealthTrigger : public HasNoAuraTrigger
    {
    public:
        NoStealthTrigger(PlayerbotAI* ai) : HasNoAuraTrigger(ai, "stealth")
        {
            checkInterval = 2;
        }
    };

    class RogueUnstealthTrigger : public BuffTrigger
    {
    public:
        RogueUnstealthTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "stealth", 2) {}

        bool IsActive() override
        {
            if (ai->HasAura("stealth", bot) && !bot->InBattleGround())
            {
                if (!AI_VALUE(bool, "has attackers") && !AI_VALUE(bool, "has enemy player targets"))
                {
                    if (AI_VALUE2(bool, "moving", "self target"))
                    {
                        if (ai->GetMaster())
                        {
                            return sServerFacade.IsDistanceGreaterThan(AI_VALUE2(float, "distance", "master target"), 10.0f);
                        }
                        else
                        {
                            return true;
                        }
                    }
                }
            }

            return false;
        }
    };

    class StealthTrigger : public Trigger
    {
    public:
        StealthTrigger(PlayerbotAI* ai) : Trigger(ai, "stealth") {}

        virtual bool IsActive() override
        {
            if (ai->HasAura("stealth", bot) || sServerFacade.IsInCombat(bot) || !sServerFacade.IsSpellReady(bot, 1784))
            {
                return false;
            }

            Unit* target = AI_VALUE(Unit*, "enemy player target");
            if (!target)
            {
                target = AI_VALUE(Unit*, "grind target");
            }
            if (!target)
            {
                target = AI_VALUE(Unit*, "dps target");
            }
            if (!target)
            {
                return false;
            }

            float distance = 30.0f;
            if (target && target->GetVictim())
            {
                distance -= 10;
            }

            if (sServerFacade.isMoving(target) && target->GetVictim())
            {
                distance -= 10;
            }

            if (bot->InBattleGround())
            {
                distance += 20;
            }


            return (target && sServerFacade.getDistance2d(bot, target) < distance);
        }
    };

    class SapTrigger : public HasCcTargetTrigger
    {
    public:
        SapTrigger(PlayerbotAI* ai) : HasCcTargetTrigger(ai, "sap") {}

        virtual bool IsPossible()
        {
            return bot->GetLevel() > 10 && bot->HasSpell(6770) && !sServerFacade.IsInCombat(bot);
        }
    };

    class SprintTrigger : public BuffTrigger
    {
    public:
        SprintTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "sprint", 2) {}

        virtual bool IsActive() override
        {
            if (!sServerFacade.IsSpellReady(bot, 2983))
            {
                return false;
            }

            float distance = ai->GetMaster() ? 45.0f : 35.0f;
            if (ai->HasAura("stealth", bot))
            {
                distance -= 10;
            }

            bool targeted = false;

            Unit* dps = AI_VALUE(Unit*, "dps target");
            if (dps)
            {
                targeted = (dps == AI_VALUE(Unit*, "current target"));
            }

            Unit* enemyPlayer = AI_VALUE(Unit*, "enemy player target");
            if (enemyPlayer && !targeted)
            {
                targeted = (enemyPlayer == AI_VALUE(Unit*, "current target"));
            }

            // use sprint on players
            if (enemyPlayer && !bot->CanReachWithMeleeAutoAttack(enemyPlayer))
            {
                return true;
            }

            if (!targeted)
            {
                return false;
            }

            if ((dps && sServerFacade.IsInCombat(dps)) || (enemyPlayer))
            {
                distance -= 10;
            }

            return  AI_VALUE2(bool, "moving", "self target") &&
                    (AI_VALUE2(bool, "moving", "dps target") ||
                    AI_VALUE2(bool, "moving", "enemy player target")) &&
                    targeted &&
                    (sServerFacade.IsDistanceGreaterThan(AI_VALUE2(float, "distance", "dps target"), distance) ||
                     sServerFacade.IsDistanceGreaterThan(AI_VALUE2(float, "distance", "enemy player target"), distance));
        }
    };

    class ApplyPoisonTrigger : public Trigger
    {
    public:
        ApplyPoisonTrigger(PlayerbotAI* ai, bool inMainHand, const std::vector<uint32>& inPoisonEnchantIds, const std::vector<uint32>& inPoisonItemIds, std::string name = "apply poison")
        : Trigger(ai, name, 5)
        , mainHand(inMainHand)
        , poisonEnchantIds(inPoisonEnchantIds)
        , poisonItemIds(inPoisonItemIds) {}

        bool HasPoisonItem() const
        {
            if (ai->HasCheat(BotCheatMask::item))
                return true;
            for (const uint32 itemId : poisonItemIds)
            {
                const ItemPrototype* proto = sObjectMgr.GetItemPrototype(itemId);
                if (proto && (bot->GetLevel() >= proto->RequiredLevel))
                {
                    if (bot->HasItemCount(itemId, 1))
                        return true;
                }
            }
            return false;
        }

        bool IsActive() override
        {
            if (!HasPoisonItem())
                return false;

            Item* weapon = mainHand ? bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND) : bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
            if (weapon)
            {
                const uint32 currentEnchantId = weapon->GetEnchantmentId(TEMP_ENCHANTMENT_SLOT);
                if(currentEnchantId != 0)
                {
                    // Check if the current poison is the poison we need
                    return std::find(poisonEnchantIds.begin(), poisonEnchantIds.end(), currentEnchantId) == poisonEnchantIds.end();
                }

                return true;
            }

            return false;
        }

    private:
        bool mainHand;
        std::vector<uint32> poisonEnchantIds;
        std::vector<uint32> poisonItemIds;
    };

    class ApplyDeadlyPoisonTrigger : public ApplyPoisonTrigger
    {
    public:
        ApplyDeadlyPoisonTrigger(PlayerbotAI* ai, bool inMainHand) : ApplyPoisonTrigger(ai, inMainHand, { 7, 8, 626, 627, 2630, 2642, 2643, 3770, 3771 }, { 2892, 2893, 8984, 8985, 20844, 22053, 22054 }, "apply deadly poison main hand") {}
    };

    class ApplyCripplingPoisonTrigger : public ApplyPoisonTrigger
    {
    public:
        ApplyCripplingPoisonTrigger(PlayerbotAI* ai, bool inMainHand) : ApplyPoisonTrigger(ai, inMainHand, { 22, 603 }, { 3775, 3776 }, "apply crippling poison main hand") {}
    };

    class ApplyMindPoisonTrigger : public ApplyPoisonTrigger
    {
    public:
        ApplyMindPoisonTrigger(PlayerbotAI* ai, bool inMainHand) : ApplyPoisonTrigger(ai, inMainHand, { 35, 23, 643 }, { 5237, 6951, 9186 }, "apply mind poison main hand") {}
    };

    class ApplyInstantPoisonTrigger : public ApplyPoisonTrigger
    {
    public:
        ApplyInstantPoisonTrigger(PlayerbotAI* ai, bool inMainHand) : ApplyPoisonTrigger(ai, inMainHand, { 323, 324, 325, 623, 624, 625, 2641, 3768, 3769 }, { 6947, 6949, 6950, 8926, 8927, 8928, 21927 }, "apply instant poison main hand") {}
    };

    class ApplyWoundPoisonTrigger : public ApplyPoisonTrigger
    {
    public:
        ApplyWoundPoisonTrigger(PlayerbotAI* ai, bool inMainHand) : ApplyPoisonTrigger(ai, inMainHand, { 703, 704, 705, 706, 2644, 3772, 3773 }, { 10918, 10920, 10921, 10922, 22055 }, "apply wound poison main hand") {}
    };

    // Donor parity (mod-playerbots RogueTriggers.h): plain "is the weapon
    // unenchanted" signals. The per-poison Apply* triggers above cover
    // expiry/rank/wrong-poison inside the spec pve/pvp/raid strategies; these
    // two drive the generic open-world upkeep (instant MH, deadly OH).
    class MainHandWeaponNoEnchantTrigger : public BuffTrigger
    {
    public:
        MainHandWeaponNoEnchantTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "main hand", 1) {}
        bool IsActive() override;
    };

    class OffHandWeaponNoEnchantTrigger : public BuffTrigger
    {
    public:
        OffHandWeaponNoEnchantTrigger(PlayerbotAI* ai) : BuffTrigger(ai, "off hand", 1) {}
        bool IsActive() override;
    };

}
