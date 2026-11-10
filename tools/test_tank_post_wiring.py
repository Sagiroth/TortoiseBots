"""Wiring pins for the tank_post boss-tactic kind.

The decision is pure (ai/playerbot/BossTacticsPolicy.h: TankPostStep, TankPostArrived) and tested in
tools/test_boss_tactics_policy.cpp; the loader shape is pinned there too and the row shape in
tools/test_boss_tactics_sql.py. These pins prove the live code calls the rules: the fight scan drops the rows without
their own key, the find picks only the row's tank and searches the awaited unit by grid, the action attacks on arrival
and drops only the boss, the trigger and action are registered, and the key is read, templated and documented.

Run: python3 -m unittest tools/test_tank_post_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
POLICY = (ROOT / "ai/playerbot/BossTacticsPolicy.h").read_text()
MGR = (ROOT / "ai/playerbot/BossTacticsMgr.cpp").read_text()
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
ACTIONS_H = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.h").read_text()
TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/BossTacticsTriggers.cpp").read_text()
STRATEGY = (ROOT / "ai/playerbot/strategy/generic/BossTacticsStrategy.cpp").read_text()
ACTION_CONTEXT = (ROOT / "ai/playerbot/strategy/actions/ActionContext.h").read_text()
TRIGGER_CONTEXT = (ROOT / "ai/playerbot/strategy/triggers/TriggerContext.h").read_text()
CONFIG_CPP = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
DOCS = (ROOT / "docs/server/settings/dungeons-raids.md").read_text()
BOSS_DOCS = (ROOT / "docs/play/boss-tactics.md").read_text()
CHANGELOG = (ROOT / "CHANGELOG.md").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class TankPostWiring(unittest.TestCase):
    def test_kind_is_parsed_and_validated(self):
        self.assertIn('if (text == "tank_post")\n            return BossTacticKind::TankPost;', POLICY)
        loader = body(POLICY, "            // tank_post: the awaited unit")
        for needle in ("row.targetEntry == 0", "row.priority < 1", "row.priority > 7", "!row.HasSpot()",
                       "row.param < 5.0f", "row.param > 40.0f", "row.windowSec < 10.0f", "row.windowSec > 120.0f",
                       "row.spellId != 0", "row.mode != TacticMode::Default"):
            self.assertIn(needle, loader)
        # The spot columns belong to the kind, and its map flag arms the trigger.
        self.assertIn("row.kind != BossTacticKind::TankPost && row.HasSpot()", POLICY)
        self.assertIn("if (row.kind == BossTacticKind::TankPost)\n                tankPostMaps_[row.map] = true;", POLICY)
        self.assertIn("bool HasTankPostMap(std::uint32_t map) const", POLICY)
        self.assertIn("a tank_post row", MGR)

    def test_fight_scan_drops_the_rows_without_the_key(self):
        scan = body(ACTIONS, "BossTacticsFight ai::ScanBossTactics")
        self.assertIn("bool const tankPostOn = sPlayerbotAIConfig.dungeonRunTankPost;", scan)
        self.assertIn("if (tankPostRow && !tankPostOn)\n                continue;", scan)
        # The band is the kind's own arming gate, not the aura-tactics gate.
        self.assertIn("RowGateOpen(row, tankPostRow ? true : keyOn, bossHpPct,", scan)

    def test_find_waits_for_the_slots_tank_only(self):
        find = body(ACTIONS, "BossTankPost ai::FindBossTankPost")
        self.assertIn("sPlayerbotAIConfig.dungeonRunTankPost", find)
        self.assertIn("GetBossTactics().HasTankPostMap(bot->GetMapId())", find)
        self.assertIn("candidate->priority != roles.botIndex", find)
        # The awaited unit comes from the throttled grid-search value, not the attackers list.
        self.assertIn('AI_VALUE2(std::vector<TankPostUnit>, "tank post units", static_cast<int32>(row->targetEntry))',
                      find)
        self.assertIn("PickTankPostUnit(candidates, row->spotX, row->spotY, row->spotZ)", find)
        blockers = (ROOT / "ai/playerbot/strategy/values/HoldKillBlockersValue.cpp").read_text()
        self.assertIn("std::vector<TankPostUnit> TankPostUnitsValue::Calculate()", blockers)
        self.assertIn("ScanEntry(bot, QualifierEntry(qualifier), found, kTankPostSearchYd)", blockers)
        self.assertIn("unit.selectable = !creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);", blockers)
        self.assertIn("kTankPostSearchYd = 200.0f", POLICY)
        # Arrival reads selectable, and the step skips the target only when it is the row's boss.
        self.assertIn("UNIT_FLAG_NOT_SELECTABLE", blockers)
        self.assertIn("in.subjectSelectable = subjectUnit && subjectUnit->selectable;", find)
        self.assertIn("TankPostHoldsOtherHostile(bot, currentTarget, row->bossEntry)", find)
        self.assertIn("holdSpot = sPlayerbotAIConfig.dungeonRunTankPostHoldSpot && row->mode == TacticMode::HoldSpot",
                      find)
        self.assertIn("in.subjectGuid = subjectUnit ? subjectUnit->guid : 0", find)
        self.assertIn("TankPostLatchCounts(record.arrivedLogged, record.arrivedSubjectGuid, in.subjectGuid)", find)
        self.assertIn("stored.arrivedSubjectGuid = in.subjectGuid;", find)
        self.assertIn("in.subjectOnTank = subject && subject->GetVictim() == bot", find)
        self.assertIn("in.seated = holdSpot && FindBossStand(ai, bot).valid", find)
        self.assertIn("if (holdSpot && currentTarget == subject)\n        in.holdingOtherHostile = false;", find)
        self.assertIn("(!holdSpot && in.holdingOtherHostile)", find)
        # The step's arrived decision reaches the trigger and the action: a hold_spot post does not stand down for
        # another hostile once the subject has arrived (Engage/Drag).
        self.assertIn("holdSpot && (step == TankPostDecision::Engage || step == TankPostDecision::Drag)", find)
        self.assertIn("out.holdingOtherHostile = in.holdingOtherHostile && !subjectOutranks;", find)
        self.assertIn("if (step == TankPostDecision::Yield)\n        return out;", find)
        self.assertIn("TankPostStep(in)", find)
        # The arrival latches on the physical condition, whatever step it chose (a first Drag or Yield included).
        self.assertIn("TankPostLatchesArrival(in, step)", find)
        self.assertIn("TankPostTankCanAct(bot)", find)
        self.assertIn('LogTankPost', find)

    def test_action_attacks_on_arrival_and_drops_only_the_boss(self):
        action = body(ACTIONS, "bool BossTacticsTankPostAction::Execute(Event& event)")
        self.assertIn("FindBossTankPost(ai, bot, ScanBossTactics(ai, bot))", action)
        self.assertIn("if (task.holdingOtherHostile)", action)
        self.assertIn("return Attack(bot, task.subject);", action)
        self.assertIn("current->GetEntry() != task.row->bossEntry", action)
        self.assertIn("bot->AttackStop();", action)
        self.assertIn("UpdateGroundPositionZ(task.x, task.y, z)", action)
        # The drag walks back to the spot keeping the target: no target change and no attack stop on that path.
        drag = action[action.index("if (task.step == TankPostDecision::Drag)"):]
        drag = drag[:drag.index("// Walk or hold")]
        self.assertIn("MoveTo(bot->GetMapId(), task.x, task.y, z, false, false, false, true)", drag)
        self.assertNotIn("AttackStop", drag)
        # One row per post event, named as the spec has it.
        self.assertIn('sPlayerbotAIConfig.logEvent(ai, "BossTacticsTankPost", info1, info2)', ACTIONS)
        self.assertIn('snprintf(info2, sizeof(info2), "event=%s,reason=%s", event, reason)', ACTIONS)

    def test_trigger_and_action_are_registered(self):
        self.assertIn('creators["boss tank post"] = [](PlayerbotAI* ai) '
                      '{ return new BossTacticsTankPostTrigger(ai); };', TRIGGER_CONTEXT)
        self.assertIn('creators["boss tactics tank post"] = [](PlayerbotAI* ai) '
                      '{ return new BossTacticsTankPostAction(ai); };', ACTION_CONTEXT)
        self.assertIn('"boss tank post"', STRATEGY)
        self.assertIn('new NextAction("boss tactics tank post", ACTION_EMERGENCY + 2)', STRATEGY)
        trigger = body(TRIGGERS, "bool BossTacticsTankPostTrigger::IsActive()")
        self.assertIn("sPlayerbotAIConfig.dungeonRunTankPost", trigger)
        self.assertIn("HasTankPostMap(bot->GetMapId())", trigger)
        self.assertIn("PlayerbotAI::IsTank(bot)", trigger)
        self.assertIn("return post.valid && !post.holdingOtherHostile;", trigger)
        self.assertIn("BossTacticsTankPostAction(PlayerbotAI* ai) : AttackAction(ai, \"boss tactics tank post\")",
                      ACTIONS_H)

    def test_config_key_is_read_templated_and_documented(self):
        self.assertIn('dungeonRunTankPost = config.GetBoolDefault("AiPlayerbot.DungeonRunTankPost", false);',
                      CONFIG_CPP)
        self.assertIn("bool dungeonRunTankPost = false;", CONFIG_H)
        self.assertRegex(TEMPLATE, r"(?m)^AiPlayerbot\.DungeonRunTankPost = 0$")
        self.assertIn("AiPlayerbot.DungeonRunTankPost", DOCS)
        self.assertIn("recommended on", DOCS[DOCS.index("### `AiPlayerbot.DungeonRunTankPost`"):][:400])
        self.assertIn("`tank_post`", BOSS_DOCS)
        self.assertIn("DungeonRunTankPost", BOSS_DOCS)
        self.assertIn("DungeonRunTankPost", CHANGELOG)
        # The hold_spot key: read, defaulted, templated, documented and named beside the mode in the boss docs.
        self.assertIn('dungeonRunTankPostHoldSpot = config.GetBoolDefault('
                      '"AiPlayerbot.DungeonRunTankPostHoldSpot", false);', CONFIG_CPP)
        self.assertIn("bool dungeonRunTankPostHoldSpot = false;", CONFIG_H)
        self.assertRegex(TEMPLATE, r"(?m)^AiPlayerbot\.DungeonRunTankPostHoldSpot = 0$")
        self.assertIn("AiPlayerbot.DungeonRunTankPostHoldSpot", DOCS)
        self.assertIn("recommended on",
                      DOCS[DOCS.index("### `AiPlayerbot.DungeonRunTankPostHoldSpot`"):][:600])
        self.assertIn("`hold_spot`", BOSS_DOCS)
        self.assertIn("DungeonRunTankPostHoldSpot", BOSS_DOCS)
        self.assertIn("DungeonRunTankPostHoldSpot", CHANGELOG)


if __name__ == "__main__":
    unittest.main()
