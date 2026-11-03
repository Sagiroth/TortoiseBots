"""Wiring pins for the crowd-control area guards (ZG walk 32).

The pure rules live in ai/playerbot/strategy/values/CcMarkHoldPolicy.h
(CcDotTargetAllowed, CcCancelSwingSplashBuff; behaviour in
tools/test_cc_mark_hold_policy.cpp) and the game-state reads in
ai/playerbot/strategy/values/TargetValue.cpp (CcGuard::CcInRunningArea,
CcGuard::PartyDotOn, CcGuard::IsCcMarkedUnit/IsCrowdControlledUnit). These pins
only prove the live code calls them:

 - a running Rain of Fire, Volley, Blizzard or Hurricane channel is cancelled when
   a unit under the party's crowd control stands inside its area: the spells'
   cast-start guard (BreaksCcInArea) ran before the sheep landed, and the channel
   kept ticking on it;
 - Sweeping Strikes is not started beside a protected enemy, and a running one is
   dropped ("remove sweeping strikes" on the "sweeping strikes near cc" trigger),
   the warrior's copy of the Blade Flurry rule;
 - the DoT rows ("attacker without aura") skip a CC mark or a crowd-controlled
   unit while AiPlayerbot.DungeonRunAutoCc is on, and keep the held-mark skip
   without it.

Run: python3 -m unittest tools/test_cc_area_guards_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STRATEGY = ROOT / "ai/playerbot/strategy"
VALUES = STRATEGY / "values"
WARLOCK_TRIGGERS = (STRATEGY / "warlock/WarlockTriggers.cpp").read_text()
HUNTER_TRIGGERS = (STRATEGY / "hunter/HunterTriggers.cpp").read_text()
MAGE_TRIGGERS = (STRATEGY / "mage/MageTriggers.cpp").read_text()
DRUID_TRIGGERS = (STRATEGY / "druid/DruidTriggers.cpp").read_text()
DRUID_TRIGGERS_H = (STRATEGY / "druid/DruidTriggers.h").read_text()
DRUID_CONTEXT = (STRATEGY / "druid/DruidAiObjectContext.cpp").read_text()
BALANCE = (STRATEGY / "druid/BalanceDruidStrategy.cpp").read_text()
WARRIOR_ACTIONS = (STRATEGY / "warrior/WarriorActions.h").read_text()
WARRIOR_TRIGGERS = (STRATEGY / "warrior/WarriorTriggers.cpp").read_text()
WARRIOR_TRIGGERS_H = (STRATEGY / "warrior/WarriorTriggers.h").read_text()
WARRIOR_CONTEXT = (STRATEGY / "warrior/WarriorAiObjectContext.cpp").read_text()
ARMS = (STRATEGY / "warrior/ArmsWarriorStrategy.cpp").read_text()
PERIODIC_AURA = (VALUES / "PeriodicDamageAura.h").read_text()
COMMANDER = (ROOT / "commands/Commander.cpp").read_text()
CC_TARGET_VALUE = (VALUES / "CcTargetValue.cpp").read_text()
TARGET_VALUE = (VALUES / "TargetValue.cpp").read_text()
TARGET_VALUE_H = (VALUES / "TargetValue.h").read_text()
DOT_TARGET = (VALUES / "AttackerWithoutAuraTargetValue.cpp").read_text()
POLICY = (VALUES / "CcMarkHoldPolicy.h").read_text()
AREA_SPELL_H = (STRATEGY / "actions/EnemyAreaSpell.h").read_text()
GENERIC_SPELL_ACTIONS = (STRATEGY / "actions/GenericSpellActions.cpp").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class ChannelCancelWiring(unittest.TestCase):
    """A running area channel is cancelled when our CC'd unit stands inside it."""

    def test_the_area_guard_reads_the_spell_data_and_the_nearby_targets(self):
        area = body(TARGET_VALUE, "bool CcGuard::CcInRunningArea(")
        self.assertIn("EnemyAreaOf(spell->m_spellInfo, atTarget, radius)", area)
        self.assertIn("spell->m_targets.getDestination(x, y, z)", area)
        # A crowd-controlled unit attacks nobody: the attackers list the other
        # guards read cannot see it, so this one reads the sight list.
        self.assertIn('GetValue<std::list<ObjectGuid>>("possible targets no los")', area)
        self.assertIn("PossibleAttackTargetsValue::HoldsBreakableCc(ai, unit, bot)", area)
        self.assertIn("CcInRunningArea(Spell const* spell)", TARGET_VALUE_H)
        # The shape helper is shared with the cast-start guard's own derivation.
        self.assertIn("inline bool EnemyAreaOf(SpellEntry const* spellInfo, bool& atTarget, float& radius)", AREA_SPELL_H)
        self.assertIn("EffectRadiusIndex[i]", AREA_SPELL_H)

    def test_every_area_channel_checks_it(self):
        for text, name, marker in (
                (WARLOCK_TRIGGERS, "RainOfFireChannelCheckTrigger", 'uint8 aoeCount = AI_VALUE(uint8, "aoe count");'),
                (HUNTER_TRIGGERS, "VolleyChannelCheckTrigger", 'return AI_VALUE(uint8, "aoe count") < 2;'),
                (MAGE_TRIGGERS, "BlizzardChannelCheckTrigger", 'return AI_VALUE(uint8, "attackers count") < 2;')):
            fn = body(text, "bool %s::IsActive()" % name)
            self.assertIn("CcGuard ccGuard(ai);", fn)
            self.assertIn("CcInRunningArea(spell)", fn)
            # The CC test is the fast veto: it runs before the density count.
            self.assertLess(fn.index("CcInRunningArea(spell)"), fn.index(marker))
        # Hurricane had no channel check at all: a new one, keyed on the spell
        # data (no rank list), on the cancel row.
        self.assertIn("class HurricaneChannelCheckTrigger", DRUID_TRIGGERS_H)
        hurricane = body(DRUID_TRIGGERS, "bool HurricaneChannelCheckTrigger::IsActive(")
        self.assertIn("GetCurrentSpell(CURRENT_CHANNELED_SPELL)", hurricane)
        self.assertIn("IsEnemyAreaSpell(spell->m_spellInfo)", hurricane)
        self.assertIn("CcInRunningArea(spell)", hurricane)
        self.assertIn('creators["hurricane channel check"]', DRUID_CONTEXT)
        self.assertIn('"hurricane channel check"', BALANCE)
        self.assertIn('new NextAction("cancel channel"', BALANCE)


class SweepingStrikesWiring(unittest.TestCase):
    """The warrior's swing copy gets the Blade Flurry treatment."""

    def test_start_and_cancel_share_the_rule(self):
        near = body(WARRIOR_ACTIONS, "inline bool SweepingStrikesNearCc(")
        self.assertIn("ProtectedEnemyNear(bot, radius, true, bot->GetVictim())", near)
        self.assertIn("ReadyCcMarkNear(bot, radius)", near)
        self.assertIn("CcCancelSwingSplashBuff(true, protectedNear", near)
        start = body(WARRIOR_ACTIONS, "class CastSweepingStrikesAction")
        self.assertIn("CastBuffSpellAction::isUseful()", start)
        self.assertIn("SweepingStrikesNearCc(ai, bot, CcSweepingStrikesRadius)", start)
        self.assertIn("constexpr float CcSweepingStrikesRadius = 8.0f;", POLICY)
        self.assertIn("inline bool CcCancelSwingSplashBuff(bool buffUp, bool protectedEnemyInReach, bool readyCcMarkInReach)",
                      POLICY)

    def test_the_running_buff_is_dropped(self):
        trigger = body(WARRIOR_TRIGGERS, "bool SweepingStrikesNearCcTrigger::IsActive(")
        # Aura first: only a warrior with the buff up pays for the scan.
        self.assertIn('ai->HasAura("sweeping strikes", bot)', trigger)
        self.assertIn("SweepingStrikesNearCc(ai, bot, CcSweepingStrikesRadius)", trigger)
        self.assertIn("class SweepingStrikesNearCcTrigger", WARRIOR_TRIGGERS_H)
        self.assertIn('creators["sweeping strikes near cc"]', WARRIOR_CONTEXT)
        self.assertIn('creators["remove sweeping strikes"]', WARRIOR_CONTEXT)
        self.assertIn('"remove sweeping strikes"', ARMS)
        self.assertIn("ACTION_INTERRUPT + 5", ARMS)


class DotTargetWiring(unittest.TestCase):
    """No DoT on a CC mark or a crowd-controlled unit (auto crowd control on)."""

    def test_the_dot_rows_call_the_policy(self):
        fn = body(DOT_TARGET, "Unit* AttackerWithoutAuraTargetValue::Calculate()")
        self.assertIn("sPlayerbotAIConfig.dungeonRunAutoCc", fn)
        self.assertIn("ccGuard.IsCcMarkedUnit(unit)", fn)
        self.assertIn("ccGuard.IsCrowdControlledUnit(unit)", fn)
        self.assertIn(
            "CcDotTargetAllowed(sPlayerbotAIConfig.dungeonRunAutoCc, ccGuard.IsHeldCcMark(unit), ccMarkOrControlled)",
            fn)
        # The two reads behind the policy are the guard's own mark and CC tests.
        marked = body(TARGET_VALUE, "bool CcGuard::IsCcMarkedUnit(")
        self.assertIn("IsCcMark(unit)", marked)
        controlled = body(TARGET_VALUE, "bool CcGuard::IsCrowdControlledUnit(")
        self.assertIn("IsCrowdControlled(ai, unit)", controlled)
        self.assertIn("bool CcDotTargetAllowed(bool runAutoCc, bool heldMark, bool ccMarkOrControlled)", POLICY)

    def test_the_dot_rows_guard_their_current_target(self):
        # The caster's own rotation at its current target (Corruption, Immolate, Serpent Sting, Flame Shock, Rupture
        # ...): once that target gets a CC mark, no periodic damage is applied or refreshed on it, so the mark is not
        # broken by the caster already on it.
        for name in ("bool CastMeleeDebuffSpellAction::isUseful()", "bool CastRangedDebuffSpellAction::isUseful()"):
            fn = body(GENERIC_SPELL_ACTIONS, name)
            self.assertIn("DotCastAtTarget(ai, GetTarget(), GetSpellID())", fn)
            self.assertIn("CastAuraSpellAction::isUseful()", fn)
        dot = body(GENERIC_SPELL_ACTIONS, "bool SpellAppliesPeriodicDamage(")
        self.assertIn("IsPeriodicDamageAura(info->EffectApplyAuraName[i])", dot)
        guard = body(GENERIC_SPELL_ACTIONS, "bool DotCastAtTarget(")
        self.assertIn("ccGuard.IsCcMarkedUnit(target)", guard)
        self.assertIn("ccGuard.IsCrowdControlledUnit(target)", guard)
        self.assertIn("ccGuard.IsHeldCcMark(target)", guard)
        self.assertIn("CcDotCastAllowed(", guard)
        self.assertIn(
            "bool CcDotCastAllowed(bool periodicDamage, bool runAutoCc, bool heldMark, bool ccMarkOrControlled)",
            POLICY)


class PeriodicDamageAuraWiring(unittest.TestCase):
    """The core counts three aura types as a DoT's periodic damage; every guard reads all three.

    Behaviour (an assigned Polymorph target skipped for each type from a party caster) needs live Unit and Aura
    objects, so it is pinned here by source and proven in game, not by this file.
    """

    def test_the_helper_names_all_three_periodic_damage_auras(self):
        self.assertIn("SPELL_AURA_PERIODIC_DAMAGE,", PERIODIC_AURA)
        self.assertIn("SPELL_AURA_PERIODIC_LEECH,", PERIODIC_AURA)
        self.assertIn("SPELL_AURA_PERIODIC_DAMAGE_PERCENT,", PERIODIC_AURA)
        self.assertIn("inline bool IsPeriodicDamageAura(uint32 auraType)", PERIODIC_AURA)

    def test_the_party_dot_guards_walk_all_three_types(self):
        for text, start, qualifier in (
            (TARGET_VALUE, "bool CcGuard::PartyDotOn(", ""),
            (COMMANDER, "bool PartyDotOn(Unit* unit, Player* lead)", "ai::"),
        ):
            fn = body(text, start)
            self.assertIn(f"{qualifier}kPeriodicDamageAuraTypes", fn)
            self.assertNotIn("GetAurasByType(SPELL_AURA_PERIODIC_DAMAGE)", fn)

    def test_the_assigned_cc_keeps_the_banish_and_fear_exemption(self):
        assigned = body(CC_TARGET_VALUE, "if (assignedTarget)", "\n        }\n")
        self.assertIn('spell != "fear" && spell != "banish"', assigned)
        self.assertIn("ccGuard.PartyDotOn(creature)", assigned)

    def test_the_spell_check_uses_the_same_helper(self):
        dot = body(GENERIC_SPELL_ACTIONS, "bool SpellAppliesPeriodicDamage(")
        self.assertIn("IsPeriodicDamageAura(info->EffectApplyAuraName[i])", dot)
        self.assertNotIn("== SPELL_AURA_PERIODIC_DAMAGE", dot)


if __name__ == "__main__":
    unittest.main()
