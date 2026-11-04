"""The totem focus's glue: the read finds a boss's wild-summoned totem, the tick orders it first, and the focus
leaves the skull on the boss and cuts the DPS' cast before the attack.

T1: `ReadKillFirst` scans the plan's kill_first entries around an engaged plan boss under
AiPlayerbot.DungeonRunTotemFocus, because Jin'do's Brain Wash Totem (spell 24262) is SPELL_EFFECT_SUMMON_WILD: no
owner and no creator guid, so only the temporary summon's summoner names him, and no CONTROLLED_TOTEMS list holds it.
T2: the totem focus sits at the top of TickBody's fight branch, before HpHeldTick, the CC break, the retreat, CheckCc
and the automatic crowd control, so none of them spends its tick.
T3/T5: CommanderFocus sends the DPS by guid (`attack my target`) with no SetTargetIcon, interrupts the current cast
first and opens with an instant; the policy's IssueKillFirstFocus then leaves Run::focusIconGuid alone.
T4: the kill-first trigger checks every second under the key, every two seconds otherwise.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
POLICY = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
COMMANDER = (ROOT / "commands/Commander.cpp").read_text()
TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/BossTacticsTriggers.h").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class TotemFocusWiringTest(unittest.TestCase):
    def test_read_scans_the_boss_for_a_wild_summoned_totem(self):
        fn = body(COMMANDER, "void ReadKillFirst(ai::DungeonPlan const& plan")
        self.assertIn("if (f.totemFocus && unit->IsCreature() && planBoss(", fn)
        self.assertIn("GetCreatureListWithEntryInGrid(found, entry, TortoiseBots::DungeonRun::kKillFirstTotemGridYd)",
            fn)
        # 24262 sets no creator guid: the temporary summon's summoner is the boss.
        self.assertIn("GetSummonerGuid()", fn)
        self.assertIn("TortoiseBots::DungeonRun::KillFirstOwner(", fn)
        self.assertIn("TortoiseBots::DungeonRun::KillFirstGridTotemOf(", fn)
        # The plan's boss, by entry or by the entry its spawn row first had.
        self.assertIn("TortoiseBots::DungeonRun::ScriptChangedFrom(c)", fn)
        self.assertIn("step.kind == ai::PlanStepKind::Boss", fn)

    def test_the_totem_focus_is_first_in_the_fight_branch(self):
        tick = body(POLICY, "inline std::string TickBody(Run& r, const Facts& f, std::uint32_t dt, const RestLines& lines)\n{")
        focus = tick.index("AiPlayerbot.DungeonRunTotemFocus first")
        for later in ("else if (HpHeldTick(r, f, dt))", "if (RetreatTick(r, f, *s, dt, order))",
                "if (CheckCc(r, f, dt, order))", "if (RunAutoCcFightTick(r, f, *s, dt, order))",
                "if (CheckGuard(r, f, dt, order))"):
            self.assertLess(focus, tick.index(later), later)
        # ... and it is the totem branch, by guid, with the skull left alone.
        self.assertIn("return IssueKillFirstFocus(r, *totem, FocusMovesSkull(f.totemFocus, true))", tick)

    def test_the_generic_focus_leaves_the_skull_for_a_totem_too(self):
        tick = body(POLICY, "inline std::string TickBody(Run& r, const Facts& f, std::uint32_t dt, const RestLines& lines)\n{")
        self.assertIn(
            "order = IssueKillFirstFocus(r, *k, FocusMovesSkull(f.totemFocus, k->creatureType == kCreatureTotem));",
            tick)

    def test_focus_interrupts_then_attacks_by_guid_and_skips_the_skull(self):
        fn = body(COMMANDER, "static bool CommanderFocus(ChatHandler* handler")
        self.assertIn("if (!totemFocus)\n        order.group->SetTargetIcon(", fn)
        self.assertIn("accepted = TotemFocusBot(ai, bot, order.lead, target->GetObjectGuid());", fn)
        helper = body(COMMANDER, "static bool TotemFocusBot(PlayerbotAI* ai")
        self.assertLess(helper.index("bot->InterruptNonMeleeSpells(false)"),
            helper.index('ExecuteQuietAction(ai, "attack my target"'))
        self.assertIn('"fire blast", "shadowburn", "death coil", "arcane shot"', COMMANDER)
        self.assertIn("kTotemFocusOpeners", helper)

    def test_kill_first_spans_close_out_of_combat(self):
        i = COMMANDER.index("if (slot.run.plan && !slot.run.plan->killFirst.empty())")
        block = COMMANDER[i:i + 420]
        self.assertIn("if (f.anyInCombat)\n            ReadKillFirst(*slot.run.plan, lead, bots, f);", block)
        self.assertIn("LogKillFirstSpans(slot, lead, f);", block)

    def test_kill_first_trigger_drops_to_one_second_under_the_key(self):
        self.assertIn('Trigger(ai, "boss tactics kill first", sPlayerbotAIConfig.dungeonRunTotemFocus ? 1 : 2)', TRIGGERS)


if __name__ == "__main__":
    unittest.main()
