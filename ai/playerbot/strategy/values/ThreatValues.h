#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"

namespace ai
{
    class MyThreatValue : public LogCalculatedValue<float>, public Qualified
    {
    public:
        MyThreatValue(PlayerbotAI* ai, std::string name = "my threat") : LogCalculatedValue(ai, name), Qualified() { minChangeInterval = 1; lastTarget = ObjectGuid(); }
        virtual bool EqualToLast(float value) override { return value == lastValue; }
        virtual float Calculate() override;

    public:
        ObjectGuid lastTarget;
    };

    class TankThreatValue : public FloatCalculatedValue, public Qualified
    {
    public:
        TankThreatValue(PlayerbotAI* ai, std::string name = "tank threat") : FloatCalculatedValue(ai, name), Qualified() {}
        virtual float Calculate() override;
    };

    class ThreatValue : public Uint8CalculatedValue, public Qualified
	{
	public:
        ThreatValue(PlayerbotAI* ai, std::string name = "threat") : Uint8CalculatedValue(ai, name), Qualified() {}
	virtual uint8 Calculate() override;
        static float GetThreat(Player* player, Unit* target);
        static float GetTankThreat(PlayerbotAI* ai, Unit* target);

    protected:
	uint8 Calculate(Unit* target);
    };

    // Whether the core makes this target immune to taunts: Creature::HasImmunityFlag(CREATURE_IMMUNITY_TAUNT) reads
    // creature_template.immunity_flags 0x02, which Creature.cpp applies to SPELL_EFFECT_ATTACK_ME and
    // SPELL_AURA_MOD_TAUNT (Hakkar 14834: no taunt has ever landed on him).
    bool TargetTauntImmune(Unit* target);

    // Whether the target's boss tactic data asks for a threat successor (ai_boss_tactic kind `threat_successor`):
    // damage dealers are then measured against the assigned off-tanks' highest threat, so the second tank stays second
    // for a threat reset. A taunt-immune target without the row keeps the highest tank.
    bool ThreatSuccessorTarget(Unit* target);

    // Whether the target's `threat_successor` row carries the mode word `strict` (BossTacticTable::HasStrictThreatSuccessor):
    // the row's opt-in to AiPlayerbot.DungeonRunThreatSuccessorStrict. Callers gate it with StrictSuccessorTarget.
    bool StrictThreatSuccessorRow(Unit* target);

    struct RaidTankRoles;

    // The threat cap's own tank list on `target` (ThreatValue::GetTankThreat's successor pick): the raid's tank order
    // `roles`, a dead, unsafe or creature-charmed tank and `skip` (the reading bot; null: none) left out, the Hex and
    // Delusions marks and ThreatCapCursedOnBoss applied. `who` (when given) gets each listed tank, in list order. The
    // nearest take's cap guard (BossTacticsActions SeekTankLeaveCollapsesCap) reads the same list, so the two cannot
    // drift.
    std::vector<ThreatCapTank> ThreatCapTanksOf(PlayerbotAI* ai, Unit* target, const RaidTankRoles& roles, Player* skip,
                                                std::vector<Player*>* who = nullptr);
}
