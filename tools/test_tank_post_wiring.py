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
WARRIOR = (ROOT / "ai/playerbot/strategy/warrior/WarriorActions.h").read_text()
DRUID = (ROOT / "ai/playerbot/strategy/druid/DruidActions.h").read_text()


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
        self.assertIn("const BossStandTarget stand = holdSpot ? FindBossStand(ai, bot) : BossStandTarget();", find)
        self.assertIn("in.seated = TankPostSeated(stand.valid, stand.stay);", find)
        self.assertIn("if (holdSpot && currentTarget == subject)\n        in.holdingOtherHostile = false;", find)
        self.assertIn("(!holdSpot && in.holdingOtherHostile)", find)
        # The step's arrived decision reaches the trigger and the action: a hold_spot post does not stand down for
        # another hostile once the subject has arrived (Engage/Drag).
        self.assertIn("const bool subjectOutranks = holdSpot && (step == TankPostDecision::Engage || "
                      "step == TankPostDecision::Drag ||", find)
        self.assertIn("step == TankPostDecision::Leash);", find)
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
        # The drag walks back to the spot keeping the target: no target change and no attack stop on that path, and
        # the leash rides the same branch (a pre-arrival walk back with the tank's own target kept).
        drag = action[action.index("if (task.step == TankPostDecision::Drag || task.step == TankPostDecision::Leash)"):]
        drag = drag[:drag.index("// Walk or hold")]
        self.assertIn("MoveTo(bot->GetMapId(), task.x, task.y, z, false, false, false, true)", drag)
        self.assertNotIn("AttackStop", drag)
        # One row per post event, named as the spec has it.
        self.assertIn('sPlayerbotAIConfig.logEvent(ai, "BossTacticsTankPost", info1, info2)', ACTIONS)
        self.assertIn('snprintf(info2, sizeof(info2), "event=%s,reason=%s", event, reason)', ACTIONS)

    def test_leash_key_is_read_templated_and_documented(self):
        # The key: read with the row's mode, defaulted off.
        self.assertIn('dungeonRunTankPostLeash = config.GetBoolDefault("AiPlayerbot.DungeonRunTankPostLeash", false);',
                      CONFIG_CPP)
        self.assertIn("bool dungeonRunTankPostLeash = false;", CONFIG_H)
        self.assertRegex(TEMPLATE, r"(?m)^AiPlayerbot\.DungeonRunTankPostLeash = 0$")
        self.assertIn("AiPlayerbot.DungeonRunTankPostLeash", DOCS)
        self.assertIn("recommended on",
                      DOCS[DOCS.index("### `AiPlayerbot.DungeonRunTankPostLeash`"):][:600])
        self.assertIn("AiPlayerbot.DungeonRunTankPostLeash", BOSS_DOCS)
        self.assertIn("DungeonRunTankPostLeash", CHANGELOG)
        # It leashes a `hold_spot` post only: the flag is set from the key and the mode together.
        find = body(ACTIONS, "BossTankPost ai::FindBossTankPost")
        self.assertIn("in.leash = holdSpot && sPlayerbotAIConfig.dungeonRunTankPostLeash;", find)
        # The leash rides the drag's branch, so the walk back keeps the target and never stops the attack.
        action = body(ACTIONS, "bool BossTacticsTankPostAction::Execute(Event& event)")
        self.assertIn("if (task.step == TankPostDecision::Drag || task.step == TankPostDecision::Leash)", action)
        # Leash outranks the whelp like the arrived subject: the trigger fires instead of standing down for it.
        self.assertIn("step == TankPostDecision::Leash", find)
        self.assertIn("if (step == TankPostDecision::Yield)\n        return out;", find)

    def test_step_rows_are_logged_once_and_entered_under_the_lock(self):
        find = body(ACTIONS, "BossTankPost ai::FindBossTankPost")
        # The last step is stored with the record, inside the same lock that writes it, and the row is written when the
        # step is entered only; it comes after the start/arrive rows and before the seat stands the post down.
        store = find[find.index("bool arrivedNow = false;"):]
        store = store[:store.index("if (startedNow)")]
        self.assertIn("std::lock_guard<std::mutex> lock(g_tankPostMutex);", store)
        self.assertIn("TankPostRecord& stored = g_tankPost[self];", store)
        self.assertIn("stepEntered = TankPostStepEntered(stored.lastStep, step);", store)
        self.assertIn("stored.lastStep = static_cast<std::uint8_t>(step);", store)
        logged = find.index("LogTankPostStep(ai, *row, step, in.botToSpotYd, in.subjectToSpotYd, in.subjectOnTank);")
        self.assertGreater(logged, find.index('LogTankPost(ai, *row, "arrive", nullptr);'))
        self.assertLess(logged, find.index("if (step == TankPostDecision::Yield)"))
        self.assertIn("std::uint8_t lastStep = 0;", ACTIONS)
        # The row shape: the three step words, both distances and the on-tank flag on the post's own event name.
        step_log = body(ACTIONS, "void LogTankPostStep(")
        self.assertIn('step == TankPostDecision::Drag ? "drag"', step_log)
        self.assertIn('step == TankPostDecision::Yield            ? "yield"', step_log)
        self.assertIn('"leash"', step_log)
        self.assertIn('"event=%s,tank_yd=%.1f,subject_yd=%.1f,on_tank=%d"', step_log)
        self.assertIn('sPlayerbotAIConfig.logEvent(ai, "BossTacticsTankPost", info1, info2)', step_log)
        self.assertIn('sPlayerbotAIConfig.hasLog("bot_events.csv")', step_log)

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

    def test_a_live_post_reserves_its_subject(self):
        # The rule is pure (TankPostSubjectReserved, TankPostReserves, TankPostEndWord) and tested in the policy test;
        # the glue reads the shared record. A record read in the last 3 s, on this map and the target's entry, by a tank
        # in the bot's instance that can act, within 30 yd of the subject, keeps every other tank off the unit until the
        # subject has been loose off that tank past the guard's grace.
        reserve = body(ACTIONS, "bool ai::TankPostReservesSubject(PlayerbotAI* ai, Player* bot, Unit* target)")
        self.assertIn("sPlayerbotAIConfig.dungeonRunTankPostOwnsSubject", reserve)
        self.assertIn("!target->IsCreature()", reserve)
        self.assertIn("PlayerbotAI::IsTank(bot)", reserve)
        self.assertIn("entry.second.map != bot->GetMapId()", reserve)
        self.assertIn("entry.second.targetEntry != target->GetEntry()", reserve)
        self.assertIn("nowMs - entry.second.lastReadMs >= kTankPostLiveMs", reserve)
        self.assertIn("entry.first == self", reserve)
        # Only the keys leave the lock; the player lookup and the instance compare happen after it.
        lock = reserve.index("std::lock_guard<std::mutex> lock(g_tankPostMutex);")
        self.assertLess(lock, reserve.index("sObjectMgr.GetPlayer("))
        self.assertIn("tank->GetInstanceId() != bot->GetInstanceId()", reserve)
        self.assertIn("TankPostTankCanAct(tank)", reserve)
        # The distance is the post tank's to the subject (3D), and the loose clock is the record's, read under its lock.
        self.assertIn("ownerYd = TankPostDistanceYd(tank->GetPositionX()", reserve)
        self.assertIn("target->GetPositionZ())", reserve)
        self.assertIn("target->GetVictim() == tank", reserve)
        self.assertIn("ownerYd <= kTankPostReserveRangeYd, onOwner, looseMs)", reserve)
        # The clock starts on the first selectable, attackable tick the subject is off the post tank, so a subject still
        # in the air does not spend its grace; a running clock keeps its start through short unhittable blips and only
        # a lift-off of the grace length clears it (the unhittable spell is tracked in the record).
        self.assertIn("struct TankPostLooseClockState", POLICY)
        self.assertIn("inline TankPostLooseClockState TankPostLooseClock(std::uint32_t prior, "
                      "std::uint32_t unhittableSince,", POLICY)
        self.assertIn("out.looseSinceMs = prior != 0 ? prior : nowMs;", POLICY)
        self.assertIn("out.unhittableSinceMs = unhittableSince != 0 ? unhittableSince : nowMs;", POLICY)
        self.assertIn("out.looseSinceMs = nowMs - out.unhittableSinceMs >= kGuardAssignedGraceMs ? 0 : prior;",
                      POLICY)
        self.assertIn("TankPostLooseClockState const loose = TankPostLooseClock(it->second.looseSinceMs,", reserve)
        self.assertIn("it->second.unhittableSinceMs, onOwner,", reserve)
        self.assertIn("!target->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE), "
                      "tank->IsValidAttackTarget(target),", reserve)
        self.assertIn("it->second.looseSinceMs = loose.looseSinceMs;", reserve)
        self.assertIn("it->second.unhittableSinceMs = loose.unhittableSinceMs;", reserve)
        self.assertIn("looseMs = loose.looseSinceMs == 0 ? 0 : nowMs - loose.looseSinceMs;", reserve)
        # The action's store keeps the clock current through the same rule: on its tank clears both fields, and a
        # subject in the air neither starts the clock nor loses the start it had.
        self.assertIn("TankPostLooseClockState const loose = TankPostLooseClock(priorLoose, priorUnhittable, "
                      "in.subjectOnTank,", ACTIONS)
        self.assertIn("subject != nullptr && bot->IsValidAttackTarget(subject), nowMs);", ACTIONS)
        self.assertIn("stored.looseSinceMs = loose.looseSinceMs;", ACTIONS)
        self.assertIn("stored.unhittableSinceMs = loose.unhittableSinceMs;", ACTIONS)
        self.assertIn("std::uint32_t looseSinceMs = 0;", ACTIONS)
        self.assertIn("std::uint32_t unhittableSinceMs = 0;", ACTIONS)
        # The gate refuses before it reads its own key: the reservation does not need DungeonRunTauntGate.
        gate = body(ACTIONS, "bool ai::TauntRefusedByGate(")
        self.assertLess(gate.index("TankPostReservesSubject(ai, bot, target)"),
                        gate.index("sPlayerbotAIConfig.dungeonRunTauntGate"))
        self.assertIn("LogTauntGateRefusal(ai, bot, target, TauntGate::PostSubject, spell);", gate)
        gate_log = body(ACTIONS, "static void LogTauntGateRefusal(")
        self.assertIn('"reason=%s", TauntGateWord(gate)', gate_log)
        self.assertIn('sPlayerbotAIConfig.logEvent(ai, "BossTacticsTauntGate", info1, info2);', gate_log)

    def test_area_taunts_scan_their_radius(self):
        # The two area taunts read the gate on their current target and then scan the whole radius: the shout/roar
        # taunts every hostile in it, so another post's subject anywhere inside refuses the cast.
        area = body(ACTIONS, "bool ai::AreaTauntRefusedByPost(PlayerbotAI* ai, Player* bot, const char* spell)")
        self.assertIn("MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck check(bot, bot, kAreaTauntRadiusYd)", area)
        self.assertIn("Cell::VisitAllObjects(bot, searcher, kAreaTauntRadiusYd)", area)
        self.assertIn("fact.reservedByOther = TankPostReservesSubject(ai, bot, unit)", area)
        self.assertIn("AreaTauntHitsReservedSubject(units, kAreaTauntRadiusYd)", area)
        self.assertIn("LogTauntGateRefusal(ai, bot, reserved, TauntGate::PostSubject, spell)", area)
        for text, cls, spell in (
                (WARRIOR, "class CastChallengingShoutAction", '"challenging shout"'),
                (DRUID, "class CastChallengingRoarAction", '"challenging roar"')):
            taunt = body(text, cls, "};")
            self.assertIn("CastMeleeSpellAction(ai, " + spell + ")", taunt)
            self.assertIn("virtual bool isUseful() override", taunt)
            self.assertIn("CastMeleeSpellAction::isUseful()", taunt)
            self.assertIn(f"!(GetTarget() && TauntRefusedByGate(ai, bot, GetTarget(), {spell}))", taunt)
            self.assertIn(f"!AreaTauntRefusedByPost(ai, bot, {spell})", taunt)

    def test_slot_end_word(self):
        find = body(ACTIONS, "BossTankPost ai::FindBossTankPost")
        # A renumbering: no row for this bot's index, but its record's row stands at another slot.
        self.assertIn("candidate->priority != roles.botIndex", find)
        self.assertIn("candidate->bossEntry == record.bossEntry", find)
        self.assertIn("candidate->targetEntry == record.targetEntry", find)
        self.assertIn('(row || slotMoved) ? "slot" : "band"', find)
        self.assertIn("in.slotMoved = slotMoved;", find)
        end = body(ACTIONS, "const char* TankPostEndReason(")
        self.assertIn("TankPostEndWord(in.live, in.slotMoved, in.tankCanAct, in.windowExpired, in.subjectFound,", end)

    def test_owns_subject_key_is_read_templated_and_documented(self):
        self.assertIn('dungeonRunTankPostOwnsSubject =\n        config.GetBoolDefault('
                      '"AiPlayerbot.DungeonRunTankPostOwnsSubject", false);', CONFIG_CPP)
        self.assertIn("bool dungeonRunTankPostOwnsSubject = false;", CONFIG_H)
        self.assertRegex(TEMPLATE, r"(?m)^AiPlayerbot\.DungeonRunTankPostOwnsSubject = 0$")
        self.assertIn("AiPlayerbot.DungeonRunTankPostOwnsSubject", DOCS)
        self.assertIn("recommended on",
                      DOCS[DOCS.index("### `AiPlayerbot.DungeonRunTankPostOwnsSubject`"):][:400])
        self.assertIn("DungeonRunTankPostOwnsSubject", BOSS_DOCS)
        self.assertIn("DungeonRunTankPostOwnsSubject", CHANGELOG)
        # The gate word ships with the key it comes from.
        self.assertIn('case TauntGate::PostSubject: return "post_subject";', POLICY)
        self.assertIn("constexpr std::size_t kTauntGateWords = 5;", POLICY)


if __name__ == "__main__":
    unittest.main()
