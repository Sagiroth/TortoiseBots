"""Pins the glue of fix/jindo-r1: the carrier's grid fallback to its Shade, the kill-first row's new words and fields,
and the taunt gate (a tank never taunts a Shade of Jin'do or a taunt-immune creature).

The rules themselves are unit-tested in tools/test_boss_tactics_policy.cpp (CarrierScanRefuses, CarrierScanPick,
CarrierScanReasonWord, TauntGateFor, TauntGateWord) and tools/test_attacker_of_non_tank_policy.cpp
(AttackerOfNonTankUsable's tauntRefused). These pins only prove the live code calls them, behind their keys, and that
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

    def test_the_scan_runs_only_for_the_carrier_mode_behind_its_key(self):
        body = member(ACTIONS, "float carrierReachYd)\n{", "\n}\n")
        # The gate: the key, the carrier mode and no pick at all. A player's order never gets here (mode None returns
        # at the top), and a free DPS keeps the ordinary pick.
        self.assertIn("if (sPlayerbotAIConfig.dungeonRunCarrierScan && mode == KillFirstPickMode::Carrier && "
                      "picked < 0)", body)
        self.assertIn("int const scanned = CarrierScanPick(facts, carrierReachYd);", body)
        self.assertIn("return scanUnits[scanned];", body)
        self.assertLess(body.index("mode == KillFirstPickMode::None"),
                        body.index("sPlayerbotAIConfig.dungeonRunCarrierScan"))
        # The fallback reads the same grid value the kill_first grid pass reads, through one shared owner test
        # (GridOwnerInFight, which the pass above also uses).
        self.assertIn("CollectCarrierScanHits(ai, bot, fight, facts, scanUnits);", body)
        self.assertIn("GridOwnerInFight(fight, hit.owner)", body)
        self.assertIn("? PickCarrierOverride(fight.rows, candidates, carrierReachYd)", body)
        self.assertIn(": PickKillFirst(fight.rows, candidates);", body)

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
        self.assertIn('snprintf(info2, sizeof(info2), "shade=%s,dist=%.1f,pick=%s,current=%s%s"', body)
        self.assertIn("char info2[256];", body)
        self.assertIn('case CarrierNoSwitch::NoShade:       reason = "not_read"; break;', body)
        self.assertIn('reason = "player_order";', body)
        # The word rule itself: a player's order, then the most common refusal, then no hit at all.
        words = member(ACTIONS, "static const char* CarrierScanWord(", "\n}\n")
        self.assertIn('return "player_order";', words)
        self.assertIn("return CarrierScanReasonWord(static_cast<CarrierScanRefusal>(best));", words)
        self.assertIn('return "no_unit";', words)
        self.assertIn('return "not_pickable";', words)
        self.assertIn("CarrierScanRefusal::BeyondReach", words)
        self.assertIn('LogCarrierNoSwitch(ai, bot, fight, pick, current, "attack_refused");', ACTIONS)
        self.assertIn("attack_refused", ACTIONS)

    def test_the_taunt_gate_reads_the_policy_and_logs_its_word(self):
        # The gate sits in the taunt actions, not in "attacker of non-tank target": that value also feeds the warrior
        # gap closer's Charge (WarriorGapCloser.cpp), which is no taunt.
        gate = member(ACTIONS, "bool ai::TauntRefusedByGate(", "\n}\n")
        self.assertIn("TauntGateFor(sPlayerbotAIConfig.dungeonRunTauntGate,", gate)
        self.assertIn("GetBossTactics().RequiredAura(bot->GetMapId(), target->GetEntry()), "
                      "TargetTauntImmune(target))", gate)
        self.assertIn('snprintf(info1, sizeof(info1), "reason=%s", TauntGateWord(gate));', gate)
        self.assertIn('sPlayerbotAIConfig.logEvent(ai, "BossTacticsTauntGate", info1, info2);', gate)
        self.assertIn('snprintf(info2, sizeof(info2), "target=%u:%u,spell=%s"', gate)
        self.assertIn("constexpr uint32 kTauntGateLogGapMs = 5000;", ACTIONS)

    def test_the_three_warrior_taunt_actions_gate(self):
        for cls, spell in (("class CastMockingBlowAction", '"mocking blow"'),
                           ("class CastTauntAction", '"taunt"')):
            body = member(WARRIOR, cls, "};")
            self.assertIn(f"TauntRefusedByGate(ai, bot, GetTarget(), {spell})", body)
        body = member(WARRIOR, "class CastTauntOnAttackerAction", "};")
        self.assertIn('TauntRefusedByGate(ai, bot, GetTarget(), "taunt")', body)
        self.assertIn("CastSnareSpellAction::isUseful()", body)
        # Challenging Shout is an area taunt with no single target: a documented limit, not gated.
        self.assertIn('MELEE_ACTION(CastChallengingShoutAction, "challenging shout");', WARRIOR)

    def test_the_scan_policy_is_pure(self):
        for name in ("enum class CarrierScanRefusal : std::uint8_t",
                     "struct CarrierScanFacts",
                     "struct CarrierScanTally",
                     "inline CarrierScanRefusal CarrierScanRefuses(const CarrierScanFacts& f, float reachYd)",
                     "inline int CarrierScanPick(const std::vector<CarrierScanFacts>& hits, float reachYd)",
                     "inline const char* CarrierScanReasonWord(CarrierScanRefusal refusal)",
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
