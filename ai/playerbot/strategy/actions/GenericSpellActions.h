#pragma once
#include "playerbot/strategy/Action.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/PlayerbotAI.h"

namespace ai
{
    // Issue #T7: several buffers in one party picked the same target in the same
    // tick - a freshly hired member has no auras yet, so every buffer decided
    // "it lacks my buff" before the first cast (1.5 s) had landed the aura, and
    // all of them cast the same spell. This tiny registry is shared by every bot
    // in the process: the caster claims the target (or the whole group for the
    // area buffs) right before casting and the other casters stand down while a
    // claim is live. Claims are time-bounded only - a caster that dies, leaves
    // or logs out is harmless, its entry simply expires.
    class BuffClaimRegistry
    {
    public:
        // Claim a scope (a target's guid, or GroupScope()) for spell.
        static void Claim(ObjectGuid const& caster, ObjectGuid const& scope, std::string const& spell);
        // True when a caster other than `caster` holds a live claim on scope.
        static bool IsClaimedByOther(ObjectGuid const& caster, ObjectGuid const& scope, std::string const& spell);
        // Stable group scope for a bot: its group id when grouped (one id for the
        // whole party, so two separate parties never interfere), its own guid
        // when solo.
        static ObjectGuid GroupScope(Player* bot);
        // True when another bot holds a live claim on target, or on the caster's
        // group for this spell (the group scope covers the area buffs).
        static bool IsTargetClaimedByOther(Player* caster, Unit* target, std::string const& spell);
    };

    class CastSpellAction : public Action
    {
    public:
        CastSpellAction(PlayerbotAI* ai, std::string spell);
        virtual ActionThreatType getThreatType() override { return ActionThreatType::ACTION_THREAT_SINGLE; }
        virtual bool Execute(Event& event) override;
        virtual bool isPossible() override;
		virtual bool isUseful() override;

        // Used when this action is executed as a reaction
        bool ShouldReactionInterruptCast() const override { return true; }

        bool HasReachAction() { return !GetReachActionName().empty(); }

    protected:
        const uint32& GetSpellID() const { return spellId; }
        const std::string& GetSpellName() const { return spellName; }
        void SetSpellName(const std::string& name, std::string spellIDContextName = "spell id", bool force = false);

        Value<Unit*>* GetTargetValue() override;
        Unit* GetTarget() override;
        virtual std::string GetTargetName() override { return "current target"; }
        virtual std::string GetTargetQualifier() { return ""; }
        virtual std::string GetReachActionName() { return "reach spell"; }

        virtual NextAction** getPrerequisites() override;

    protected:
		float range;

    private:
        std::string spellName;
        uint32 spellId;
    };

    class CastPetSpellAction : public CastSpellAction
    {
    public:
        CastPetSpellAction(PlayerbotAI* ai, std::string spell) : CastSpellAction(ai, spell) {}

        virtual bool isPossible() override;

    protected:
        virtual std::string GetTargetName() override { return "current target"; }
        std::string GetReachActionName() override { return ""; }
    };

	//---------------------------------------------------------------------------------------------------------------------
	class CastAuraSpellAction : public CastSpellAction
	{
	public:
		CastAuraSpellAction(PlayerbotAI* ai, std::string spell, bool isOwner = false) : CastSpellAction(ai, spell), isOwner(isOwner) {}
		virtual bool isUseful() override;

    protected:
        virtual std::string GetReachActionName() override { return "reach spell"; }

    protected:
        bool isOwner;
	};

    //---------------------------------------------------------------------------------------------------------------------
    class CastMeleeSpellAction : public CastSpellAction
    {
    public:
        CastMeleeSpellAction(PlayerbotAI* ai, std::string spell) : CastSpellAction(ai, spell)
        {
            range = ATTACK_DISTANCE;
        }

    protected:
        virtual std::string GetReachActionName() override { return "reach melee"; }
    };

    //---------------------------------------------------------------------------------------------------------------------
    class CastMeleeAoeSpellAction : public CastSpellAction
    {
    public:
        CastMeleeAoeSpellAction(PlayerbotAI* ai, std::string spell, float radius) : CastSpellAction(ai, spell), radius(radius)
        {
            range = ATTACK_DISTANCE;
        }

        virtual bool isUseful() override;

    protected:
        virtual std::string GetReachActionName() override { return ""; }

    protected:
        float radius;
    };

    //---------------------------------------------------------------------------------------------------------------------
    class CastMeleeDebuffSpellAction : public CastAuraSpellAction
    {
    public:
        CastMeleeDebuffSpellAction(PlayerbotAI* ai, std::string spell, bool isOwner = true) : CastAuraSpellAction(ai, spell, isOwner)
        {
            range = ATTACK_DISTANCE;
        }

    protected:
        virtual std::string GetReachActionName() override { return "reach melee"; }
    };

    class CastRangedDebuffSpellAction : public CastAuraSpellAction
    {
    public:
        CastRangedDebuffSpellAction(PlayerbotAI* ai, std::string spell, bool isOwner = true) : CastAuraSpellAction(ai, spell, isOwner) {}

    protected:
        virtual std::string GetReachActionName() override { return "reach spell"; }
    };

    class CastMeleeDebuffSpellOnAttackerAction : public CastAuraSpellAction
    {
    public:
        CastMeleeDebuffSpellOnAttackerAction(PlayerbotAI* ai, std::string spell, bool isOwner = true) : CastAuraSpellAction(ai, spell, isOwner)
        {
            range = ATTACK_DISTANCE;
        }

    protected:
        std::string GetReachActionName() override { return "reach melee"; }
        std::string GetTargetName() override { return "attacker without aura"; }
        std::string GetTargetQualifier() override { return GetSpellName(); }
        virtual std::string getName() override { return GetSpellName() + " on attacker"; }
        virtual ActionThreatType getThreatType() override { return ActionThreatType::ACTION_THREAT_AOE; }
    };

    class CastRangedDebuffSpellOnAttackerAction : public CastAuraSpellAction
    {
    public:
        CastRangedDebuffSpellOnAttackerAction(PlayerbotAI* ai, std::string spell, bool isOwner = true) : CastAuraSpellAction(ai, spell, isOwner) {}

    protected:
        virtual std::string GetReachActionName() override { return "reach spell"; }
        virtual std::string GetTargetName() override { return "attacker without aura"; }
        virtual std::string GetTargetQualifier() override { return GetSpellName(); }
        virtual std::string getName() override { return GetSpellName() + " on attacker"; }
        virtual ActionThreatType getThreatType() override { return ActionThreatType::ACTION_THREAT_AOE; }
    };

	class CastBuffSpellAction : public CastAuraSpellAction
	{
	public:
		CastBuffSpellAction(PlayerbotAI* ai, std::string spell) : CastAuraSpellAction(ai, spell) { }
        virtual std::string GetTargetName() override { return "self target"; }
        virtual bool isUseful() override;
        virtual bool Execute(Event& event) override;

    protected:
        // Issue #378: retry window for one attempt on one target, out of combat.
        // The party group buffs widen it (see GreaterBuffOnPartyAction): a single
        // cast covers the whole (sub)group and spends a reagent, so a target that
        // cannot receive the area buff must not burn another reagent every 3 s.
        virtual uint32 GetBuffRetryCooldown() const;

        // Issue #T7: the scopes this cast claims before it is attempted, so other
        // bots stop duplicating it. Default: the resolved target. The greater
        // (area) buffs override it to claim the caster's whole group, under both
        // the greater and the lower single-target name.
        virtual void ClaimBuffCast(Unit* target);

        // Issue #378: out of combat an upkeep buff is not worth its mana while the
        // bot is below its floor. Combat casts (seals, totems, shields, charge
        // re-applies) are never gated. Percentage-cost spells and shapeshift forms
        // are handled inside; see the .cpp for the exemptions.
        bool HasManaForBuff();

        // Issue #359: an upkeep buff has no retry cooldown of its own. Its trigger
        // re-evaluates every tick (BuffTrigger interval < 2) and the engine's
        // failure backoff deliberately exempts bots with a real player master, so a
        // buff whose aura is still missing after the attempt (drink in progress, out
        // of range or LOS, not enough mana, master moving away) was re-attempted on
        // every AI tick - the hired priest "buffs itself, drinks and repeats" loop.
        // One attempt per target per GetBuffRetryCooldown() seconds, out of combat
        // only; the aura gate in CastAuraSpellAction::isUseful still decides whether
        // the buff is needed at all.
        ObjectGuid lastAttemptTarget;
        time_t lastAttemptTime = 0;

        // bot_events.csv telemetry ("SelfBuff"), rate limited: one row per applied
        // self buff, so a recast loop stays countable without flooding the log.
        time_t lastSelfBuffEventTime = 0;
	};

    class CastSpellTargetAction : public CastSpellAction
    {
    public:
        CastSpellTargetAction(PlayerbotAI* ai, std::string spell, std::string targetsValue, bool aliveCheck = false, bool auraCheck = false) : CastSpellAction(ai, spell), targetsValue(targetsValue), aliveCheck(aliveCheck), auraCheck(auraCheck) {}
        virtual std::string GetTargetName() override { return "self target"; }
        virtual bool IsTargetValid(Unit* target);
        Unit* GetTarget() override;

    private:
        std::string targetsValue;
        bool aliveCheck;
        bool auraCheck;
    };

    class CastItemTargetAction : public CastSpellTargetAction
    {
    public:
        CastItemTargetAction(PlayerbotAI* ai, std::string targetsValue, bool aliveCheck = false, bool auraCheck = false) : CastSpellTargetAction(ai, "item target", targetsValue, aliveCheck, false), itemAuraCheck(auraCheck) {}
        virtual bool IsTargetValid(Unit* target) override;
        virtual uint32 GetItemId() = 0;

    protected:
        virtual bool isUseful() override;
        virtual bool isPossible() override;
        virtual bool Execute(Event& event) override;

    private:
        bool HasSpellCooldown(uint32 itemId);

    private:
        bool itemAuraCheck;
    };

	class CastEnchantItemAction : public CastSpellAction
	{
	public:
	    CastEnchantItemAction(PlayerbotAI* ai, std::string spell) : CastSpellAction(ai, spell) { }
        virtual std::string GetTargetName() override { return "self target"; }
        virtual bool isPossible() override;
	};

    //---------------------------------------------------------------------------------------------------------------------

    class CastHealingSpellAction : public CastAuraSpellAction
    {
    public:
        CastHealingSpellAction(PlayerbotAI* ai, std::string spell, uint8 estAmount = 15.0f) : CastAuraSpellAction(ai, spell, true), estAmount(estAmount) {}

    protected:
        virtual ActionThreatType getThreatType() override { return ActionThreatType::ACTION_THREAT_AOE; }
        virtual std::string GetTargetName() override { return "self target"; }
        virtual std::string GetReachActionName() override { return "reach party member to heal"; }

    protected:
        uint8 estAmount;
    };

    class CastAoeHealSpellAction : public CastHealingSpellAction
    {
    public:
	CastAoeHealSpellAction(PlayerbotAI* ai, std::string spell, uint8 estAmount = 15.0f) : CastHealingSpellAction(ai, spell, estAmount) {}
		virtual std::string GetTargetName() override { return "party member to heal"; }
        virtual bool isUseful() override;
    };

	class CastCureSpellAction : public CastSpellAction
	{
	public:
		CastCureSpellAction(PlayerbotAI* ai, std::string spell) : CastSpellAction(ai, spell) {}
		virtual std::string GetTargetName() override { return "self target"; }
	};

	class PartyMemberActionNameSupport
    {
	public:
		PartyMemberActionNameSupport(std::string spell) : name(spell + " on party") {}
		std::string getName() { return name; }

	private:
		std::string name;
	};

    class HealPartyMemberAction : public CastHealingSpellAction, public PartyMemberActionNameSupport
    {
    public:
        HealPartyMemberAction(PlayerbotAI* ai, std::string spell, uint8 estAmount = 15.0f) : CastHealingSpellAction(ai, spell, estAmount), PartyMemberActionNameSupport(spell) {}
        virtual std::string getName() override { return PartyMemberActionNameSupport::getName(); }
		virtual std::string GetTargetName() override { return "party member to heal"; }
    };

    class HealHotPartyMemberAction : public HealPartyMemberAction
    {
    public:
        HealHotPartyMemberAction(PlayerbotAI* ai, std::string spell) : HealPartyMemberAction(ai, spell) {}
        virtual bool isUseful() override;
    };

	class ResurrectPartyMemberAction : public CastSpellAction
	{
	public:
		ResurrectPartyMemberAction(PlayerbotAI* ai, std::string spell) : CastSpellAction(ai, spell) {}

    protected:
        virtual std::string GetTargetName() override { return "party member to resurrect"; }
        virtual std::string GetReachActionName() override { return "reach party member to resurrect"; }
	};
    //---------------------------------------------------------------------------------------------------------------------

    class CurePartyMemberAction : public CastSpellAction, public PartyMemberActionNameSupport
    {
    public:
        CurePartyMemberAction(PlayerbotAI* ai, std::string spell, uint32 dispelType) : CastSpellAction(ai, spell), PartyMemberActionNameSupport(spell), dispelType(dispelType) {}

    protected:
        virtual std::string GetReachActionName() override { return "reach party member to heal"; }
        virtual std::string getName() override { return PartyMemberActionNameSupport::getName(); }
        virtual std::string GetTargetName() override { return "party member to dispel"; }
        virtual std::string GetTargetQualifier() override { return std::to_string(dispelType); }

    protected:
        uint32 dispelType;
    };

    //---------------------------------------------------------------------------------------------------------------------

    class BuffOnPartyAction : public CastBuffSpellAction, public PartyMemberActionNameSupport
    {
    public:
        BuffOnPartyAction(PlayerbotAI* ai, std::string spell, bool ignoreTanks = false) : CastBuffSpellAction(ai, spell), PartyMemberActionNameSupport(spell), ignoreTanks(ignoreTanks) {}
        // Issue #468 (donor UpgradeToGroupIfAppropriate): when this single has
        // a group variant and the group cast is ready (trained, stocked, and
        // enough members lack both auras), stand the single down so the group
        // version fires instead of N single casts. Runs beneath the aura,
        // claim, retry and mana gates in CastBuffSpellAction::isUseful.
        virtual bool isUseful() override;

    protected:
        virtual std::string getName() override { return PartyMemberActionNameSupport::getName(); }
        virtual std::string GetTargetName() override { return "party member without aura"; }
        virtual std::string GetTargetQualifier() override { return GetSpellName() + "-" + (ignoreTanks ? "1" : "0"); }
        // Counts live same-map group members lacking both this buff (or its
        // window-expiring aura) and its group variant. Shared with the quorum
        // gate below; per tick, bounded by group size, no DB or world scan.
        uint32 CountGroupMembersMissingBoth(std::string const& groupName) const;

    protected:
        bool ignoreTanks;
    };

    //---------------------------------------------------------------------------------------------------------------------

    class GreaterBuffOnPartyAction : public CastBuffSpellAction, public PartyMemberActionNameSupport
    {
    public:
        GreaterBuffOnPartyAction(PlayerbotAI* ai, std::string spell, bool ignoreTanks = false, std::string lowerSpell = "") : CastBuffSpellAction(ai, spell), PartyMemberActionNameSupport(spell), ignoreTanks(ignoreTanks), lowerSpell(lowerSpell) {}

    protected:
        virtual std::string getName() override { return PartyMemberActionNameSupport::getName(); }
        virtual std::string GetTargetName() override { return "party member without aura"; }
        // Greater buffs get the long retry window: the cast is area-wide and costs
        // a reagent, so a member the area never covers is only re-attempted once a
        // minute. While it is cooling down isUseful() is false and the engine runs
        // the lower-priority single-target buff for that member instead.
        virtual uint32 GetBuffRetryCooldown() const override;
        // Issue #T7: a greater buff covers the whole (sub)group from one cast, so
        // the claim is on the group (under the greater and the lower spell name)
        // and every other bot stands down for both casts.
        virtual void ClaimBuffCast(Unit* target) override;
        // Must match GreaterBuffOnPartyTrigger::GetTargetValue(): the member has
        // to lack the lower single-target buff as well (issue #378).
        virtual std::string GetTargetQualifier() override { return GetSpellName() + (lowerSpell.empty() ? "" : "," + lowerSpell) + "-" + (ignoreTanks ? "1" : "0"); }

    private:
        bool ignoreTanks;
        std::string lowerSpell;
    };

    //---------------------------------------------------------------------------------------------------------------------

    class PartyTankActionNameSupport
    {
    public:
        PartyTankActionNameSupport(std::string spell) : name(spell + " on tank") {}
        std::string getName() { return name; }

    private:
        std::string name;
    };

    class BuffOnTankAction : public CastBuffSpellAction, public PartyMemberActionNameSupport
    {
    public:
        BuffOnTankAction(PlayerbotAI* ai, std::string spell) : CastBuffSpellAction(ai, spell), PartyMemberActionNameSupport(spell) {}

    protected:
        virtual std::string getName() override { return PartyMemberActionNameSupport::getName(); }
        virtual std::string GetTargetName() override { return "party tank without aura"; }
        virtual std::string GetTargetQualifier() override { return GetSpellName(); }
    };

    class CastShootAction : public CastSpellAction
    {
    public:
        CastShootAction(PlayerbotAI* ai) : CastSpellAction(ai, "shoot"), rangedWeapon(nullptr), weaponDelay(0), needsAmmo(false) {}
        ActionThreatType getThreatType() override { return ActionThreatType::ACTION_THREAT_NONE; }
        bool Execute(Event& event) override;
        bool isPossible() override;

    protected:
        virtual std::string GetReachActionName() override { return "reach spell"; }

    private:
        void UpdateWeaponInfo();

    private:
        const Item* rangedWeapon;
        uint32 weaponDelay;
        bool needsAmmo;
    };

    class RemoveBuffAction : public Action
    {
    public:
        RemoveBuffAction(PlayerbotAI* ai, std::string spell) : Action(ai, "remove aura"), name(spell) {}
        virtual std::string getName() override { return "remove " + name; }
        virtual bool isUseful() override;
        virtual bool Execute(Event& event) override;
        virtual bool isUsefulWhenStunned() override { return true; }
    private:
        std::string name;
    };

    // racials

    // heal
    HEAL_ACTION(CastCannibalizeAction, "cannibalize");

    // buff

    BUFF_ACTION(CastShadowmeldAction, "shadowmeld");
    BUFF_ACTION(CastBerserkingAction, "berserking");
    BUFF_ACTION(CastBloodFuryAction, "blood fury");
    BUFF_ACTION(CastStoneformAction, "stoneform");
    BUFF_ACTION(CastPerceptionAction, "perception");

    // spells


    class CastWarStompAction : public CastSpellAction
    {
    public:
        CastWarStompAction(PlayerbotAI* ai) : CastSpellAction(ai, "war stomp") {}
    };
    class CastManaTapAction : public CastSpellAction
    {
    public:
        CastManaTapAction(PlayerbotAI* ai) : CastSpellAction(ai, "mana tap") {}
    };

    class CastArcaneTorrentAction : public CastSpellAction
    {
    public:
        CastArcaneTorrentAction(PlayerbotAI* ai) : CastSpellAction(ai, "arcane torrent") {}
    };

    //cc breakers

    BUFF_ACTION(CastWillOfTheForsakenAction, "will of the forsaken");
    BUFF_ACTION_U(CastEscapeArtistAction, "escape artist", !ai->HasAura("stealth", AI_VALUE(Unit*, "self target")));


    class CastSpellOnEnemyHealerAction : public CastSpellAction
    {
    public:
        CastSpellOnEnemyHealerAction(PlayerbotAI* ai, std::string spell) : CastSpellAction(ai, spell) {}

    protected:
        virtual std::string GetReachActionName() override { return "reach spell"; }
        virtual std::string GetTargetName() override { return "enemy healer target"; }
        virtual std::string GetTargetQualifier() override { return GetSpellName(); }
        virtual std::string getName() override { return GetSpellName() + " on enemy healer"; }
    };

    class CastSnareSpellAction : public CastRangedDebuffSpellAction
    {
    public:
        CastSnareSpellAction(PlayerbotAI* ai, std::string spell) : CastRangedDebuffSpellAction(ai, spell) {}

    protected:
        virtual std::string GetReachActionName() override { return "reach spell"; }
        virtual std::string GetTargetName() override { return "snare target"; }
        virtual std::string GetTargetQualifier() override { return GetSpellName(); }
        virtual std::string getName() override { return GetSpellName() + " on snare target"; }
        virtual ActionThreatType getThreatType() override { return ActionThreatType::ACTION_THREAT_NONE; }
    };

    class CastCrowdControlSpellAction : public CastRangedDebuffSpellAction
    {
    public:
        CastCrowdControlSpellAction(PlayerbotAI* ai, std::string spell) : CastRangedDebuffSpellAction(ai, spell) {}
        bool IsCrowdControlAction() const override { return true; }
        std::string GetCrowdControlSpellName() const override { return GetSpellName(); }

    private:
        virtual std::string GetReachActionName() override { return "reach spell"; }
        virtual std::string GetTargetName() override { return "cc target"; }
        virtual std::string GetTargetQualifier() override { return GetSpellName(); }
        virtual ActionThreatType getThreatType() override { return ActionThreatType::ACTION_THREAT_NONE; }
    };

    class CastProtectSpellAction : public CastSpellAction
    {
    public:
        CastProtectSpellAction(PlayerbotAI* ai, std::string spell) : CastSpellAction(ai, spell) {}
        virtual bool isUseful() override { return CastSpellAction::isUseful() && !ai->HasAura(GetSpellName(), GetTarget()); }

    protected:
        virtual std::string GetReachActionName() override { return "reach spell"; }
        virtual std::string GetTargetName() override { return "party member to protect"; }
        virtual ActionThreatType getThreatType() override { return ActionThreatType::ACTION_THREAT_NONE; }
    };

    class InterruptCurrentSpellAction : public Action
    {
    public:
        InterruptCurrentSpellAction(PlayerbotAI* ai) : Action(ai, "interrupt current spell") {}
        virtual bool isUseful() override;
        virtual bool Execute(Event& event) override;
    };

}
