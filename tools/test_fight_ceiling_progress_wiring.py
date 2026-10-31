"""The run leader's progress-aware fight ceiling and the glue that feeds it (Dragonmaw Retreat r25 slot 7).

r25 slot 7 (2026-10-23) paused four times at step 260 on "a fight lasted more than 180 s (kept on by entry 62072
... attackable)" while the boss Searistrasz (62072) lost hp steadily from 100 % to 7 % over about 20 min with the
party fine. runtime/DungeonRunPolicy.h then held the ceiling while an engaged unit lost hp (FightProgressTick, with
a hard cap) and gave a plan boss engaged on any step the boss ceiling (EngagedPlanBoss). Its decisions are tested in
tools/test_dungeon_run_policy.cpp; this test pins that commands/BotCommands.cpp feeds them (every engaged unit's hp
on every tick in combat, the real tick length) and that the policy's fight block runs the watch on every fight tick
before the ceiling's pause.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDS = "\n".join((ROOT / "commands" / n).read_text() for n in ("Commander.cpp", "Action.cpp", "Party.cpp", "BotCommands.cpp"))
POLICY = (ROOT / "runtime/DungeonRunPolicy.h").read_text()


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0].rstrip() for line in text.splitlines())


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


CODE = strip_comments(COMMANDS)
RULES = strip_comments(POLICY)


class FightCeilingProgressWiringTest(unittest.TestCase):
    def test_the_glue_reads_every_engaged_units_hp_in_combat(self):
        facts = body(CODE, "void ReadRunFacts(DungeonRunSlot const& slot, Player* lead,")
        self.assertIn("if (f.anyInCombat)\n    {\n        ReadEngagedUnits(lead, bots, f);", facts)
        units = body(CODE, "void ReadEngagedUnits(Player* lead, std::vector<Player*> const& bots,")
        self.assertIn("ReadEngagedWith(lead, judge, bot, seen, f.engagedUnits, &ep);", units)
        self.assertIn("ReadEngagedWith(lead, judge, pet, seen, f.engagedUnits, &ep);", units)
        self.assertIn("f.engagedRead = true;", units)
        read = body(CODE, "void ReadEngagedWith(Player* lead, Player* judge, Unit* unit,")
        self.assertLess(read.index("fact.health = other->GetHealth();"), read.index("out.push_back(fact);"))
        self.assertLess(read.index("fact.maxHealth = other->GetMaxHealth();"), read.index("out.push_back(fact);"))
        # The watch's clock is the tick's real length.
        self.assertIn(": TortoiseBots::DungeonRun::Tick(slot.run, facts, dt);", CODE)

    def test_the_fight_block_watches_every_fight_tick_before_the_pause(self):
        fight = body(RULES, "    if (f.anyInCombat && !objectDone)\n    {", "\n    }\n")
        watch = fight.index("FightProgress const progress = FightProgressTick(r, f, dt);")
        # Before any return of the fight block's ceiling: the 1-hp watch's pause and the ceiling's own.
        self.assertLess(watch, fight.index("if (r.fightMs > stopCap && autoRegroup)"))
        self.assertLess(watch, fight.index("if (pastCeiling && (!progressHolds || hardCapped))"))
        self.assertIn(
            "bool const bossStep = s->kind == PlanStepKind::Boss || EngagedPlanBoss(r, f) || r.raidBossFight;", fight)
        # A raid's boss fight gets kRaidBossFightCapMs only with AiPlayerbot.DungeonRunRaidBossRules.
        self.assertIn(": !bossStep ? kFightCapMs", fight)
        self.assertIn(": RaidBossRulesFight(r, f) ? kRaidBossFightCapMs", fight)
        self.assertIn(": kBossFightCapMs;", fight)
        tick = body(RULES, "inline std::string Tick(Run& r, const Facts& raw, std::uint32_t dt")
        self.assertIn("ClearFightProgress(r);", tick)
        reset = body(RULES, "inline void ResetStep(Run& r)")
        self.assertIn("ClearFightProgress(r);", reset)


if __name__ == "__main__":
    unittest.main()
