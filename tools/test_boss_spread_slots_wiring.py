"""Wiring pins for the fixed ranged spread slots (DungeonRunBossSpreadSlots).

The pure rules live in ai/playerbot/CombatSpreadPolicy.h and are tested in
tools/test_combat_spread_policy.cpp. These pins hold the call sites: the action
reads the key and the slot read does not, the old step-away names the gate that
stopped it with the key off, and the combat strategy queues the new action once.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
POLICY = (ROOT / "ai/playerbot/CombatSpreadPolicy.h").read_text()
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
DUNGEON_ACTIONS = (ROOT / "ai/playerbot/strategy/actions/DungeonActions.cpp").read_text()
TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/BossTacticsTriggers.cpp").read_text()
DUNGEON_TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/DungeonTriggers.cpp").read_text()
STRATEGY = (ROOT / "ai/playerbot/strategy/generic/BossTacticsStrategy.cpp").read_text()
TRIGGER_CONTEXT = (ROOT / "ai/playerbot/strategy/triggers/TriggerContext.h").read_text()
ACTION_CONTEXT = (ROOT / "ai/playerbot/strategy/actions/ActionContext.h").read_text()


def body(text, signature):
    start = text.index(signature)
    end = text.index("\n}\n", start)
    return text[start:end]


class SpreadSlotsWiring(unittest.TestCase):
    def test_the_action_reads_the_key_before_the_slot_read(self):
        fn = body(ACTIONS, "bool BossTacticsSpreadSlotAction::Execute(Event& event)")
        self.assertLess(fn.index("dungeonRunBossSpreadSlots"), fn.index("FindBossSpreadSlot("))

    def test_the_slot_read_decides_without_the_key(self):
        fn = body(ACTIONS, "BossSpreadSlotTask ReadBossSpreadSlot(PlayerbotAI* ai, Player* bot)")
        # The decision never branches on the key: the only mention is the row's ownership guard at the end.
        self.assertEqual(fn.count("dungeonRunBossSpreadSlots"), 1)
        self.assertIn("BossSpreadLive(", fn)
        self.assertIn(
            "    if (SpreadLogOwned(sPlayerbotAIConfig.dungeonRunBossSpreadSlots, SpreadLogCaller::Read))\n"
            "        LogBossSpread(ai, bot, SpreadSlotVerdictWord(task.verdict), task.slot, task.slotYd, task.fallback,\n"
            "                      SpreadCentreWord(task.centreKind));",
            fn)

    def test_the_read_row_is_guarded_by_the_key(self):
        read = body(ACTIONS, "BossSpreadSlotTask ReadBossSpreadSlot(PlayerbotAI* ai, Player* bot)")
        # With the key off the read writes nothing: the old step-away owns the row and its gate words.
        self.assertLess(read.index("task.verdict = SpreadSlotWanted("), read.index("SpreadLogOwned("))
        self.assertNotIn("LogBossSpread(ai, bot, SpreadSlotVerdictWord", read[:read.index("SpreadLogOwned(")])

    def test_the_read_is_cached_per_bot_and_tick(self):
        # One read per bot per AI update: the slot trigger, the old spread trigger and the action all ask, and the
        # read builds the ring points, maps the group and samples the floor.
        fn = body(ACTIONS, "BossSpreadSlotTask ai::FindBossSpreadSlot(PlayerbotAI* ai, Player* bot)")
        self.assertIn("t_spreadSlotTick", fn)
        self.assertIn("ai->UpdateTick()", fn)
        self.assertIn("ReadBossSpreadSlot(ai, bot)", fn)
        read = body(ACTIONS, "BossSpreadSlotTask ReadBossSpreadSlot(PlayerbotAI* ai, Player* bot)")
        self.assertNotIn("t_spreadSlotTick", read)
        for text, signature in ((TRIGGERS, "bool BossTacticsSpreadSlotTrigger::IsActive()"),
                                (DUNGEON_TRIGGERS, "bool RaidSpreadNeededTrigger::IsActive()"),
                                (ACTIONS, "bool BossTacticsSpreadSlotAction::Execute(Event& event)")):
            caller = body(text, signature)
            self.assertIn("FindBossSpreadSlot(ai, bot)", caller)
            self.assertNotIn("ReadBossSpreadSlot(", caller)

    def test_the_row_names_its_centre(self):
        fn = body(ACTIONS, "void ai::LogBossSpread(PlayerbotAI* ai, Player* bot, const char* verdict, int slot,")
        # The legacy writer derives the word from the anchor and the seek spot, so neither is `none`.
        self.assertIn("SpreadCentreWord(SpreadCentreFor(anchored, FindBossSeek(ai, bot).valid))", fn)
        self.assertIn("centre=%s", fn)
        # The read writer passes the centre it actually used.
        read = body(ACTIONS, "BossSpreadSlotTask ReadBossSpreadSlot(PlayerbotAI* ai, Player* bot)")
        self.assertIn("task.centreKind = SpreadCentreFor(anchored, hasSeekSpot);", read)
        self.assertIn("SpreadCentreWord(task.centreKind)", read)

    def test_the_legacy_row_is_guarded_by_not_the_key(self):
        trigger = body(DUNGEON_TRIGGERS, "bool RaidSpreadNeededTrigger::IsActive()")
        self.assertIn(
            "SpreadLogOwned(sPlayerbotAIConfig.dungeonRunBossSpreadSlots, SpreadLogCaller::Legacy)", trigger)
        self.assertEqual(trigger.count("LogBossSpread("), 4)
        # Each of the trigger's gate rows sits behind the key-off owner, never the raw key.
        self.assertEqual(trigger.count("if (logSpread)"), 4)
        self.assertNotIn("!sPlayerbotAIConfig.dungeonRunBossSpreadSlots", trigger)
        action = body(DUNGEON_ACTIONS, "bool RaidSpreadAction::Execute(Event& event)")
        self.assertIn(
            "spreadLive && SpreadLogOwned(sPlayerbotAIConfig.dungeonRunBossSpreadSlots, SpreadLogCaller::Legacy)",
            action)
        useful = body(DUNGEON_ACTIONS, "bool RaidSpreadAction::isUseful()")
        self.assertIn("SpreadLogOwned(sPlayerbotAIConfig.dungeonRunBossSpreadSlots, SpreadLogCaller::Legacy)", useful)

    def test_the_new_trigger_fires_on_the_three_moving_verdicts(self):
        fn = body(TRIGGERS, "bool BossTacticsSpreadSlotTrigger::IsActive()")
        self.assertIn("dungeonRunBossSpreadSlots", fn)
        for verdict in ("SpreadSlotVerdict::SlotMove", "SpreadSlotVerdict::SlotHold", "SpreadSlotVerdict::Cast"):
            self.assertIn(verdict, fn)
        # The key gate runs before the read: with the key off the read writes nothing and is not even taken.
        self.assertLess(fn.index("dungeonRunBossSpreadSlots"), fn.index("FindBossSpreadSlot("))

    def test_SpreadVerdict_EveryVerdictIsLogged(self):
        # Every verdict word the enum can produce reaches the one log call: it is the read's statement after the
        # verdict is assigned, behind the key-on ownership guard, and it takes SpreadSlotVerdictWord(task.verdict).
        read = body(ACTIONS, "BossSpreadSlotTask ReadBossSpreadSlot(PlayerbotAI* ai, Player* bot)")
        self.assertIn(
            "    if (SpreadLogOwned(sPlayerbotAIConfig.dungeonRunBossSpreadSlots, SpreadLogCaller::Read))\n"
            "        LogBossSpread(ai, bot, SpreadSlotVerdictWord(task.verdict), task.slot, task.slotYd, task.fallback,\n"
            "                      SpreadCentreWord(task.centreKind));\n"
            "    return task;",
            read)
        self.assertLess(read.index("task.verdict = SpreadSlotWanted("), read.index("LogBossSpread("))
        words = re.findall(r'case SpreadSlotVerdict::\w+: return "([a-z_]+)";', POLICY)
        self.assertEqual(len(words), 12)
        self.assertEqual(len(set(words)), 12)
        # The action logs no verdict of its own; only a move it could not send.
        action = body(ACTIONS, "bool BossTacticsSpreadSlotAction::Execute(Event& event)")
        self.assertNotIn("SpreadSlotVerdictWord", action)
        self.assertIn('LogBossSpread(ai, bot, "move_failed"', action)

    def test_the_read_reports_the_owned_slot_not_the_fallback(self):
        read = body(ACTIONS, "BossSpreadSlotTask ReadBossSpreadSlot(PlayerbotAI* ai, Player* bot)")
        self.assertIn("SpreadSlotSelect(owned, fallback, ownedUsable, fallbackUsable)", read)
        self.assertIn("task.slot = selection.slot;", read)
        self.assertIn("task.fallback = selection.fallback;", read)

    def test_flag_off_keeps_the_spread_live_stay_precedence(self):
        # With the slots key at 0 a live spread row still widens the spacing and outranks a stay: both the old trigger
        # and the step-away test the stay only while no spread row is live, and the stand check still answers false.
        trigger = body(DUNGEON_TRIGGERS, "bool RaidSpreadNeededTrigger::IsActive()")
        self.assertIn('!spreadLive && ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT)', trigger)
        self.assertIn("sPlayerbotAIConfig.dungeonRunBossSpreadSlots && spreadLive", trigger)
        action = body(DUNGEON_ACTIONS, "bool RaidSpreadAction::Execute(Event& event)")
        self.assertIn('!spreadLive && ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT)', action)
        useful = body(DUNGEON_ACTIONS, "bool RaidSpreadAction::isUseful()")
        self.assertLess(useful.index("FindBossStand(ai, bot).valid"), useful.index("return true;"))
        self.assertIn("if (FindBossStand(ai, bot).valid)", useful)
        self.assertNotIn("FindBossSpreadSlot(", useful)

    def test_the_old_trigger_names_the_gate_that_stopped_it(self):
        fn = body(DUNGEON_TRIGGERS, "bool RaidSpreadNeededTrigger::IsActive()")
        self.assertLess(fn.index("BossSpreadLive("), fn.index("BossSeeking("))
        for word in ("seek", "no_target", "order", "clear"):
            self.assertIn('LogBossSpread(ai, bot, "%s"' % word, fn)
        self.assertIn("FindBossSpreadSlot(", fn)

    def test_the_step_away_names_its_gates(self):
        fn = body(DUNGEON_ACTIONS, "bool RaidSpreadAction::Execute(Event& event)")
        for word in ("cooldown", "stepped", "anchor", "no_step"):
            self.assertIn('"%s"' % word, fn)
        useful = body(DUNGEON_ACTIONS, "bool RaidSpreadAction::isUseful()")
        self.assertIn('LogBossSpread(ai, bot, "stand"', useful)

    def test_the_combat_strategy_queues_the_action_once(self):
        nodes = re.findall(r'new TriggerNode\(\s*"boss tactics spread slot"', STRATEGY)
        self.assertEqual(len(nodes), 1)
        combat = STRATEGY[
            STRATEGY.index("void BossTacticsStrategy::InitCombatTriggers"):
            STRATEGY.index("void BossTacticsStrategy::InitNonCombatTriggers")
        ]
        self.assertIn('"boss tactics spread slot"', combat)
        self.assertLess(combat.index('"boss tactics stand"'), combat.index('"boss tactics spread slot"'))
        reaction = STRATEGY[
            STRATEGY.index("void BossTacticsStrategy::InitReactionTriggers"):
            STRATEGY.index("void BossTacticsStrategy::InitCombatMultipliers")
        ]
        self.assertNotIn('"boss tactics spread slot"', reaction)

    def test_both_contexts_register_the_name(self):
        self.assertIn('creators["boss tactics spread slot"]', TRIGGER_CONTEXT)
        self.assertIn('creators["boss tactics spread slot"]', ACTION_CONTEXT)


if __name__ == "__main__":
    unittest.main()
