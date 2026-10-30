"""The run leader's glue reads which units the group can attack.

Gnomeregan r21-19z40e step 180: the Alarm-a-bomb 2600 (7897) kept the party in
combat, every `attack` on it was refused invalid-target ("The group cannot
attack that target."), and "a fight lasted more than 180 s" paused the run four
times. runtime/DungeonRunPolicy.h treats a fight with only units the group
cannot attack as no fight (UnattackableOnlyFight) and never pulls, picks up,
engages or focuses such a unit (UnitFact::attackable). This test pins that
commands/BotCommands.cpp feeds those rules: the same IsValidAttackTarget test
`cmd attack` refuses on, judged by the same bot, set on every unit fact the
run picks from, and the engaged units read on every tick in combat and for the
ceiling pause's self-resume.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDS = "\n".join((ROOT / "commands" / n).read_text() for n in ("Commander.cpp", "Action.cpp", "Party.cpp", "BotCommands.cpp"))


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0].rstrip() for line in text.splitlines())


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


CODE = strip_comments(COMMANDS)


class UnattackableFightWiringTest(unittest.TestCase):
    def test_the_check_is_the_attack_orders_own(self):
        # `cmd attack` refuses NotAttackable on the judge's IsValidAttackTarget.
        self.assertIn("facts.attackable = order.judge->IsValidAttackTarget(unit);", CODE)
        can = body(CODE, "bool GroupCanAttack(Player* judge, Unit* unit)")
        self.assertIn("judge->IsValidAttackTarget(unit)", can)
        # The judge is picked as CommanderOrder::judge is: the lead bot, else a living bot on its map; a charmed
        # bot or one not friendly to the lead bot never judges (test_boss_pair_wiring pins RunJudgeFit).
        judge = body(CODE, "Player* RunJudge(Player* lead, std::vector<Player*> const& bots)")
        self.assertIn("if (RunJudgeFit(lead, lead))\n        return lead;", judge)
        self.assertIn("if (RunJudgeFit(lead, bot))\n            return bot;", judge)
        fit = body(CODE, "bool RunJudgeFit(Player* lead, Player* bot)")
        self.assertIn("!bot->IsAlive() || !bot->IsInWorld() || bot->GetMap() != lead->GetMap()", fit)

    def test_engaged_units_feed_the_stale_test_and_the_fight_rule(self):
        party = body(CODE, "void ReadEngagedUnits(Player* lead, std::vector<Player*> const& bots,")
        read = body(CODE, "void ReadEngagedWith(Player* lead, Player* judge, Unit* unit,")
        self.assertIn("fact.attackable = GroupCanAttack(judge, other);", read)
        self.assertIn("out.push_back(fact);", read)
        self.assertIn("f.engagedRead = true;", party)
        # Both a bot's and its pet's attackers and victims, as the stale test read them.
        self.assertIn("for (Unit* attacker : unit->GetAttackers())", read)
        self.assertIn("add(unit->GetVictim());", read)
        self.assertIn("ReadEngagedWith(lead, judge, bot, seen, f.engagedUnits, &ep);", party)
        self.assertIn("ReadEngagedWith(lead, judge, pet, seen, f.engagedUnits, &ep);", party)
        self.assertIn("sServerFacade.IsFriendlyTo(other, lead)", read)
        facts = body(CODE, "void ReadRunFacts(DungeonRunSlot const& slot, Player* lead,")
        self.assertIn("ReadEngagedUnits(lead, bots, f);\n        f.staleCombat = f.engagedUnits.empty();", facts)

    def test_every_picked_unit_carries_attackable(self):
        facts = body(CODE, "void ReadRunFacts(DungeonRunSlot const& slot, Player* lead,")
        self.assertIn("Player* const judge = RunJudge(lead, bots);", facts)
        # The step's units (pull/boss, and a hold's), and the hop units.
        self.assertEqual(facts.count("unit.attackable = GroupCanAttack(judge, creature);"), 2)
        self.assertIn("fact.attackable = GroupCanAttack(judge, unit);\n                if (f.pullStand)", facts)
        guard = body(CODE, "void ReadGuardFacts(Player* lead, std::vector<Player*> const& bots,")
        self.assertIn("fact.attackable = GroupCanAttack(lead, nearestHit);", guard)
        # The anchor is the lead bot, or with it dead in a raid's boss fight a living bot (DungeonRun::GuardReadMode).
        self.assertIn("fact.attackable = GroupCanAttack(anchor, attacker);", guard)
        self.assertIn("Player* anchor = lead->IsAlive() ? lead : nullptr;", guard)
        kill = body(CODE, "void ReadKillFirst(ai::DungeonPlan const& plan, Player* lead,")
        self.assertIn("fact.attackable = GroupCanAttack(judge, unit);", kill)

    def test_ceiling_self_resume_counts_unattackable_combat_as_quiet(self):
        resume = body(CODE, "bool partyInCombat = false;", "CeilingFightOverWhilePaused(slot.run, partyInCombat, dt)")
        self.assertIn("ReadEngagedUnits(lead, pausedBots, engaged);", resume)
        self.assertIn("partyInCombat = TortoiseBots::DungeonRun::FightOn(slot.run, engaged, dt);", resume)

    def test_member_engaged_read_includes_threat_lists(self):
        member = body(CODE, "void ReadMemberEngaged(Player* lead, Player* judge, Player* bot,")
        self.assertIn("if (!MemberInCombat(bot))\n        return;", member)
        self.assertIn("ReadEngagedWith(lead, judge, bot, seen, member.engaged, nullptr, true);", member)
        self.assertIn("ReadEngagedWith(lead, judge, pet, seen, member.engaged, nullptr, true);", member)
        self.assertIn("member.engagedRead = true;", member)
        read = body(CODE, "void ReadEngagedWith(Player* lead, Player* judge, Unit* unit,")
        self.assertIn("if (threatRefs)", read)
        self.assertIn("sServerFacade.GetHostileRefManager(unit).getFirst(); ref; ref = ref->next()", read)
        self.assertIn("add(source->getOwner());", read)
        self.assertIn("fact.attackable = GroupCanAttack(judge, other);", read)
        facts = body(CODE, "void ReadRunFacts(DungeonRunSlot const& slot, Player* lead,")
        self.assertIn("member.inCombat = fighting;\n\n        ReadMemberEngaged(lead, judge, bot, member);", facts)
        # The party-wide fight read keeps attackers and victims only.
        party = body(CODE, "void ReadEngagedUnits(Player* lead, std::vector<Player*> const& bots,")
        self.assertNotIn(", true);", party)

    def test_combat_drop_runs_after_tick_on_rechecked_bots_only(self):
        # The test-speed apply (2026-10-25) sits between the tick and the drop: Tick, then the apply, then the
        # drop, then the order (round 1 restores the stronger pin).
        tick = body(CODE, "void TickDungeonRun(uint64 commanderRaw, uint32 dt)", "\n}\n")
        self.assertLess(tick.index("TortoiseBots::DungeonRun::Tick(slot.run, facts, dt);"),
                        tick.index("ApplyRunSpeed(commanderRaw, lead, bots, speedRate);"))
        self.assertLess(tick.index("ApplyRunSpeed(commanderRaw, lead, bots, speedRate);"),
                        tick.index("DropUnattackableCombat(lead, bots, slot.run.combatDrops);"))
        self.assertLess(tick.index("DropUnattackableCombat(lead, bots, slot.run.combatDrops);"),
                        tick.index("bool const accepted = IssueRunOrder(commander, order, answer,"))
        drop = body(CODE, "void DropUnattackableCombat(Player* lead, std::vector<Player*> const& bots,")
        self.assertIn("if (!bot->IsAlive() || !bot->IsInWorld() ||", drop)
        self.assertIn("ReadMemberEngaged(lead, judge, bot, now);", drop)
        self.assertIn("now.yd = bot == lead ? 0.0f : bot->GetMap() == lead->GetMap() ? bot->GetDistance(lead) : 1000.0f;",
                      drop)
        self.assertIn("if (!TortoiseBots::DungeonRun::HeldByUnattackable(now) && "
                      "!TortoiseBots::DungeonRun::HeldByFarUnitsOnly(now))\n            continue;", drop)
        self.assertLess(drop.index("ReadMemberEngaged(lead, judge, bot, now);"), drop.index("now.yd = "))
        self.assertIn("sServerFacade.GetHostileRefManager(pet).deleteReferences();\n            pet->CombatStop();", drop)
        self.assertIn("sServerFacade.GetHostileRefManager(bot).deleteReferences();\n        bot->CombatStop();", drop)
        # `bots` are the group's headless bots (never the commander).
        self.assertIn("std::vector<Player*> const bots = CommandedBots(group, lead);", CODE)
        fill = body(CODE, "void FillCommandedBots(Group* group, Player* lead, std::vector<Player*>& bots)")
        self.assertIn("IsLiveHeadlessBot(member)", fill)

    def test_a_pet_in_combat_keeps_its_bot_in_combat(self):
        shared = body(CODE, "bool MemberInCombat(Player* bot)")
        self.assertIn("bot->IsInCombat() || (pet && pet->IsAlive() && pet->IsInWorld() && pet->IsInCombat())", shared)
        facts = body(CODE, "void ReadRunFacts(DungeonRunSlot const& slot, Player* lead,")
        self.assertIn("bool const fighting = MemberInCombat(bot);\n        if (fighting)\n            f.anyInCombat = true;", facts)
        self.assertIn("member.inCombat = fighting;", facts)
        self.assertIn("for (Unit* attacker : pet->GetAttackers())", facts)
        self.assertNotIn("member.inCombat = bot->IsInCombat();", facts)
        member = body(CODE, "void ReadMemberEngaged(Player* lead, Player* judge, Player* bot,")
        self.assertIn("if (!MemberInCombat(bot))\n        return;", member)


if __name__ == "__main__":
    unittest.main()
