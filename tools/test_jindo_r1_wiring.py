"""Pins the glue of fix/jindo-r1: the carrier's grid fallback to its Shade, the kill-first row's new words and fields,
and the taunt gate (a tank never taunts a Shade of Jin'do or a taunt-immune creature).

The rules themselves are unit-tested in tools/test_boss_tactics_policy.cpp (CarrierScanRefuses, CarrierScanPick,
CarrierScanReasonWord, CarrierScanWord, TauntGateFor, TauntGateWord) and tools/test_attacker_of_non_tank_policy.cpp
(AttackerOfNonTankSkipsUnit). These pins only prove the live code calls them, behind their keys, and that
both keys ship off.
"""
import os
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read(path):
    with open(os.path.join(ROOT, path), encoding="utf-8") as handle:
        return handle.read()


ACTIONS = read("ai/playerbot/strategy/actions/BossTacticsActions.cpp")
POLICY = read("ai/playerbot/BossTacticsPolicy.h")
TRIGGERS = read("ai/playerbot/strategy/triggers/BossTacticsTriggers.cpp")
WARRIOR = read("ai/playerbot/strategy/warrior/WarriorActions.h")
DRUID = read("ai/playerbot/strategy/druid/DruidActions.h")
PALADIN = read("ai/playerbot/strategy/paladin/PaladinActions.h")
SHAMAN = read("ai/playerbot/strategy/shaman/ShamanActions.h")
NON_TANK_VALUE = read("ai/playerbot/strategy/values/AttackerOfNonTankTargetValue.cpp")
NON_TANK_POLICY = read("ai/playerbot/policy/combat/AttackerOfNonTankPolicy.h")
CONFIG_H = read("ai/playerbot/PlayerbotAIConfig.h")
CONFIG_CPP = read("ai/playerbot/PlayerbotAIConfig.cpp")
CONF = read("ai/playerbot/aiplayerbot.conf.dist.in")
MIGRATION = read("data/sql/world/20261108140000_world.sql")


def member(text, start, end):
    """The text from `start` up to the next `end`."""
    begin = text.index(start)
    return text[begin:text.index(end, begin)]


class JindoR1Wiring(unittest.TestCase):
    def test_both_keys_ship_off(self):
        for field, key in (("dungeonRunCarrierScan", "DungeonRunCarrierScan"),
                           ("dungeonRunTauntGate", "DungeonRunTauntGate")):
            self.assertIn(f"bool {field} = false;", CONFIG_H)
            self.assertIn(f'config.GetBoolDefault("AiPlayerbot.{key}", false)', CONFIG_CPP)
            self.assertIn(f"AiPlayerbot.{key} = 0", CONF)

    def test_the_scan_runs_for_every_mode_but_none_behind_its_key(self):
        body = member(ACTIONS, "float carrierReachYd)\n{", "\n}\n")
        # The scan runs BEFORE the ordinary pick is accepted, for every mode but None: a free DPS takes every open row, so
        # a ward or a totem in view would win the ordinary pick while a Shade lives (Codex, round 3). It is no longer
        # gated on "no ordinary pick at all".
        self.assertIn("if (sPlayerbotAIConfig.dungeonRunCarrierScan && isCarrier && mode != KillFirstPickMode::None &&\n"
                      "        !ordinaryIsCarrierEntry)", body)
        self.assertNotIn("picked < 0)\n    {", body)
        self.assertNotIn("dungeonRunCarrierScan && mode == KillFirstPickMode::Carrier", body)
        self.assertIn("bool const isCarrier = HasCarrierKillFirstRow(fight.rows);", body)
        self.assertIn("int const scanned = CarrierScanPick(facts, carrierReachYd);", body)
        # The one decision is the policy's, fed the carrier flag, the key, the mode and the scan's result, and the scan's
        # Shade is what returns when it wins; otherwise the ordinary pick is returned last.
        self.assertIn("if (CarrierScanTakesPrecedence(isCarrier, sPlayerbotAIConfig.dungeonRunCarrierScan, mode, "
                      "scanUnit != nullptr,\n                ordinaryIsCarrierEntry))", body)
        self.assertIn("return scanUnit;", body)
        self.assertTrue(body.rstrip().endswith("return ordinary;"))
        self.assertLess(body.index(": PickKillFirst(fight.rows, candidates);"),
                        body.index("CarrierScanTakesPrecedence("))
        self.assertLess(body.index("CarrierScanTakesPrecedence("), body.index("return scanUnit;"))
        self.assertLess(body.index("return scanUnit;"), body.rindex("return ordinary;"))
        self.assertLess(body.index("mode == KillFirstPickMode::None"),
                        body.index("sPlayerbotAIConfig.dungeonRunCarrierScan"))
        # A scan Shade that replaces an ordinary pick logs the over= row.
        self.assertIn("LogCarrierScanOver(ai, bot, scanUnit, ordinary, mode);", body)
        over = member(ACTIONS, "void LogCarrierScanOver(", "\n}\n")
        self.assertIn('"pick=%u:%u,over=%u,mode=%s"', over)
        self.assertIn('"BossTacticsKillFirst", "reason=scan_over", info2', over)
        # The fallback reads the same grid value the kill_first grid pass reads, through one shared owner test
        # (GridOwnerInFight, which the pass above also uses).
        self.assertIn("CollectCarrierScanHits(ai, bot, fight, facts, scanUnits);", body)
        self.assertIn("GridOwnerInFight(fight, hit.owner)", body)
        self.assertIn("? PickCarrierOverride(fight.rows, candidates, carrierReachYd)", body)
        self.assertIn(": PickKillFirst(fight.rows, candidates);", body)

    def test_the_precedence_policy_is_pure(self):
        self.assertIn("inline bool CarrierScanTakesPrecedence(bool isCarrier, bool keyOn, KillFirstPickMode mode, "
                      "bool scanFound,", POLICY)
        self.assertIn("return keyOn && isCarrier && mode != KillFirstPickMode::None && scanFound && "
                      "!ordinaryIsCarrierEntry;", POLICY)

    def test_the_scan_facts_read_the_checks_the_policy_names(self):
        scan = member(ACTIONS, "void CollectCarrierScanHits(", "\n}\n")
        self.assertIn("factsOfHit.gridEligible = KillFirstGridEligible(hit.alive, hit.evading, hit.victimOurs, "
                      "hit.weHoldIt,\n                GridOwnerInFight(fight, hit.owner));", scan)
        self.assertIn("factsOfHit.distanceYd = hit.distance;", scan)
        self.assertIn("factsOfHit.los = bot->IsWithinLOSInMap(unit);", scan)
        self.assertIn("factsOfHit.validAttack = bot->IsValidAttackTarget(unit);", scan)

    def test_the_row_words_come_from_the_scan_only_behind_its_key(self):
        body = member(ACTIONS, "void ai::LogCarrierNoSwitch(", "\n}\n")
        # The scan's words and codes only under the key; flag off keeps the old words and the old info2.
        self.assertIn("if (sPlayerbotAIConfig.dungeonRunCarrierScan)", body)
        self.assertLess(body.index("if (sPlayerbotAIConfig.dungeonRunCarrierScan)"),
                        body.index("CarrierScanReasonWord("))
        self.assertIn('",codes=', body)
        # The one place scanText is filled sits inside the key's block, before the old-words branch: the flag-off row
        # never gains `,codes=` (byte-identical info2).
        self.assertEqual(body.count("snprintf(scanText"), 1)
        self.assertEqual(body.count("scanText"), 5)  # declared, cleared, filled (twice on that line), appended to info2
        self.assertIn("scanText[0] = '\\0';", body)
        flag_block = member(body, "if (sPlayerbotAIConfig.dungeonRunCarrierScan)", "    else if (!reason)")
        self.assertIn("snprintf(scanText", flag_block)
        self.assertIn(",mode=%s", flag_block)
        self.assertNotIn("scanText", member(body, "    else if (!reason)", "    char info1[64];"))
        self.assertIn("currentText, scanText);", body)
        # The log throttle comes before any scan work (the tally reads the grid and asks the core for every hit).
        self.assertLess(body.index("CarrierNoSwitchLogDue(bot)"), body.index("CarrierScanTallyOf("))
        self.assertIn('snprintf(info2, sizeof(info2), "shade=%s,dist=%.1f,pick=%s,current=%s%s"', body)
        self.assertIn("char info2[256];", body)
        self.assertIn('case CarrierNoSwitch::NoShade:       reason = "not_read"; break;', body)
        self.assertIn('reason = "player_order";', body)
        # The word rule itself: a player's order, then the most common refusal, then no hit at all.
        words = member(POLICY, "inline const char* CarrierScanWord(", "\n    }\n")
        self.assertIn('return "player_order";', words)
        self.assertIn("return CarrierScanReasonWord(static_cast<CarrierScanRefusal>(best));", words)
        self.assertIn("CarrierScanWord(tally, shade != nullptr, pickableNow)", body)
        self.assertIn('return "no_unit";', words)
        self.assertIn('return "not_pickable";', words)
        self.assertIn("CarrierScanRefusal::BeyondReach", words)
        self.assertIn('LogCarrierNoSwitch(ai, bot, fight, pick, current, "attack_refused");', ACTIONS)
        self.assertIn("attack_refused", ACTIONS)

    def test_the_taunt_gate_reads_the_policy_and_logs_its_word(self):
        # The gate sits in the taunt actions, not in "attacker of non-tank target": that value also feeds the warrior
        # gap closer's Charge (WarriorGapCloser.cpp), which is no taunt.
        gate = member(ACTIONS, "bool ai::TauntRefusedByGate(", "\n}\n")
        # The key is read first: with it off the gate does no lookup at all.
        self.assertTrue(gate.split("{", 1)[1].lstrip().startswith(
            "if (!sPlayerbotAIConfig.dungeonRunTauntGate)\n        return false;"))
        self.assertIn("TauntGateFor(sPlayerbotAIConfig.dungeonRunTauntGate,", gate)
        self.assertIn("GetBossTactics().RequiredAura(bot->GetMapId(), target->GetEntry()), "
                      "TargetTauntImmune(target))", gate)
        self.assertIn('snprintf(info1, sizeof(info1), "reason=%s", TauntGateWord(gate));', gate)
        self.assertIn('sPlayerbotAIConfig.logEvent(ai, "BossTacticsTauntGate", info1, info2);', gate)
        self.assertIn('snprintf(info2, sizeof(info2), "target=%u:%u,spell=%s"', gate)
        self.assertIn("constexpr uint32 kTauntGateLogGapMs = 5000;", ACTIONS)

    def test_every_single_target_taunt_is_gated(self):
        for cls, spell in (("class CastMockingBlowAction", '"mocking blow"'),
                           ("class CastTauntAction", '"taunt"'),
                           ("class CastTauntOnSnareTargetAction", '"taunt"')):
            body = member(WARRIOR, cls, "};")
            self.assertIn(f"!TauntRefusedByGate(ai, bot, GetTarget(), {spell})", body)
        body = member(WARRIOR, "class CastTauntOnAttackerAction", "};")
        self.assertIn('TauntRefusedByGate(ai, bot, GetTarget(), "taunt")', body)
        self.assertIn("CastSnareSpellAction::isUseful()", body)
        # The druid, paladin and shaman taunts. The on-attacker actions derive from these and inherit the gate.
        for text, cls, spell, derived in (
                (DRUID, "class CastGrowlAction", '"growl"', "class CastGrowlOnAttackerAction : public CastGrowlAction"),
                (PALADIN, "class CastHandOfReckoningAction", '"hand of reckoning"',
                 "class CastHandOfReckoningOnAttackerAction : public CastHandOfReckoningAction"),
                (SHAMAN, "class CastEarthshakerSlamAction", '"earthshaker slam"',
                 "class CastEarthshakerSlamOnAttackerAction : public CastEarthshakerSlamAction")):
            body = member(text, cls, "};")
            self.assertIn(f"!TauntRefusedByGate(ai, bot, GetTarget(), {spell})", body)
            self.assertIn("virtual bool isUseful() override", body)
            self.assertIn(derived, text)
            self.assertNotIn("isUseful", member(text, derived, "};"))
        # The boss actions that cast a class taunt themselves: the swap, the seek take, the taunt-after window.
        self.assertIn("if (!taunt || TauntRefusedByGate(ai, bot, task.boss, taunt))", ACTIONS)
        self.assertIn("!canTaunt || TauntRefusedByGate(ai, bot, task.source, taunt)", ACTIONS)
        self.assertIn("if (!taunt || TauntRefusedByGate(ai, bot, task.subject, taunt))", ACTIONS)
        # Challenging Shout and Challenging Roar are area taunts with no single target: a documented limit, not gated.
        self.assertIn('MELEE_ACTION(CastChallengingShoutAction, "challenging shout");', WARRIOR)

    def test_the_attacker_value_skips_needs_aura_units_for_both_users(self):
        self.assertIn("std::uint32_t const requiredAura = GetBossTactics().RequiredAura(bot->GetMapId(), "
                      "unit->GetEntry());", NON_TANK_VALUE)
        # The bot's own aura is passed: a cursed tank can hurt the Shade, so the Shade stays its target.
        self.assertIn("        if (AttackerOfNonTankSkipsUnit(sPlayerbotAIConfig.dungeonRunTauntGate, requiredAura,\n"
                      "                requiredAura != 0 && bot->HasAura(requiredAura)))\n"
                      "            continue;", NON_TANK_VALUE)
        # The skip comes before the unit becomes a candidate.
        self.assertLess(NON_TANK_VALUE.index("AttackerOfNonTankSkipsUnit("),
                        NON_TANK_VALUE.index("candidates.push_back(candidate)"))
        self.assertIn("return tauntGateOn && requiredAura != 0 && !botHasAura;", NON_TANK_POLICY)
        self.assertNotIn("tauntRefused", NON_TANK_POLICY)
        self.assertNotIn("tauntRefused", NON_TANK_VALUE)

    def test_the_scan_policy_is_pure(self):
        for name in ("enum class CarrierScanRefusal : std::uint8_t",
                     "struct CarrierScanFacts",
                     "struct CarrierScanTally",
                     "inline CarrierScanRefusal CarrierScanRefuses(const CarrierScanFacts& f, float reachYd)",
                     "inline int CarrierScanPick(const std::vector<CarrierScanFacts>& hits, float reachYd)",
                     "inline const char* CarrierScanReasonWord(CarrierScanRefusal refusal)",
                     "inline const char* CarrierScanWord(const CarrierScanTally& tally, bool shadeInFight, "
                     "bool pickable)",
                     "enum class TauntGate : std::uint8_t { Allow, NeedsAura, TauntImmune };",
                     "inline TauntGate TauntGateFor(bool keyOn, std::uint32_t requiredAura, bool tauntImmune)",
                     "inline const char* TauntGateWord(TauntGate gate)"):
            self.assertIn(name, POLICY)
        # The scan order is the policy's, and it is checked in order, never as one conjunction.
        refuses = member(POLICY, "inline CarrierScanRefusal CarrierScanRefuses(", "\n    }\n")
        for earlier, later in (("if (!f.unitFound)", "if (!f.gridEligible)"),
                               ("if (!f.gridEligible)", "if (f.distanceYd < 0.0f || f.distanceYd > reachYd)"),
                               ("if (f.distanceYd < 0.0f || f.distanceYd > reachYd)", "if (!f.los)"),
                               ("if (!f.los)", "if (!f.validAttack)")):
            self.assertLess(refuses.index(earlier), refuses.index(later))

    def test_the_trigger_still_logs_the_no_pick_row(self):
        self.assertIn('LogCarrierNoSwitch(ai, bot, fight, nullptr, AI_VALUE(Unit*, "current target"));', TRIGGERS)

    def test_the_migration_adds_the_two_aura_rows_only(self):
        self.assertIn("(17172, 'Zul''Gurub: Hex (Jin''do, SPELL_HEX boss_jindo.cpp:33)')", MIGRATION)
        self.assertIn("(24261, 'Zul''Gurub: Brain Wash (Jin''do totem 15112, SPELL_BRAINWASH boss_jindo.cpp:38)')",
                      MIGRATION)
        self.assertIn("DELETE FROM `ai_combat_aura_watch` WHERE `spell_id` IN (17172, 24261);", MIGRATION)
        self.assertNotIn("CREATE TABLE", MIGRATION)


if __name__ == "__main__":
    unittest.main()
