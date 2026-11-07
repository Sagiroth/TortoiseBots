"""The totem focus's glue: the read finds a boss's wild-summoned totem, the tick orders it first, and the focus
leaves the skull on the boss and cuts the DPS' cast before the attack.

T1: `ReadKillFirst` scans the plan's kill_first entries around an engaged plan boss under
AiPlayerbot.DungeonRunTotemFocus, because Jin'do's Brain Wash Totem (spell 24262) is SPELL_EFFECT_SUMMON_WILD: no
owner and no creator guid, so only the temporary summon's summoner names him, and no CONTROLLED_TOTEMS list holds it.
T2: the totem focus sits at the top of TickBody's fight branch, before HpHeldTick, the CC break, the retreat, CheckCc
and the automatic crowd control, so none of them spends its tick.
T3/T5: CommanderFocus sends the DPS by guid (`attack my target`) with no SetTargetIcon, cuts only its own generic cast
or channel through the policy's TotemFocusMayInterrupt first (only an offensive cast on a hostile target, so never a
heal, shield, buff or defensive cast; never an Auto Shot, a wand's shot, a run crowd-control spell, a resurrect or the
hearthstone) and opens with an instant; the policy's IssueKillFirstFocus then leaves Run::focusIconGuid alone.
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
        self.assertIn("TortoiseBots::DungeonRun::KillFirstGridReadsOwner(f.totemFocus, unit->IsCreature(),", fn)
        # The summoner fallback is the totem read's alone.
        self.assertIn(
            "if (f.totemFocus && unit->IsCreature() && static_cast<Creature*>(unit)->IsTemporarySummon())", fn)
        # One bounded grid visit per engaged boss, not one per plan entry: the visitor filters the entries.
        self.assertIn(
            "Cell::VisitGridObjects(boss, searcher, TortoiseBots::DungeonRun::kKillFirstTotemGridYd, false)", fn)
        self.assertIn(
            "std::unordered_set<std::uint32_t> const entries(plan.killFirst.begin(), plan.killFirst.end());", fn)
        self.assertIn("MaNGOS::CreatureListSearcher<KillFirstTotemCheck> searcher(found, check);", fn)
        # 24262 sets no creator guid: the temporary summon's summoner is the boss.
        visitor = body(COMMANDER, "struct KillFirstTotemCheck", "\n};\n")
        self.assertIn("entries.count(c->GetEntry())", visitor)
        self.assertIn("boss->IsWithinDist(c, TortoiseBots::DungeonRun::kKillFirstTotemGridYd, false)", visitor)
        self.assertIn("GetSummonerGuid()", visitor)
        self.assertIn("TortoiseBots::DungeonRun::KillFirstOwner(", visitor)
        self.assertIn("TortoiseBots::DungeonRun::KillFirstGridTotemOf(", visitor)
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

    def test_the_refocus_wants_three_dps_and_leaves_the_carriers_out(self):
        # t1106b: one hunter on the totem used to hold the re-send off for its whole life (any one DPS was enough).
        self.assertIn("constexpr std::uint32_t kTotemFocusDpsWanted = 3;", POLICY)
        self.assertIn("return std::min(kTotemFocusDpsWanted, dpsAlive);", POLICY)
        self.assertIn("k.dpsOnItCount < TotemFocusDpsWanted(dpsAlive)", POLICY)
        self.assertIn("if (FocusResendDue(r, *totem, f.dpsAlive))", POLICY)
        # The count leaves the tanks, the healers and the aura-gated carriers out, per bot and per unit.
        fn = body(COMMANDER, "void ReadKillFirst(ai::DungeonPlan const& plan")
        self.assertIn("std::uint32_t const aura = ai::GetBossTactics().RequiredAura(lead->GetMapId(), entry)", fn)
        self.assertEqual(fn.count("!isCarrier(bot)"), 2)
        self.assertIn("++fact.dpsOnItCount;", fn)
        # ... and a totem is never sent to a carrier (the fight scan drops a gated row for a bot without the aura).
        self.assertIn("bool const gatedCarrier = totemTarget && ai::CarriesAuraGatedKillFirst(ai, bot);", COMMANDER)
        self.assertIn("totemTarget, totemFocus, carrier, gatedCarrier)", COMMANDER)
        self.assertIn("return isTank || isHealer || gatedCarrier;", POLICY)

    def test_the_resend_goes_out_ahead_of_the_cc_and_the_guard(self):
        tick = body(POLICY, "inline std::string TickBody(Run& r, const Facts& f, std::uint32_t dt, const RestLines& lines)\n{")
        resend = tick.index("if (resendTotem != 0)")
        for later in ("if (CheckCc(r, f, dt, order))", "if (RunCcReapplyTick(r, f, dt, order))",
                "if (CheckGuard(r, f, dt, order))"):
            self.assertLess(resend, tick.index(later), later)
        # ... but after the fight ceiling and the retreat, which keep the tick.
        self.assertLess(tick.index("if (RetreatTick(r, f, *s, dt, order))"), resend)

    def test_one_row_per_bot_that_takes_the_totem_order(self):
        helper = body(COMMANDER, "static bool TotemFocusBot(PlayerbotAI* ai")
        self.assertIn('sPlayerbotAIConfig.logEvent(ai, "BossTacticsTotemFocus", info1, info2);', helper)
        self.assertIn('if (attacked && sPlayerbotAIConfig.hasLog("bot_events.csv"))', helper)
        self.assertIn("prevEntry", helper)
        self.assertIn("prevOnTotem", helper)
        self.assertIn("bot->GetDistance(totem)", helper)

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
        self.assertNotIn("InterruptNonMeleeSpells", helper)
        self.assertLess(helper.index("TotemFocusCutSlot(bot, CURRENT_GENERIC_SPELL, targetGuid)"),
            helper.index('ExecuteQuietAction(ai, "attack my target"'))
        self.assertIn("TotemFocusCutSlot(bot, CURRENT_CHANNELED_SPELL, targetGuid)", helper)
        self.assertIn('"fire blast", "shadowburn", "death coil", "arcane shot"', COMMANDER)
        self.assertIn("kTotemFocusOpeners", helper)
        cut = body(COMMANDER, "static void TotemFocusCutSlot(Player* bot")
        # A cast already aimed at the focused totem is not cut: compared by guid, never by the raw unit pointer.
        self.assertIn("!castGuid.IsEmpty() && castGuid == focusGuid", cut)
        self.assertIn("ObjectGuid focusGuid", COMMANDER)
        self.assertIn("TortoiseBots::DungeonRun::TotemFocusMayInterrupt(kind", cut)
        # Only a cast that is positively offense: the spell not beneficial, its target hostile.
        self.assertIn("spell->m_spellInfo->IsPositiveSpell()", cut)
        self.assertIn("TotemFocusTargetHostile(!castGuid.IsEmpty(), target != nullptr,", cut)
        self.assertIn("target && bot->IsHostileTo(target)", cut)
        # The cast's target is looked up by guid: the core's raw m_unitTarget pointer can be freed on the World thread.
        self.assertIn("spell->m_targets.getUnitTargetGuid()", cut)
        self.assertIn("map->GetUnit(castGuid)", cut)
        self.assertNotIn("getUnitTarget()", cut)
        self.assertIn("bot->InterruptSpell(slot, slot == CURRENT_CHANNELED_SPELL)", cut)

    def test_the_totem_focus_yields_to_the_retreat(self):
        tick = body(POLICY, "inline std::string TickBody(Run& r, const Facts& f, std::uint32_t dt, const RestLines& lines)\n{")
        self.assertIn("if (f.totemFocus && !r.retreatOn)", tick)

    def test_kill_first_spans_close_out_of_combat(self):
        i = COMMANDER.index("if (slot.run.plan && !slot.run.plan->killFirst.empty())")
        block = COMMANDER[i:i + 420]
        self.assertIn("if (f.anyInCombat)\n            ReadKillFirst(*slot.run.plan, lead, bots, f);", block)
        self.assertIn("LogKillFirstSpans(slot, lead, f);", block)

    def test_kill_first_trigger_drops_to_one_second_under_the_key(self):
        self.assertIn('Trigger(ai, "boss tactics kill first", sPlayerbotAIConfig.dungeonRunTotemFocus ? 1 : 2)', TRIGGERS)


if __name__ == "__main__":
    unittest.main()
