"""The run leader's pack-cleared gate and near-wipe stop are wired where their specs put them.

runtime/DungeonRunPolicy.h decides both: ClassifyPullRead classifies every
living creature of a pull step's entries (radius, residue band, spawn
ownership, waypoint followers), and NearWipeStopCount/NoteWipes mark wipe
events and pause the run. The glue in commands/Commander.cpp must feed them:
the pull step's read inside the existing per-entry grid loop (no new grid or
world search), the DB spawn row rather than GetRespawnCoord, the creature
group's formation leader, and the config keys read every tick so a reload
switches them mid-run. The keys must exist in the shipped template with the
same defaults the code falls back to.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDS = (ROOT / "commands/Commander.cpp").read_text()
CONFIG = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class PackClearGateWiringTest(unittest.TestCase):
    def test_pull_step_is_classified_in_the_existing_grid_loop(self):
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        pull = body(facts, "case ai::PlanStepKind::Pull:", "case ai::PlanStepKind::Move:")
        self.assertIn("ClassifyPullRead(read, step->radius, f.packClearMarginYd)", pull)
        # One grid search per listed entry, as before: the classification reads the creatures it found.
        self.assertEqual(pull.count("GetCreatureListWithEntryInGrid("), 1)

    def test_spawn_is_the_db_row_not_the_home_position(self):
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        pull = body(facts, "case ai::PlanStepKind::Pull:", "case ai::PlanStepKind::Move:")
        self.assertIn("creature->GetCreatureData()", pull)
        self.assertIn("data->position.x", pull)
        self.assertNotIn("GetRespawnCoord(", pull)

    def test_residue_units_carry_their_position(self):
        # OwnedByOtherStep reads a residue unit's point: x and y must come from the creature, not stay (0, 0).
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        pull = body(facts, "case ai::PlanStepKind::Pull:", "case ai::PlanStepKind::Move:")
        residue = body(pull, "if (residue)\n                    {", "f.packResidue.push_back(unit);")
        self.assertIn("unit.x = creature->GetPositionX();", residue)
        self.assertIn("unit.y = creature->GetPositionY();", residue)

    def test_formation_followers_are_read_from_the_group(self):
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        pull = body(facts, "case ai::PlanStepKind::Pull:", "case ai::PlanStepKind::Move:")
        self.assertIn("creature->GetCreatureGroup()", pull)
        self.assertIn("group->IsFormation()", pull)
        self.assertIn("group->GetLeaderGuid()", pull)

    def test_gate_keys_are_read_every_tick(self):
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        self.assertIn("f.packClearMarginYd = sPlayerbotAIConfig.dungeonRunPackClearMarginYd;", facts)
        self.assertIn("f.packClearRepulls = sPlayerbotAIConfig.dungeonRunPackClearRepulls;", facts)

    def test_near_wipe_stop_is_gated_after_the_raid_read(self):
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        raid = facts.index("f.raid.on = true;")
        gate = facts.index("NearWipeStopCount(f.raid.on,")
        self.assertGreater(gate, raid)
        self.assertIn("sPlayerbotAIConfig.dungeonRunNearWipeStopParty", facts)
        self.assertIn("sPlayerbotAIConfig.dungeonRunNearWipeStopMinutes * 60000u", facts)

    def test_keys_are_loaded_with_the_dungeon_run_keys(self):
        load = body(CONFIG, "void PlayerbotAIConfig::LoadDungeonRunKeys()", "\n}\n")
        for key in ("DungeonRunPackClearMarginYd", "DungeonRunPackClearRepulls", "DungeonRunNearWipeStop",
                    "DungeonRunNearWipeStopMinutes", "DungeonRunNearWipeStopParty"):
            self.assertIn('"AiPlayerbot.' + key + '"', load)

    def test_template_carries_the_keys_with_the_code_defaults(self):
        for key in ("DungeonRunPackClearMarginYd", "DungeonRunPackClearRepulls", "DungeonRunNearWipeStop",
                    "DungeonRunNearWipeStopMinutes", "DungeonRunNearWipeStopParty"):
            self.assertIn("AiPlayerbot." + key + " =", TEMPLATE)
        self.assertIn("AiPlayerbot.DungeonRunPackClearMarginYd = 10", TEMPLATE)
        self.assertIn("AiPlayerbot.DungeonRunPackClearRepulls = 3", TEMPLATE)
        self.assertIn("AiPlayerbot.DungeonRunNearWipeStop = 2", TEMPLATE)
        self.assertIn("AiPlayerbot.DungeonRunNearWipeStopMinutes = 10", TEMPLATE)
        self.assertIn("AiPlayerbot.DungeonRunNearWipeStopParty = 0", TEMPLATE)


if __name__ == "__main__":
    unittest.main()
