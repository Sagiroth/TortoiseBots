#pragma once

#include "playerbot/strategy/actions/GenericActions.h"
#include "playerbot/strategy/actions/UseItemAction.h"
#include "playerbot/AoeFearPolicy.h"
#include "../../../runtime/SeductionPolicy.h"

namespace ai
{
	SNARE_ACTION(CastDeathCoilSnareAction, "death coil");
	ENEMY_HEALER_ACTION(CastDeathCoilOnHealerAction, "death coil");
	SPELL_ACTION(CastDeathCoilAction, "death coil");
    BUFF_ACTION(CastShadowWardAction, "shadow ward");

	class CastDemonSkinAction : public CastBuffSpellAction
	{
	public:
		CastDemonSkinAction(PlayerbotAI* ai) : CastBuffSpellAction(ai, "demon skin") {}
	};

	class CastDemonArmorAction : public CastBuffSpellAction
	{
	public:
		CastDemonArmorAction(PlayerbotAI* ai) : CastBuffSpellAction(ai, "demon armor") {}
	};

    BEGIN_RANGED_SPELL_ACTION(CastShadowBoltAction, "shadow bolt")
    END_SPELL_ACTION()

    class CastDrainSoulAction : public CastSpellAction
    {
    public:
        CastDrainSoulAction(PlayerbotAI* ai) : CastSpellAction(ai, "drain soul") {}
    };

    class CastCreateSoulShardAction : public CastSpellAction
    {
    public:
        CastCreateSoulShardAction(PlayerbotAI* ai) : CastSpellAction(ai, "create soul shard") {}
        bool Execute(Event& event) override { return CastSpellAction::Execute(event); }
        // Organic-only: Create Soul Shard exists in the DB (23464/24827) but
        // is not a trained spell, so isPossible() stays false while Drain Soul
        // is the single real harvest path (see the DrainSoulTrigger note).
        bool isUseful() override { return false; }
    };

    class DestroySoulShardAction : public Action
    {
    public:
        DestroySoulShardAction(PlayerbotAI* ai) : Action(ai, "destroy soul shard") {}
        bool Execute(Event& event) override
        {
            (void)event;
            uint32 count = bot->GetItemCount(6265);
            if (count <= 5)
                return false;
            bot->DestroyItemCount(6265, count - 5, true);
            return true;
        }
    };

	class CastDarkHarvestAction : public CastSpellAction
	{
	public:
		CastDarkHarvestAction(PlayerbotAI* ai) : CastSpellAction(ai, "dark harvest") {}
	};

	class CastPowerOverwhelmingAction : public CastSpellAction
	{
	public:
		CastPowerOverwhelmingAction(PlayerbotAI* ai) : CastSpellAction(ai, "power overwhelming") {}
		std::string GetTargetName() override { return "pet target"; }

		bool isUseful() override
		{
			// The demon takes % base health as damage over the duration:
			// refuse a burst that would finish off a dying pet. The trigger
			// holds the same floor for the evaluation-to-execution gap.
			Unit* pet = AI_VALUE(Unit*, "pet target");
			if (!pet || !pet->IsAlive())
				return false;
			return CastSpellAction::isUseful();
		}
	};

    class CastShadowburnAction : public CastSpellAction
    {
    public:
		CastShadowburnAction(PlayerbotAI* ai) : CastSpellAction(ai, "shadowburn") {}

        bool Execute(Event& event) override
        {
            if (CastSpellAction::Execute(event))
            {
                return true;
            }

            return false;
        }
    };

    class CastSoulstoneAction : public CastItemTargetAction
    {
    public:
        CastSoulstoneAction(PlayerbotAI* ai) : CastItemTargetAction(ai, "revive targets", true, true) {}
        std::string GetTargetName() override { return "party member to soulstone"; }

    private:
        uint32 GetItemId() override
        {
            uint32 itemId = 0;
            const uint32 level = bot->GetLevel();
            if (level >= 18 && level < 30)
            {
                itemId = 5232;
            }
            else if (level >= 30 && level < 40)
            {
                itemId = 16892;
            }
            else if (level >= 40 && level < 50)
            {
                itemId = 16893;
            }
            else if (level >= 50 && level < 60)
            {
                itemId = 16895;
            }
            else if (level >= 60)
            {
                itemId = 16896;
            }

            return itemId;
        }
    };

    class CastSoulFireAction : public CastSpellAction
    {
    public:
		CastSoulFireAction(PlayerbotAI* ai) : CastSpellAction(ai, "soul fire") {}
    };

	BUFF_ACTION(CastDarkPactAction, "dark pact");

	class CastDrainManaAction : public CastSpellAction
	{
	public:
		CastDrainManaAction(PlayerbotAI* ai) : CastSpellAction(ai, "drain mana") {}
	};

	class CastDrainLifeAction : public CastSpellAction
	{
	public:
		CastDrainLifeAction(PlayerbotAI* ai) : CastSpellAction(ai, "drain life") {}
		bool isUseful() override
		{
			return CastSpellAction::isUseful() && AI_VALUE2(uint8, "health", "self target") < sPlayerbotAIConfig.almostFullHealth;
		}
	};

    class CastCurseOfExhaustionAction : public CastRangedDebuffSpellAction
    {
    public:
		CastCurseOfExhaustionAction(PlayerbotAI* ai) : CastRangedDebuffSpellAction(ai, "curse of exhaustion") {}
    };

	class CastCorruptionAction : public CastRangedDebuffSpellAction
	{
	public:
		CastCorruptionAction(PlayerbotAI* ai) : CastRangedDebuffSpellAction(ai, "corruption") {}
	};

	class CastCorruptionOnAttackerAction : public CastRangedDebuffSpellOnAttackerAction
	{
	public:
	    CastCorruptionOnAttackerAction(PlayerbotAI* ai) : CastRangedDebuffSpellOnAttackerAction(ai, "corruption") {}
	};

    class CastCurseOfAgonyAction : public CastRangedDebuffSpellAction
    {
    public:
        CastCurseOfAgonyAction(PlayerbotAI* ai) : CastRangedDebuffSpellAction(ai, "curse of agony") {}
    };

    class CastCurseOfAgonyOnAttackerAction : public CastRangedDebuffSpellOnAttackerAction
    {
    public:
        CastCurseOfAgonyOnAttackerAction(PlayerbotAI* ai) : CastRangedDebuffSpellOnAttackerAction(ai, "curse of agony") {}
    };

    class CastCurseOfDoomAction : public CastRangedDebuffSpellAction
    {
    public:
		CastCurseOfDoomAction(PlayerbotAI* ai) : CastRangedDebuffSpellAction(ai, "curse of doom") {}
    };

    class CastCurseOfDoomOnAttackerAction : public CastRangedDebuffSpellOnAttackerAction
    {
    public:
		CastCurseOfDoomOnAttackerAction(PlayerbotAI* ai) : CastRangedDebuffSpellOnAttackerAction(ai, "curse of doom") {}
    };

    class CastCurseOfTheElementsAction : public CastRangedDebuffSpellAction
    {
    public:
        CastCurseOfTheElementsAction(PlayerbotAI* ai) : CastRangedDebuffSpellAction(ai, "curse of the elements") {}
    };

    class CastCurseOfTheElementsOnAttackerAction : public CastRangedDebuffSpellOnAttackerAction
    {
    public:
        CastCurseOfTheElementsOnAttackerAction(PlayerbotAI* ai) : CastRangedDebuffSpellOnAttackerAction(ai, "curse of the elements") {}
    };

    class CastCurseOfRecklessnessAction : public CastRangedDebuffSpellAction
    {
    public:
        CastCurseOfRecklessnessAction(PlayerbotAI* ai) : CastRangedDebuffSpellAction(ai, "curse of recklessness") {}
    };

    class CastCurseOfRecklessnessOnAttackerAction : public CastRangedDebuffSpellOnAttackerAction
    {
    public:
        CastCurseOfRecklessnessOnAttackerAction(PlayerbotAI* ai) : CastRangedDebuffSpellOnAttackerAction(ai, "curse of recklessness") {}
    };

    class CastCurseOfWeaknessAction : public CastRangedDebuffSpellAction
    {
    public:
        CastCurseOfWeaknessAction(PlayerbotAI* ai) : CastRangedDebuffSpellAction(ai, "curse of weakness") {}
    };

    class CastCurseOfWeaknessOnAttackerAction : public CastRangedDebuffSpellOnAttackerAction
    {
    public:
        CastCurseOfWeaknessOnAttackerAction(PlayerbotAI* ai) : CastRangedDebuffSpellOnAttackerAction(ai, "curse of weakness") {}
    };

    class CastCurseOfTonguesAction : public CastRangedDebuffSpellAction
    {
    public:
        CastCurseOfTonguesAction(PlayerbotAI* ai) : CastRangedDebuffSpellAction(ai, "curse of tongues") {}
    };

    class CastCurseOfTonguesOnAttackerAction : public CastRangedDebuffSpellOnAttackerAction
    {
    public:
        CastCurseOfTonguesOnAttackerAction(PlayerbotAI* ai) : CastRangedDebuffSpellOnAttackerAction(ai, "curse of tongues") {}
    };

    class CastCurseOfShadowAction : public CastRangedDebuffSpellAction
    {
    public:
        CastCurseOfShadowAction(PlayerbotAI* ai) : CastRangedDebuffSpellAction(ai, "curse of shadow") {}
    };

    class CastCurseOfShadowOnAttackerAction : public CastRangedDebuffSpellOnAttackerAction
    {
    public:
        CastCurseOfShadowOnAttackerAction(PlayerbotAI* ai) : CastRangedDebuffSpellOnAttackerAction(ai, "curse of shadow") {}
    };

    class CastDemonicSacrificeAction : public CastBuffSpellAction
    {
    public:
		CastDemonicSacrificeAction(PlayerbotAI* ai) : CastBuffSpellAction(ai, "demonic sacrifice") {}
    };

    class CastSoulLinkAction : public CastBuffSpellAction
    {
    public:
		CastSoulLinkAction(PlayerbotAI* ai) : CastBuffSpellAction(ai, "soul link") {}
    };

    class CastSacrificeAction : public CastPetSpellAction
    {
    public:
        CastSacrificeAction(PlayerbotAI* ai) : CastPetSpellAction(ai, "sacrifice") {}
        std::string GetTargetName() override { return "self target"; }
    };

    class CastSpellLockAction : public CastPetSpellAction
    {
    public:
        CastSpellLockAction(PlayerbotAI* ai) : CastPetSpellAction(ai, "spell lock") {}
    };

    class CastSpellLockOnEnemyHealerAction : public CastPetSpellAction
    {
    public:
        CastSpellLockOnEnemyHealerAction(PlayerbotAI* ai) : CastPetSpellAction(ai, "spell lock") {}
        virtual std::string GetTargetName() override { return "enemy healer target"; }
        virtual std::string GetTargetQualifier() override { return GetSpellName(); }
        virtual std::string getName() override { return GetSpellName() + " on enemy healer"; }
    };

    class CastTormentAction : public CastPetSpellAction
    {
    public:
        CastTormentAction(PlayerbotAI* ai) : CastPetSpellAction(ai, "torment") {}
        bool isUseful() override
        {
            Unit* target = GetTarget();
            Unit* pet = AI_VALUE(Unit*, "pet target");
            return target && pet && target->GetVictim() != pet;
        }
    };

    class CastBloodPactAction : public CastPetSpellAction
    {
    public:
        CastBloodPactAction(PlayerbotAI* ai) : CastPetSpellAction(ai, "blood pact") {}
        std::string GetTargetName() override { return "self target"; }
        bool isUseful() override { return !ai->HasAura("blood pact", bot); }
    };

    class CastFireboltAction : public CastPetSpellAction
    {
    public:
        CastFireboltAction(PlayerbotAI* ai) : CastPetSpellAction(ai, "firebolt") {}
    };

	class CastSummonImpAction : public CastBuffSpellAction
	{
	public:
		CastSummonImpAction(PlayerbotAI* ai) : CastBuffSpellAction(ai, "summon imp") {}
        std::string GetTargetName() override { return "self target"; }

        bool isUseful() override
        {
            Unit* pet = AI_VALUE(Unit*, "pet target");
            if (pet)
            {
                return pet->GetEntry() != 416;
            }

            return true;
        }
	};

    class CastSummonSuccubusAction : public CastSpellAction
    {
    public:
        CastSummonSuccubusAction(PlayerbotAI* ai) : CastSpellAction(ai, "summon succubus") {}
        std::string GetTargetName() override { return "self target"; }

        bool isUseful() override
        {
            Unit* pet = AI_VALUE(Unit*, "pet target");
            if (pet)
            {
                return pet->GetEntry() != 1863;
            }

            return true;
        }
    };

	class CastSummonFelhunterAction : public CastSpellAction
	{
	public:
		CastSummonFelhunterAction(PlayerbotAI* ai) : CastSpellAction(ai, "summon felhunter") {}
        std::string GetTargetName() override { return "self target"; }

        bool isUseful() override
        {
            Unit* pet = AI_VALUE(Unit*, "pet target");
            if (pet)
            {
                return pet->GetEntry() != 417;
            }

            return true;
        }
	};

    class CastSummonVoidwalkerAction : public CastSpellAction
    {
    public:
        CastSummonVoidwalkerAction(PlayerbotAI* ai) : CastSpellAction(ai, "summon voidwalker") {}
        std::string GetTargetName() override { return "self target"; }

        bool isUseful() override
        {
            Unit* pet = AI_VALUE(Unit*, "pet target");
            if (pet)
            {
                return pet->GetEntry() != 1860;
            }

            return true;
        }
    };

	class CastSummonInfernoAction : public CastSpellAction
	{
	public:
		CastSummonInfernoAction(PlayerbotAI* ai) : CastSpellAction(ai, "inferno") {}
		virtual bool isPossible() { return true; }
	};

	class CastCreateHealthstoneAction : public CastSpellAction
	{
	public:
		CastCreateHealthstoneAction(PlayerbotAI* ai) : CastSpellAction(ai, "create healthstone") {}
        std::string GetTargetName() override { return "self target"; }
	};

        class CastCreateSoulstoneAction : public CastSpellAction
        {
        public:
                CastCreateSoulstoneAction(PlayerbotAI* ai) : CastSpellAction(ai, "create soulstone") {}
        std::string GetTargetName() override { return "self target"; }
        };

	class CastCreateFirestoneAction : public CastSpellAction
	{
	public:
		CastCreateFirestoneAction(PlayerbotAI* ai) : CastSpellAction(ai, "create firestone") {}
        std::string GetTargetName() override { return "self target"; }
	};

	class CastCreateSpellstoneAction : public CastSpellAction
	{
	public:
		CastCreateSpellstoneAction(PlayerbotAI* ai) : CastSpellAction(ai, "create spellstone") {}
        std::string GetTargetName() override { return "self target"; }
	};

    class CastBanishAction : public CastSpellAction
    {
    public:
        CastBanishAction(PlayerbotAI* ai) : CastSpellAction(ai, "banish") {}
        virtual std::string GetTargetName() override { return "snare target"; }
        virtual std::string GetTargetQualifier() override { return GetSpellName(); }
        virtual ActionThreatType getThreatType() { return ActionThreatType::ACTION_THREAT_NONE; }
    };

    class CastBanishOnCcAction : public CastCrowdControlSpellAction
    {
    public:
        CastBanishOnCcAction(PlayerbotAI* ai) : CastCrowdControlSpellAction(ai, "banish") {}
        bool isPossible() override
        {
            // Banish only lands on demons and elementals; the core rejects
            // other types, so firing there wastes mana and the GCD.
            Unit* target = GetTarget();
            if (!target || target->IsPlayer())
                return false;
            uint32 type = target->GetCreatureType();
            if (type != CREATURE_TYPE_DEMON && type != CREATURE_TYPE_ELEMENTAL)
                return false;
            return CastCrowdControlSpellAction::isPossible();
        }
    };

    // PET-2: succubus Seduction as warlock CC for humanoids. Pet-cast (range
    // and cooldown resolve against the demon) with the CC shape: target is
    // the assigned "cc target" and the CC flags mark it for CC discipline.
    // No positioning: isPossible fails while the succubus is out of range,
    // so this only fires when she is already near the mark (owner CC covers
    // the rest). Break-protection comes free — "seduction" is in the
    // breakable-CC list, so pet attacks hold fire once the aura lands.
    class CastSeductionOnCcAction : public CastPetSpellAction
    {
    public:
        CastSeductionOnCcAction(PlayerbotAI* ai) : CastPetSpellAction(ai, "seduction") {}
        std::string GetTargetName() override { return "cc target"; }
        std::string GetTargetQualifier() override { return GetSpellName(); }
        std::string getName() override { return "seduction on cc"; }
        bool IsCrowdControlAction() const override { return true; }
        std::string GetCrowdControlSpellName() const override { return GetSpellName(); }
        // CC, not DPS: banish/fear are THREAT_NONE so the threat multiplier
        // never zeroes them — seduction must match or it fails exactly when
        // the warlock is under pressure.
        ActionThreatType getThreatType() override { return ActionThreatType::ACTION_THREAT_NONE; }
        bool isPossible() override
        {
            // Succubus out on a humanoid mark — the only type the core lets
            // Seduction land on. Firing elsewhere wastes the succubus GCD.
            // Cached-singleton refresh (see the devour actions): the
            // ctor-resolved spellId stays 0 when no succubus is out at first
            // creation, which would fail the pet HasSpell check forever.
            Unit* target = GetTarget();
            // A dotted mark breaks on the first tick and re-seduces forever:
            // refuse it like the free-pick and auto-CC choosers already do.
            if (target && target->HasAuraType(SPELL_AURA_PERIODIC_DAMAGE))
                return false;
            TortoiseBots::SeductionGateInputs gate;
            Unit* pet = AI_VALUE(Unit*, "pet target");
            gate.hasPet = pet != nullptr;
            gate.currentPetEntry = pet ? pet->GetEntry() : 0;
            gate.targetIsPlayer = target && target->IsPlayer();
            gate.targetCreatureType = target ? target->GetCreatureType() : 0;
            if (!TortoiseBots::CanCastSeduction(gate))
                return false;
            SetSpellName("seduction", "spell id", true);
            return CastPetSpellAction::isPossible();
        }
        bool isUseful() override
        {
            Unit* target = GetTarget();
            Unit* pet = AI_VALUE(Unit*, "pet target");
            TortoiseBots::SeductionGateInputs gate;
            gate.hasPet = pet != nullptr;
            gate.currentPetEntry = pet ? pet->GetEntry() : 0;
            gate.targetIsPlayer = target && target->IsPlayer();
            gate.targetCreatureType = target ? target->GetCreatureType() : 0;
            return TortoiseBots::CanCastSeduction(gate) && CastPetSpellAction::isUseful();
        }
    };

    class CastRainOfFireAction : public CastSpellAction
    {
    public:
        CastRainOfFireAction(PlayerbotAI* ai) : CastSpellAction(ai, "rain of fire") {}
    };
    class CastInfernoAction : public CastSpellAction
    {
    public:
        // Inferno (1122) summons an infernal with a stun; the InfernoTrigger
        // gates attackers, spell knowledge, and the infernal stone.
        CastInfernoAction(PlayerbotAI* ai) : CastSpellAction(ai, "inferno") {}
    };

    class CastImmolateAction : public CastRangedDebuffSpellAction
    {
    public:
        CastImmolateAction(PlayerbotAI* ai) : CastRangedDebuffSpellAction(ai, "immolate") {}
    };

    class CastConflagrateAction : public CastSpellAction
    {
    public:
        CastConflagrateAction(PlayerbotAI* ai) : CastSpellAction(ai, "conflagrate") {}
    };

    class CastFearAction : public CastRangedDebuffSpellAction
    {
    public:
        CastFearAction(PlayerbotAI* ai) : CastRangedDebuffSpellAction(ai, "fear") {}
    };

    class CastFearOnCcAction : public CastCrowdControlSpellAction
    {
    public:
        CastFearOnCcAction(PlayerbotAI* ai) : CastCrowdControlSpellAction(ai, "fear") {}
        bool isPossible() override
        {
            // Fear never lands on undead or mechanical targets; the core
            // rejects them, so firing there wastes mana and the GCD.
            Unit* target = GetTarget();
            if (!target || target->IsPlayer())
                return false;
            uint32 type = target->GetCreatureType();
            if (type == CREATURE_TYPE_UNDEAD || type == CREATURE_TYPE_MECHANICAL)
                return false;
            return CastCrowdControlSpellAction::isPossible();
        }
    };

    class CastLifeTapAction: public CastSpellAction
    {
    public:
        CastLifeTapAction(PlayerbotAI* ai) : CastSpellAction(ai, "life tap") {}
        virtual std::string GetTargetName() override { return "self target"; }
        virtual bool isUseful() override { return AI_VALUE2(uint8, "health", "self target") > sPlayerbotAIConfig.lowHealth; }
    };

    class CastAmplifyCurseAction : public CastBuffSpellAction
    {
    public:
        CastAmplifyCurseAction(PlayerbotAI* ai) : CastBuffSpellAction(ai, "amplify curse") {}
    };

    class CastFelDominationAction : public CastBuffSpellAction
    {
    public:
        CastFelDominationAction(PlayerbotAI* ai) : CastBuffSpellAction(ai, "fel domination") {}
        std::string GetTargetName() override { return "self target"; }

        bool isUseful() override
        {
            // Turtle 18708 is a 5-min emergency summon accelerator: only
            // worth burning in combat with a dead pet, never out of combat
            // where the free 10s summon is available.
            if (!bot->IsInCombat())
                return false;
            Unit* pet = AI_VALUE(Unit*, "pet target");
            if (pet && sServerFacade.IsAlive(pet))
                return false;
            return CastBuffSpellAction::isUseful();
        }
    };

    class CastSiphonLifeAction : public CastRangedDebuffSpellAction
    {
    public:
        CastSiphonLifeAction(PlayerbotAI* ai) : CastRangedDebuffSpellAction(ai, "siphon life") {}
    };

    class CastSiphonLifeOnAttackerAction : public CastRangedDebuffSpellOnAttackerAction
    {
    public:
        CastSiphonLifeOnAttackerAction(PlayerbotAI* ai) : CastRangedDebuffSpellOnAttackerAction(ai, "siphon life") {}
    };

    class CastHowlOfTerrorAction : public CastMeleeAoeSpellAction
    {
    public:
		CastHowlOfTerrorAction(PlayerbotAI* ai) : CastMeleeAoeSpellAction(ai, "howl of terror", 10.0f) {}
        bool isUseful() override
        {
            // Issue #383: same AoE-fear pack scatter as Psychic Scream. The
            // trigger already lives in the PvP-only cc kit; this action gate
            // covers the manual `.bot action` path. Shared rule in
            // ai/playerbot/AoeFearPolicy.h.
            Map* map = bot->GetMap();
            bool inInstance = map && (map->IsDungeon() || map->IsRaid());
            if (!ai::AoeFearAllowed(inInstance, ai->HasActivePlayerMaster(), ai->HasStrategy("aoe fear", BotState::BOT_STATE_COMBAT)))
                return false;
            return CastMeleeAoeSpellAction::isUseful();
        }
    };

	SPELL_ACTION(CastSearingPainAction, "searing pain");

    class UpdateWarlockPveStrategiesAction : public UpdateStrategyDependenciesAction
    {
    public:
        UpdateWarlockPveStrategiesAction(PlayerbotAI* ai) : UpdateStrategyDependenciesAction(ai, "update pve strats")
        {
            std::vector<std::string> strategiesRequired = { "demonology" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "demonology pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "demonology pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_DEAD, "demonology pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_REACTION, "demonology pve", strategiesRequired);

            strategiesRequired = { "demonology", "aoe" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "aoe demonology pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "aoe demonology pve", strategiesRequired);

            strategiesRequired = { "demonology", "cc" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cc demonology pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cc demonology pve", strategiesRequired);

            strategiesRequired = { "demonology", "pet" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "pet demonology pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "pet demonology pve", strategiesRequired);

            strategiesRequired = { "demonology", "buff" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "buff demonology pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "buff demonology pve", strategiesRequired);

            strategiesRequired = { "demonology", "boost" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "boost demonology pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "boost demonology pve", strategiesRequired);

            strategiesRequired = { "demonology", "curse" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "curse demonology pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "curse demonology pve", strategiesRequired);

            strategiesRequired = { "destruction" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "destruction pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "destruction pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_DEAD, "destruction pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_REACTION, "destruction pve", strategiesRequired);

            strategiesRequired = { "destruction", "aoe" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "aoe destruction pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "aoe destruction pve", strategiesRequired);

            strategiesRequired = { "destruction", "cc" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cc destruction pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cc destruction pve", strategiesRequired);

            strategiesRequired = { "destruction", "pet" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "pet destruction pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "pet destruction pve", strategiesRequired);

            strategiesRequired = { "destruction", "buff" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "buff destruction pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "buff destruction pve", strategiesRequired);

            strategiesRequired = { "destruction", "boost" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "boost destruction pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "boost destruction pve", strategiesRequired);

            strategiesRequired = { "destruction", "curse" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "curse destruction pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "curse destruction pve", strategiesRequired);

            strategiesRequired = { "affliction" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "affliction pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "affliction pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_DEAD, "affliction pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_REACTION, "affliction pve", strategiesRequired);

            strategiesRequired = { "affliction", "aoe" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "aoe affliction pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "aoe affliction pve", strategiesRequired);

            strategiesRequired = { "affliction", "cc" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cc affliction pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cc affliction pve", strategiesRequired);

            strategiesRequired = { "affliction", "pet" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "pet affliction pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "pet affliction pve", strategiesRequired);

            strategiesRequired = { "affliction", "buff" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "buff affliction pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "buff affliction pve", strategiesRequired);

            strategiesRequired = { "affliction", "boost" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "boost affliction pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "boost affliction pve", strategiesRequired);

            strategiesRequired = { "affliction", "curse" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "curse affliction pve", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "curse affliction pve", strategiesRequired);
        }
    };

    class UpdateWarlockPvpStrategiesAction : public UpdateStrategyDependenciesAction
    {
    public:
        UpdateWarlockPvpStrategiesAction(PlayerbotAI* ai) : UpdateStrategyDependenciesAction(ai, "update pvp strats")
        {
            std::vector<std::string> strategiesRequired = { "demonology" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "demonology pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "demonology pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_DEAD, "demonology pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_REACTION, "demonology pvp", strategiesRequired);

            strategiesRequired = { "demonology", "aoe" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "aoe demonology pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "aoe demonology pvp", strategiesRequired);

            strategiesRequired = { "demonology", "cc" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cc demonology pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cc demonology pvp", strategiesRequired);

            strategiesRequired = { "demonology", "pet" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "pet demonology pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "pet demonology pvp", strategiesRequired);

            strategiesRequired = { "demonology", "buff" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "buff demonology pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "buff demonology pvp", strategiesRequired);

            strategiesRequired = { "demonology", "boost" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "boost demonology pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "boost demonology pvp", strategiesRequired);

            strategiesRequired = { "demonology", "curse" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "curse demonology pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "curse demonology pvp", strategiesRequired);

            strategiesRequired = { "destruction" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "destruction pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "destruction pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_DEAD, "destruction pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_REACTION, "destruction pvp", strategiesRequired);

            strategiesRequired = { "destruction", "aoe" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "aoe destruction pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "aoe destruction pvp", strategiesRequired);

            strategiesRequired = { "destruction", "cc" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cc destruction pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cc destruction pvp", strategiesRequired);

            strategiesRequired = { "destruction", "pet" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "pet destruction pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "pet destruction pvp", strategiesRequired);

            strategiesRequired = { "destruction", "buff" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "buff destruction pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "buff destruction pvp", strategiesRequired);

            strategiesRequired = { "destruction", "boost" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "boost destruction pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "boost destruction pvp", strategiesRequired);

            strategiesRequired = { "destruction", "curse" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "curse destruction pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "curse destruction pvp", strategiesRequired);

            strategiesRequired = { "affliction" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "affliction pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "affliction pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_DEAD, "affliction pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_REACTION, "affliction pvp", strategiesRequired);

            strategiesRequired = { "affliction", "aoe" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "aoe affliction pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "aoe affliction pvp", strategiesRequired);

            strategiesRequired = { "affliction", "cc" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cc affliction pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cc affliction pvp", strategiesRequired);

            strategiesRequired = { "affliction", "pet" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "pet affliction pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "pet affliction pvp", strategiesRequired);

            strategiesRequired = { "affliction", "buff" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "buff affliction pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "buff affliction pvp", strategiesRequired);

            strategiesRequired = { "affliction", "boost" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "boost affliction pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "boost affliction pvp", strategiesRequired);

            strategiesRequired = { "affliction", "curse" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "curse affliction pvp", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "curse affliction pvp", strategiesRequired);
        }
    };

    class UpdateWarlockRaidStrategiesAction : public UpdateStrategyDependenciesAction
    {
    public:
        UpdateWarlockRaidStrategiesAction(PlayerbotAI* ai) : UpdateStrategyDependenciesAction(ai, "update raid strats")
        {
            std::vector<std::string> strategiesRequired = { "demonology" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "demonology raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "demonology raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_DEAD, "demonology raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_REACTION, "demonology raid", strategiesRequired);

            strategiesRequired = { "demonology", "aoe" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "aoe demonology raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "aoe demonology raid", strategiesRequired);

            strategiesRequired = { "demonology", "cc" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cc demonology raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cc demonology raid", strategiesRequired);

            strategiesRequired = { "demonology", "pet" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "pet demonology raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "pet demonology raid", strategiesRequired);

            strategiesRequired = { "demonology", "buff" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "buff demonology raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "buff demonology raid", strategiesRequired);

            strategiesRequired = { "demonology", "boost" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "boost demonology raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "boost demonology raid", strategiesRequired);

            strategiesRequired = { "demonology", "curse" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "curse demonology raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "curse demonology raid", strategiesRequired);

            strategiesRequired = { "destruction" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "destruction raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "destruction raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_DEAD, "destruction raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_REACTION, "destruction raid", strategiesRequired);

            strategiesRequired = { "destruction", "aoe" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "aoe destruction raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "aoe destruction raid", strategiesRequired);

            strategiesRequired = { "destruction", "cc" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cc destruction raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cc destruction raid", strategiesRequired);

            strategiesRequired = { "destruction", "pet" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "pet destruction raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "pet destruction raid", strategiesRequired);

            strategiesRequired = { "destruction", "buff" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "buff destruction raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "buff destruction raid", strategiesRequired);

            strategiesRequired = { "destruction", "boost" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "boost destruction raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "boost destruction raid", strategiesRequired);

            strategiesRequired = { "destruction", "curse" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "curse destruction raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "curse destruction raid", strategiesRequired);

            strategiesRequired = { "affliction" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "affliction raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "affliction raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_DEAD, "affliction raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_REACTION, "affliction raid", strategiesRequired);

            strategiesRequired = { "affliction", "aoe" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "aoe affliction raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "aoe affliction raid", strategiesRequired);

            strategiesRequired = { "affliction", "cc" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "cc affliction raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "cc affliction raid", strategiesRequired);

            strategiesRequired = { "affliction", "pet" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "pet affliction raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "pet affliction raid", strategiesRequired);

            strategiesRequired = { "affliction", "buff" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "buff affliction raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "buff affliction raid", strategiesRequired);

            strategiesRequired = { "affliction", "boost" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "boost affliction raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "boost affliction raid", strategiesRequired);

            strategiesRequired = { "affliction", "curse" };
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_COMBAT, "curse affliction raid", strategiesRequired);
            strategiesToUpdate.emplace_back(BotState::BOT_STATE_NON_COMBAT, "curse affliction raid", strategiesRequired);
        }
    };
}
