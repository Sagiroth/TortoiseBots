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
        # The step's units (pull/boss, and a hold's), the hop units, and a stand-pull pull row's patrollers.
        self.assertEqual(facts.count("unit.attackable = GroupCanAttack(judge, creature);"), 3)
        self.assertIn("fact.attackable = GroupCanAttack(judge, unit);\n                if (f.pullStand)", facts)
        guard = body(CODE, "void ReadGuardFacts(Player* lead, std::vector<Player*> const& bots,")
        self.assertIn("fact.attackable = GroupCanAttack(lead, nearestHit);", guard)
        # The anchor is the lead bot, or with it dead in a raid's boss fight a living bot (DungeonRun::GuardReadMode).
        self.assertIn("fact.attackable = GroupCanAttack(anchor, attacker);", guard)
        self.assertIn("Player* const anchor = RaidGuardAnchor(lead, bots, f);", guard)
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
                        tick.index("DropUnattackableCombat(lead, bots, slot.run.combatDrops, slot.run.combatDropWedged);"))
        self.assertLess(tick.index("DropUnattackableCombat(lead, bots, slot.run.combatDrops, slot.run.combatDropWedged);"),
                        tick.index("bool const accepted = IssueRunOrder(commander, order, answer,"))
        drop = body(CODE, "void DropUnattackableCombat(Player* lead, std::vector<Player*> const& bots,")
        self.assertIn("if (!bot->IsAlive() || !bot->IsInWorld() ||", drop)
        self.assertIn("ReadMemberEngaged(lead, judge, bot, now);", drop)
        self.assertIn("now.yd = bot == lead ? 0.0f : bot->GetMap() == lead->GetMap() ? bot->GetDistance(lead) : 1000.0f;",
                      drop)
        self.assertIn("else if (!TortoiseBots::DungeonRun::HeldByUnattackable(now) &&\n"
                      "            !TortoiseBots::DungeonRun::HeldByFarUnitsOnly(now))\n            continue;", drop)
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

    def test_combat_drop_rechecks_a_wedged_bot_and_tick_summons_it(self):
        # ZG walks 23-25: Nack/Maevrayah/Elmind wedged below the Jeklik path while the raid fought on. The glue's
        # drop re-checks the bot still stands wedged (DungeonRun::WedgedDropRecheck, dz = bot z - lead z, attackers
        # read the same core attacker sets ReadRunFacts fills MemberFact::attackers with, no scan); the tick
        # summons a wedged raid member before the guard looks and before the runaway.
        policy = (ROOT / "runtime" / "DungeonRunPolicy.h").read_text()
        drop = body(CODE, "void DropUnattackableCombat(Player* lead, std::vector<Player*> const& bots,")
        self.assertIn("ReadMemberParticipation(bot, now);", drop)
        self.assertIn("wedged = TortoiseBots::DungeonRun::WedgedDropRecheck(now.yd, wedgedDz, wedgedAttackers,\n"
                      "                TortoiseBots::DungeonRun::MemberParticipates(now), now.losLead, now.pathToLead, now.pathLenYd);", drop)
        # The wedged re-check runs only for the bots the run named wedged (Run::combatDropWedged), and it needs the
        # same reach facts the run's own test read (LOS, a complete path and its length, at most once every 5 s).
        self.assertIn("bool const namedWedged = wedgedNames.count(bot->GetName()) != 0;", drop)
        self.assertIn("ReadMemberReach(lead, bot, now);", drop)
        self.assertIn("TortoiseBots::DungeonRun::MemberParticipates(now), now.losLead, now.pathToLead, now.pathLenYd);",
                      drop)
        self.assertIn("if (!wedged)\n                continue;", drop)
        # A bot named for an unattackable or far unit is judged by those only: its drop is not decided by looking
        # wedged on the re-read.
        self.assertIn("else if (!TortoiseBots::DungeonRun::HeldByUnattackable(now) &&\n"
                      "            !TortoiseBots::DungeonRun::HeldByFarUnitsOnly(now))\n            continue;", drop)
        # The participation clock is actual progress only: the bot's own cast, auto-repeat and swing state, never
        # its selected target or its place on a threat list (a wedged bot keeps a target it cannot reach). It is
        # pruned by last-seen time, never by last progress (a bot that has not progressed yet keeps its clock).
        part = body(CODE, "void ReadMemberParticipation(Player* bot, TortoiseBots::DungeonRun::MemberFact& member)")
        self.assertIn("clock.seenMs = now;", part)
        self.assertIn("if (ParticipationClocks().size() > TortoiseBots::DungeonRun::kParticipationClocksMax)\n"
                      "        TortoiseBots::DungeonRun::PruneParticipationClocks(ParticipationClocks(), now);", part)
        self.assertIn("if (MemberProgressed(bot, clock))\n        clock.actMs = now;", part)
        self.assertIn("member.actedMs = clock.actMs ? WorldTimer::getMSTimeDiff(clock.actMs, now) : 0xFFFFFFFF;", part)
        self.assertNotIn("member.engaged", part)
        progress = body(CODE, "bool MemberProgressed(Player* bot, MemberParticipation& clock)")
        self.assertIn("bool const inFight = bot->IsInCombat();", progress)
        self.assertIn("inFight ? ProgressSpellId(bot) : 0", progress)
        self.assertIn("TortoiseBots::DungeonRun::ParticipationProgress(inFight, spell, clock.observed,\n"
                      "        clock.spellId, baseSwing, rangedSwing);", progress)
        self.assertIn("bot->GetAttackTimer(BASE_ATTACK)", progress)
        # A shot that fired is the ranged timer reset above the auto-repeat re-arm shove (exactly 500 ms): the
        # auto-repeat spell being on is never progress, so a bot with Auto Shot on and no shot stays stale.
        self.assertIn("bot->GetAttackTimer(RANGED_ATTACK)", progress)
        self.assertIn("rangedTimer > kRangedShotTimerMs && rangedTimer > clock.rangedTimer", progress)
        self.assertIn("constexpr std::uint32_t kRangedShotTimerMs = 500;", CODE)
        self.assertNotIn("AutoRepeatSpellId", progress)
        self.assertNotIn("CURRENT_AUTOREPEAT_SPELL", progress)
        self.assertNotIn("GetTargetGuid", progress)
        self.assertNotIn("member.engaged", progress)
        # The cast the clock reads is not a self-cast.
        spell = body(CODE, "uint32 ProgressSpellId(Unit* unit)")
        self.assertIn("spell->m_targets.getUnitTargetGuid() == unit->GetObjectGuid()", spell)
        self.assertIn("TortoiseBots::DungeonRun::ProgressSpell(spell->m_spellInfo->Id, targetSelf);", spell)
        participates = body(policy, "inline bool MemberParticipates(const MemberFact& m)\n{")
        self.assertIn("return m.actedMs < kParticipateMs;", participates)
        self.assertNotIn("targetGuid", participates)
        self.assertNotIn("healMs", participates)
        self.assertIn("float const wedgedDz = bot->GetMap() == lead->GetMap() ? bot->GetPositionZ() - lead->GetPositionZ() : 0.0f;", drop)
        self.assertIn("for (Unit* attacker : bot->GetAttackers())", drop)
        self.assertIn("for (Unit* attacker : nowPet->GetAttackers())", drop)
        self.assertIn("(wedged on another floor)", drop)
        tick_body = body(policy, "inline std::string TickBody(Run& r, const Facts& f, std::uint32_t dt, const RestLines& lines)\n{",
                         "\n}\n")
        guard = tick_body.index("if (CheckGuard(r, f, dt, order))")
        self.assertLess(tick_body.index("if (order.empty() && WedgedSummon(r, f, dt, order))"), guard)
        runaway = tick_body.index("if (RunawaySummon(r, f, dt, order))")
        self.assertLess(tick_body.index("if (order.empty() && WedgedSummon(r, f, dt, order))"), runaway)


if __name__ == "__main__":
    unittest.main()
