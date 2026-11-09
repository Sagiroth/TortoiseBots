"""Wiring pins for the taunt-immune boss victim escape (AiPlayerbot.DungeonRunImmuneBossVictimEscape).

When a boss the game makes immune to taunts turns on a member that is not a tank (Hakkar after Cause Insanity), the
victim escapes with its own ability (Feign Death, Vanish, Ice Block, Divine Shield), else a paladin within 30 yd casts
Blessing of Protection on it. The rules are pure in ai/playerbot/VictimEscapePolicy.h and tested in
tools/test_victim_escape_policy.cpp; these pins prove the live code calls them, that every bot carries the triggers on
both engines, that the key defaults off, and the row format.

Run: python3 -m unittest tools/test_victim_escape_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


def read(rel):
    return (ROOT / rel).read_text()


ACTIONS = read("ai/playerbot/strategy/actions/VictimEscapeActions.cpp")
ACTIONS_H = read("ai/playerbot/strategy/actions/VictimEscapeActions.h")
POLICY = read("ai/playerbot/VictimEscapePolicy.h")
POLICY_TEST = read("tools/test_victim_escape_policy.cpp")
ACTION_CONTEXT = read("ai/playerbot/strategy/actions/ActionContext.h")
TRIGGER_CONTEXT = read("ai/playerbot/strategy/triggers/TriggerContext.h")
DUNGEON_STRATEGY = read("ai/playerbot/strategy/generic/DungeonStrategy.cpp")
COMBAT_STRATEGY = read("ai/playerbot/strategy/generic/CombatStrategy.cpp")
AI_FACTORY = read("ai/playerbot/AiFactory.cpp")
CONFIG_H = read("ai/playerbot/PlayerbotAIConfig.h")
CONFIG_CPP = read("ai/playerbot/PlayerbotAIConfig.cpp")
CONF = read("ai/playerbot/aiplayerbot.conf.dist.in")
SETTINGS = read("docs/server/settings/dungeons-raids.md")
BOSS_DOC = read("docs/play/boss-tactics.md")
REFUSAL_DOC = read("docs/play/refusal-rows.md")
CHANGELOG = read("CHANGELOG.md")

KEY = "AiPlayerbot.DungeonRunImmuneBossVictimEscape"


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class VictimEscapeWiring(unittest.TestCase):
    def test_key_defaults_off(self):
        self.assertIn("bool dungeonRunImmuneBossVictimEscape = false;", CONFIG_H)
        self.assertIn('dungeonRunImmuneBossVictimEscape = config.GetBoolDefault("' + KEY + '", false);', CONFIG_CPP)
        self.assertRegex(CONF, r"(?m)^" + re.escape(KEY) + r" = 0$")
        for text in (CONF, SETTINGS):
            self.assertIn("recommended on", text[text.index(KEY) - 2000:text.index(KEY) + 2000].replace("Recommended", "recommended"))
        self.assertIn("### `" + KEY + "`", SETTINGS)
        self.assertIn(KEY, BOSS_DOC)
        self.assertIn("DungeonRunImmuneBossVictimEscape", CHANGELOG)

    def test_triggers_and_actions_registered(self):
        self.assertIn('#include "playerbot/strategy/actions/VictimEscapeActions.h"', TRIGGER_CONTEXT)
        self.assertIn('#include "playerbot/strategy/actions/VictimEscapeActions.h"', ACTION_CONTEXT)
        self.assertIn('creators["immune boss victim"] = [](PlayerbotAI* ai) { return new ImmuneBossVictimTrigger(ai); };',
                      TRIGGER_CONTEXT)
        self.assertIn('creators["immune boss victim protect"] = [](PlayerbotAI* ai) { return new ImmuneBossVictimProtectTrigger(ai); };',
                      TRIGGER_CONTEXT)
        self.assertIn('creators["immune boss victim escape"] = [](PlayerbotAI* ai) { return new ImmuneBossVictimEscapeAction(ai); };',
                      ACTION_CONTEXT)
        self.assertIn('creators["bop immune boss victim"] = [](PlayerbotAI* ai) { return new ImmuneBossVictimProtectAction(ai); };',
                      ACTION_CONTEXT)

    def test_every_bot_carries_both_nodes_on_both_engines(self):
        own = '"immune boss victim",\n        NextAction::array(0, new NextAction("immune boss victim escape", ACTION_EMERGENCY + 2), NULL)));'
        bop = '"immune boss victim protect",\n        NextAction::array(0, new NextAction("bop immune boss victim", ACTION_EMERGENCY + 2), NULL)));'
        combat = body(DUNGEON_STRATEGY, "void DungeonStrategy::InitCombatTriggers(")
        reaction = body(COMBAT_STRATEGY, "void AvoidAoeStrategy::InitReactionTriggers(")
        for nodes in (combat, reaction):
            self.assertIn(own, nodes)
            self.assertIn(bop, nodes)
        # The always-on carriers: "dungeon" on the combat engine, "avoid aoe" on the reaction engine.
        self.assertIn('combatEngine->addStrategies("dungeon"', AI_FACTORY)
        self.assertRegex(AI_FACTORY, r'reactionEngine->addStrategies\([^;]*"avoid aoe"')
        # The reaction copy breaks the bot's own cast, and the self escape runs while stunned (Ice Block, Divine Shield).
        self.assertEqual(ACTIONS_H.count("bool ShouldReactionInterruptCast() const override { return true; }"), 2)
        self.assertIn("bool isUsefulWhenStunned() override { return true; }", ACTIONS_H)

    def test_triggers_gate_on_the_key_first(self):
        own = body(ACTIONS, "bool ImmuneBossVictimTrigger::IsActive()")
        bop = body(ACTIONS, "bool ImmuneBossVictimProtectTrigger::IsActive()")
        self.assertTrue(own.split("{", 1)[1].lstrip().startswith("if (!sPlayerbotAIConfig.dungeonRunImmuneBossVictimEscape)"))
        self.assertTrue(bop.split("{", 1)[1].lstrip().startswith("if (!sPlayerbotAIConfig.dungeonRunImmuneBossVictimEscape"))
        self.assertIn("PlayerbotAI::IsTank(bot)", own)
        self.assertIn("PlanOwnEscape(", own)
        self.assertIn("PlanProtection(", bop)
        for name in ("ImmuneBossVictimEscapeAction::Execute", "ImmuneBossVictimProtectAction::Execute"):
            self.assertIn("sPlayerbotAIConfig.dungeonRunImmuneBossVictimEscape", body(ACTIONS, "bool " + name + "("))

    def test_the_gate_is_taunt_immunity_on_a_boss_no_ids(self):
        boss = body(ACTIONS, "Unit* ai::ImmuneBossOnVictim(")
        self.assertIn("attacker->GetVictim() != member", boss)
        self.assertIn("IsBossCreature(attacker) && BossInScope(attacker)", boss)
        scope = body(ACTIONS, "bool BossInScope(")
        # Opt in by row data: taunt-immune AND a threat_successor row (Hakkar, Jin'do), never a boss id.
        self.assertIn("VictimEscapeBossInScope(TargetTauntImmune(boss), ThreatSuccessorTarget(boss))", scope)
        is_boss = body(ACTIONS, "bool IsBossCreature(")
        self.assertIn("creature->IsWorldBoss()", is_boss)
        self.assertIn("GetDungeonPlans().IsBossEntry(", is_boss)
        # Raid features serve all raids: no creature entry (Hakkar 14834, a Son 11357) in the code.
        for entry in ("14834", "11357", "GetEntry() =="):
            self.assertNotIn(entry, ACTIONS)

    def test_the_decision_goes_through_the_policy(self):
        decide = body(ACTIONS, "VictimEscape DecideFor(")
        self.assertIn("VictimEscapeFor(sPlayerbotAIConfig.dungeonRunImmuneBossVictimEscape, PlayerbotAI::IsTank(victim),", decide)
        self.assertIn("boss->GetVictim() == victim, BossInScope(boss)", decide)
        guards = body(ACTIONS, "VictimEscapeGuards GuardsFor(")
        # The slot is held 6 s only once a later read saw the escape land; a resisted one holds it 1 s.
        self.assertIn("if (!record.landed && EscapeLanded(boss, victim))", guards)
        self.assertIn("VictimEscapeRecent(true, record.landed, nowMs - record.castMs)", guards)
        self.assertIn("OtherNonTankTarget(boss, victim)", guards)
        self.assertIn("HoldsBossDuty(boss, victim)", guards)
        landed = body(ACTIONS, "bool EscapeLanded(")
        self.assertIn("victim->HasUnitState(UNIT_STAT_FEIGN_DEATH)", landed)
        self.assertIn("boss->GetVictim() != victim", landed)
        self.assertIn("victim->IsImmuneToDamage(boss->GetMeleeDamageSchoolMask())", landed)
        note = body(ACTIONS, "void NoteEscape(")
        self.assertIn("record.landed = false;", note)
        duty = body(ACTIONS, "bool HoldsBossDuty(")
        self.assertIn("VictimHoldsDuty(IsLiveWalkoutPuller(victim),", duty)
        self.assertIn("SeekMeetHealPuller(victim->GetInstanceId()) == victim->GetObjectGuid().GetRawValue()", duty)
        self.assertIn("OtherCreaturesOnVictim(boss, victim)", duty)
        others = body(ACTIONS, "int OtherCreaturesOnVictim(")
        for clause in ("attacker == boss", "!attacker->IsCreature()", "!sServerFacade.IsAlive(attacker)",
                       "!attacker->IsInCombat()", "attacker->GetVictim() != victim"):
            self.assertIn(clause, others)
        protect = body(ACTIONS, "bool PlanProtection(")
        self.assertIn("!member->HasAura(kForbearanceId) && EscapeSpellReady(ai, bot, bop, member)", protect)
        self.assertIn("ProtectorShouldBop(true, bopReady, false, victimEscape, distYd)", protect)
        self.assertIn("distYd > kVictimEscapeBopYd", protect)
        self.assertIn("LogVictimEscape(ai, boss, member, bopId, false, why, nowMs)", protect)

    def test_readiness_is_the_full_cast_check(self):
        ready = body(ACTIONS, "bool EscapeSpellReady(")
        self.assertIn('GetValue<uint32>("spell id", spell)', ready)
        self.assertIn("casterAi->CanCastSpell(spellId, target, 0, true, nullptr, false, false, false, &check)", ready)
        self.assertIn("check == SPELL_FAILED_SPELL_IN_PROGRESS", ready)
        self.assertIn("VictimEscapeCastReady(fullCheckOk, refusedOnlyByOwnCast, coreCheckOk)", ready)
        self.assertNotIn("Interrupt", ready)
        core = body(ACTIONS, "bool CoreCastCheckOk(")
        self.assertIn("spell->CheckCast(true)", core)
        self.assertIn("Spell::CalculatePowerCost(info, caster)", core)
        self.assertIn("return result == SPELL_CAST_OK;", core)
        of = body(ACTIONS, "VictimEscapeReady ReadyOf(")
        # A real player has no bot AI: no own escape, so a paladin protects them.
        self.assertIn("if (!memberAi)\n        return ready;", of)
        self.assertIn("const bool inControl = !member->HasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL);", of)
        self.assertIn("ready.feignDeath = inControl && EscapeSpellReady(", of)
        self.assertIn("ready.vanish = inControl && EscapeSpellReady(", of)
        self.assertIn("ready.iceBlock = EscapeSpellReady(", of)
        self.assertIn("ready.divineShield = !member->HasAura(kForbearanceId) &&", of)
        self.assertIn("const uint32 kForbearanceId = 25771;", ACTIONS)
        for word in ('"feign death"', '"vanish"', '"ice block"', '"divine shield"', '"hand of protection"'):
            self.assertIn(word, POLICY)

    def test_other_non_tank_target_filters(self):
        other = body(ACTIONS, "bool OtherNonTankTarget(")
        self.assertIn("player == victim", other)
        self.assertIn("!sServerFacade.IsAlive(player)", other)
        self.assertIn("!player->GetCharmerGuid().IsEmpty() || PlayerbotAI::IsTank(player)", other)

    def test_throttle_maps_evict_the_oldest(self):
        self.assertNotIn(".clear()", ACTIONS)
        self.assertIn("EvictOldest(g_victimEscapeLast,", ACTIONS)
        self.assertIn("EvictOldest(g_victimEscapeRefusedLog,", ACTIONS)
        log = body(ACTIONS, "void LogVictimEscape(")
        # The refused row's slot is the victim and the writer: a paladin's refusal is not hidden by the victim's.
        self.assertIn("std::make_pair(victim->GetObjectGuid().GetRawValue(), ai->GetBot()->GetObjectGuid().GetRawValue())", log)

    def test_cast_breaks_own_cast_and_notes_the_escape(self):
        # The interrupt only follows a plan whose full cast check passed: the triggers and actions plan first.
        for name in ("ImmuneBossVictimEscapeAction::Execute", "ImmuneBossVictimProtectAction::Execute"):
            execute = body(ACTIONS, "bool " + name + "(")
            self.assertLess(execute.index("Plan"), execute.index("CastEscape(ai, bot, plan)"))
        self.assertEqual(ACTIONS.count("StopOwnCast(bot);"), 1)
        self.assertNotIn("StopOwnCast", body(ACTIONS, "bool PlanOwnEscape("))
        self.assertNotIn("StopOwnCast", body(ACTIONS, "bool PlanProtection("))
        cast = body(ACTIONS, "bool CastEscape(")
        self.assertIn('GetValue<uint32>("spell id", spell)', cast)
        stop = cast.index("StopOwnCast(bot);")
        self.assertLess(stop, cast.index("ai->CastSpell(spell, plan.victim)"))
        self.assertLess(cast.index("ai->CastSpell(spell, plan.victim)"), cast.index("NoteEscape(plan.victim, nowMs);"))
        self.assertIn('"hand of protection"', POLICY)  # the paladin action's cast name on this core

    def test_row_format(self):
        log = body(ACTIONS, "void LogVictimEscape(")
        self.assertIn('"boss=%u,victim=%s,class=%s,spell=%u,result=%s"', log)
        self.assertIn('ok ? "ok" : "refused"', log)
        self.assertIn('"reason=%s"', log)
        self.assertIn('sPlayerbotAIConfig.logEvent(ai, "BossTacticsVictimEscape", info1, info2);', log)
        self.assertIn("nowMs - last < kVictimEscapeGapMs", log)  # the refused row's throttle
        self.assertIn("`BossTacticsVictimEscape`", REFUSAL_DOC)
        self.assertIn("BossTacticsVictimEscape", BOSS_DOC)

    def test_policy_test_names_the_failing_case(self):
        self.assertIn("CHECK(VictimEscapeFor(true, false, true, true, CLASS_MAGE, iceBlock) == VictimEscape::IceBlock);",
                      POLICY_TEST)


if __name__ == "__main__":
    unittest.main()
