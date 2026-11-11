"""Wiring pins for the add_crew boss-tactic kind (Onyxia walk 2e).

The decision is pure (ai/playerbot/BossTacticsPolicy.h: AddCrewStateFor, AddCrewMember, AddCrewRank, the loader and the
table) and tested in tools/test_boss_tactics_policy.cpp; the Onyxia glue (OnyxiaWhelpDutyFor, ShouldReturnToOnyxia) is
in tools/test_onyxia_whelp_egg_policy.cpp and the row shape in tools/test_boss_tactics_sql.py. These pins prove the live
code calls the rules: the value reads the key first, the switch unit through the tank-post grid search, the raid's AoE
ranged bots through their own AIs; the whelp duty and the return to the boss read the value only under the key; the
trigger, the action and the value are registered; and the key is read, templated, documented without "recommended on",
and shipped with its row.

Run: python3 -m unittest tools/test_add_crew_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
POLICY = (ROOT / "ai/playerbot/BossTacticsPolicy.h").read_text()
VALUES_H = (ROOT / "ai/playerbot/strategy/values/HoldKillBlockersValue.h").read_text()
VALUES_CPP = (ROOT / "ai/playerbot/strategy/values/HoldKillBlockersValue.cpp").read_text()
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/OnyxiasLairDungeonActions.cpp").read_text()
ACTIONS_H = (ROOT / "ai/playerbot/strategy/actions/OnyxiasLairDungeonActions.h").read_text()
TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/OnyxiasLairDungeonTriggers.cpp").read_text()
TRIGGERS_H = (ROOT / "ai/playerbot/strategy/triggers/OnyxiasLairDungeonTriggers.h").read_text()
STRATEGY = (ROOT / "ai/playerbot/strategy/generic/OnyxiasLairDungeonStrategies.cpp").read_text()
ACTION_CONTEXT = (ROOT / "ai/playerbot/strategy/actions/ActionContext.h").read_text()
TRIGGER_CONTEXT = (ROOT / "ai/playerbot/strategy/triggers/TriggerContext.h").read_text()
VALUE_CONTEXT = (ROOT / "ai/playerbot/strategy/values/ValueContext.h").read_text()
WHELP_POLICY = (ROOT / "ai/playerbot/policy/raid/OnyxiaWhelpEggPolicy.h").read_text()
CONFIG_CPP = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
DOCS = (ROOT / "docs/server/settings/dungeons-raids.md").read_text()
BOSS_DOCS = (ROOT / "docs/play/boss-tactics.md").read_text()
CHANGELOG = (ROOT / "CHANGELOG.md").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class AddCrewWiring(unittest.TestCase):
    def test_kind_is_parsed_and_validated(self):
        self.assertIn('if (text == "add_crew")\n            return BossTacticKind::AddCrew;', POLICY)
        self.assertIn("AddCrew = 28,", POLICY)
        loader = body(POLICY, "            // add_crew: the switch unit whose death")
        for needle in ("row.targetEntry == 0", "row.targetEntry == row.bossEntry", "row.spellId != 0",
                       "row.priority > 5", "row.param != 0.0f", "RowGateSet(row)",
                       "row.mode != TacticMode::Default"):
            self.assertIn(needle, loader)
        # Read straight from the table: a gate on it could never be answered, so the loader refuses one.
        self.assertIn("row.kind == BossTacticKind::TankIgnore ||\n                    row.kind == "
                      "BossTacticKind::AddCrew))", POLICY)

    def test_table_keeps_the_switch_unit_and_the_crew_size(self):
        self.assertIn("struct AddCrewRow", POLICY)
        self.assertIn("std::unordered_map<std::uint64_t, AddCrewRow> addCrew_;", POLICY)
        self.assertIn("addCrew_[Key(row.map, row.bossEntry)] = AddCrewRow{row.targetEntry, row.priority};", POLICY)
        accessor = body(POLICY, "        bool AddCrewOf(std::uint32_t map, std::uint32_t bossEntry,")
        self.assertIn("if (!sealed_.load(std::memory_order_acquire) || addCrew_.empty())", accessor)
        self.assertIn("auto const it = addCrew_.find(Key(map, bossEntry));", accessor)
        self.assertIn("switchEntry = it->second.switchEntry;", accessor)
        self.assertIn("crewSize = it->second.crewSize;", accessor)
        self.assertIn("return true;", accessor)

    def test_value_reads_the_key_and_the_tank_post_search(self):
        self.assertIn("class AddCrewValue : public CalculatedValue<std::uint8_t>, public Qualified", VALUES_H)
        self.assertIn('CalculatedValue<uint8>(ai, "add crew", 2)', VALUES_H)
        value = body(VALUES_CPP, "uint8 AddCrewValue::Calculate()")
        # The key first: with it off no table or grid read runs.
        self.assertLess(value.index("if (!sPlayerbotAIConfig.dungeonRunAddCrew)"),
                        value.index("AddCrewOf("))
        self.assertIn("if (!GetBossTactics().AddCrewOf(bot->GetMapId(), QualifierEntry(qualifier), switchEntry, "
                      "crewSize))", value)
        # The switch unit is the tank-post grid search: alive or dead, an empty list counts as dead.
        self.assertIn('AI_VALUE2(std::vector<TankPostUnit>, "tank post units", static_cast<int32>(switchEntry))',
                      value)
        self.assertIn("if (unit.alive)", value)
        # The raid's AoE ranged bots: alive group members on this map and instance, not tanks or healers, ranged by
        # their own AI and in the crew's class order.
        self.assertIn("for (Player* member : LiveGroupMembers(bot->GetGroup()))", value)
        self.assertIn("member->GetMapId() != bot->GetMapId()", value)
        self.assertIn("member->GetInstanceId() != bot->GetInstanceId()", value)
        self.assertIn("PlayerbotAI::IsTank(member) || PlayerbotAI::IsHeal(member)", value)
        self.assertIn("PlayerbotAI* const memberAi = GET_PLAYERBOT_AI(member);", value)
        self.assertIn("!memberAi->IsRanged(member)", value)
        self.assertIn("AddCrewClassOrder(member->GetClass())", value)
        self.assertIn("AddCrewRank(candidates, bot->GetObjectGuid().GetRawValue())", value)
        self.assertIn("AddCrewMember(PlayerbotAI::IsTank(bot), PlayerbotAI::IsHeal(bot), ai->IsRanged(bot),", value)
        self.assertIn("AddCrewStateFor(true, true, switchAlive, member)", value)
        self.assertIn('creators["add crew"] = [](PlayerbotAI* ai) { return new AddCrewValue(ai); };', VALUE_CONTEXT)

    def test_whelp_duty_reads_the_value_only_under_the_key(self):
        duty = body(ACTIONS, "bool ai::IsOnyxiaWhelpDuty(PlayerbotAI* ai, Player* bot)")
        self.assertIn("bool const today = IsOnyxiaWhelpHunter(bot->GetClass(), PlayerbotAI::IsTank(bot), "
                      "PlayerbotAI::IsHeal(bot), ai->IsRanged(bot));", duty)
        self.assertLess(duty.index("if (!sPlayerbotAIConfig.dungeonRunAddCrew)"), duty.index('"add crew"'))
        self.assertIn("AiObjectContext* context = ai->GetAiObjectContext();", duty)
        self.assertIn('OnyxiaWhelpDutyFor(today, AI_VALUE2(uint8, "add crew", '
                      "static_cast<int32>(kOnyxiaEntry)))", duty)
        self.assertIn("inline bool OnyxiaWhelpDutyFor(bool todayDuty, std::uint8_t crewState)", WHELP_POLICY)
        self.assertIn("return crewState == 1 ? true : crewState == 2 ? false : todayDuty;", WHELP_POLICY)

    def test_other_ranged_return_to_the_boss(self):
        self.assertIn("inline bool ShouldReturnToOnyxia(bool keyOn, std::uint8_t crewState, bool rangedDps, "
                      "bool bossAirborne,", WHELP_POLICY)
        self.assertIn("return keyOn && crewState == 2 && rangedDps && bossAirborne && onWhelp && !explicitOrder && "
                      "bossValid;", WHELP_POLICY)
        trigger = body(TRIGGERS, "bool OnyxiaBossDutyTrigger::IsActive()")
        self.assertIn("if (!sPlayerbotAIConfig.dungeonRunAddCrew || bot->GetMapId() != kOnyxiaMapId)", trigger)
        self.assertIn('ai->HasStrategy("onyxia", BotState::BOT_STATE_COMBAT)', trigger)
        self.assertIn("ScanOnyxiaWhelps(ai, bot)", trigger)
        self.assertIn("scan.bossAirborne", trigger)
        self.assertIn("current->GetEntry() == kOnyxianWhelpEntry", trigger)
        self.assertIn('AI_VALUE(ObjectGuid, "explicit attack target")', trigger)
        self.assertIn("HoverReachTakerIsRangedDps(PlayerbotAI::IsTank(bot), PlayerbotAI::IsHeal(bot), "
                      "ai->IsRanged(bot))", trigger)
        self.assertIn('ShouldReturnToOnyxia(true, AI_VALUE2(uint8, "add crew", '
                      "static_cast<int32>(kOnyxiaEntry)),", trigger)
        self.assertIn("PossibleAttackTargetsValue::IsValid(boss, bot)", trigger)
        # The action takes the live boss among the attackers and only a valid target.
        action = body(ACTIONS, "bool OnyxiaAttackBossAction::Execute(Event& event)")
        self.assertIn("if (!sPlayerbotAIConfig.dungeonRunAddCrew || bot->GetMapId() != kOnyxiaMapId)", action)
        self.assertIn("attacker->GetEntry() == kOnyxiaEntry && sServerFacade.IsAlive(attacker)", action)
        self.assertIn("PossibleAttackTargetsValue::IsValid(boss, bot)", action)
        self.assertIn("return Attack(bot, boss);", action)
        self.assertIn('AttackAction(ai, "onyxia attack boss")', ACTIONS_H)
        self.assertIn('OnyxiaBossDutyTrigger(PlayerbotAI* ai, std::string name = "onyxia boss duty", '
                      "int checkInterval = 2)", TRIGGERS_H)

    def test_trigger_action_and_strategy_row(self):
        self.assertIn('creators["onyxia boss duty"] = [](PlayerbotAI* ai) '
                      '{ return new OnyxiaBossDutyTrigger(ai); };', TRIGGER_CONTEXT)
        self.assertIn('creators["onyxia attack boss"] = [](PlayerbotAI* ai) '
                      '{ return new OnyxiaAttackBossAction(ai); };', ACTION_CONTEXT)
        self.assertIn('"onyxia boss duty"', STRATEGY)
        self.assertIn('new NextAction("onyxia attack boss", ACTION_HIGH + 1)', STRATEGY)

    def test_key_is_read_templated_and_documented(self):
        self.assertIn('dungeonRunAddCrew = config.GetBoolDefault("AiPlayerbot.DungeonRunAddCrew", false);', CONFIG_CPP)
        self.assertIn("bool dungeonRunAddCrew = false;", CONFIG_H)
        self.assertRegex(TEMPLATE, r"(?m)^AiPlayerbot\.DungeonRunAddCrew = 0$")
        self.assertIn("AiPlayerbot.DungeonRunAddCrew", DOCS)
        section = DOCS[DOCS.index("### `AiPlayerbot.DungeonRunAddCrew`"):]
        section = section[:section.index("### `", 4)]
        # The brief's rule for this key: documented without "recommended on".
        self.assertNotIn("recommended on", section)
        self.assertIn("**Default:** `0`", section)
        block = TEMPLATE[TEMPLATE.index("# The add_crew boss tactic"):]
        block = block[:block.index("AiPlayerbot.DungeonRunAddCrew = 0")]
        self.assertNotIn("recommended on", block)
        self.assertIn("`add_crew`", BOSS_DOCS)
        self.assertIn("DungeonRunAddCrew", BOSS_DOCS)
        self.assertIn("DungeonRunAddCrew", CHANGELOG)

    def test_row_ships_with_its_migration(self):
        migration = (ROOT / "data/sql/world/20261111160000_world.sql").read_text()
        self.assertIn("DELETE FROM `ai_boss_tactic` WHERE `map` = 249 AND `boss_entry` = 10184 "
                      "AND `kind` = 'add_crew';", migration)
        self.assertIn("(249, 10184, 'add_crew', 50143, 0, 2, 0, '', 0, 100, 0, '', 0, 0, 0, 0, 0, 0,", migration)


if __name__ == "__main__":
    unittest.main()
