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
TANK_TARGET = (ROOT / "ai/playerbot/strategy/values/TankTargetValue.cpp").read_text()
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
                       "row.param < 5.0f", "row.param > 100.0f", "row.windowSec < 10.0f", "row.windowSec > 120.0f",
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
        self.assertIn("PickTankPostUnit(candidates, row->spotX, row->spotY, row->spotZ,", find)
        self.assertIn("sPlayerbotAIConfig.dungeonRunTankPostOwnsUnit, ownLatched, otherLatched);", find)
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
        self.assertIn("const BossStandTarget stand = (holdSpot || sPlayerbotAIConfig.dungeonRunTankPostWalks)", find)
        self.assertIn("? FindBossStand(ai, bot) : BossStandTarget();", find)
        self.assertIn("in.seated = TankPostSeated(stand.valid, stand.stay);", find)
        self.assertIn("out.standActive = stand.valid;", find)
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
        # The rule is pure (TankPostSubjectReserved, TankPostReserves, TankPostLooseClock, TankPostLapseOf,
        # TankPostEndWord) and tested in the policy test; the glue reads the shared record. A record read in the last
        # 3 s, on this map and the target's entry, by a tank in the bot's instance that can act, within 60 yd of the
        # subject, keeps every other tank off the unit until the subject has been loose off that tank past the 15 s
        # post grace.
        reserve = body(ACTIONS, "bool ai::TankPostReservesSubject(PlayerbotAI* ai, Player* bot, Unit* target)")
        self.assertIn("sPlayerbotAIConfig.dungeonRunTankPostOwnsSubject", reserve)
        # With the key off the read returns before any record lookup, so it writes neither a lapse row nor a slot word.
        self.assertLess(reserve.index("if (!sPlayerbotAIConfig.dungeonRunTankPostOwnsSubject)"),
                        reserve.index("g_tankPost"))
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
        self.assertIn("bool const rangeOk = ownerYd <= kTankPostReserveRangeYd;", reserve)
        self.assertIn("TankPostReserves(sPlayerbotAIConfig.dungeonRunTankPostOwnsSubject, true, true, false, rangeOk, "
                      "onOwner,", reserve)
        # The clock starts on the first selectable, attackable tick the subject is off the post tank, so a subject still
        # in the air does not spend its grace; a running clock keeps its start through short unhittable blips and only
        # a lift-off of the grace length clears it (the unhittable spell is tracked in the record).
        self.assertIn("constexpr float kTankPostReserveRangeYd = 60.0f;", POLICY)
        self.assertIn("constexpr std::uint32_t kTankPostReserveGraceMs = 15000;", POLICY)
        self.assertIn("enum class TankPostLapse : std::uint8_t", POLICY)
        self.assertIn("inline TankPostLapse TankPostLapseOf(bool keyOn, bool ownerWithinRange, "
                      "bool subjectOnOwner,", POLICY)
        self.assertIn("inline bool TankPostLapseDue(TankPostLapse lapse, bool& lapseLogged)", POLICY)
        self.assertIn("struct TankPostLooseClockState", POLICY)
        self.assertIn("inline TankPostLooseClockState TankPostLooseClock(std::uint32_t prior, "
                      "std::uint32_t unhittableSince,", POLICY)
        self.assertIn("out.looseSinceMs = prior != 0 ? prior : nowMs;", POLICY)
        self.assertIn("out.unhittableSinceMs = unhittableSince != 0 ? unhittableSince : nowMs;", POLICY)
        self.assertIn("out.looseSinceMs = nowMs - out.unhittableSinceMs >= kTankPostReserveGraceMs ? 0 : prior;",
                      POLICY)
        self.assertIn("TankPostLooseClockState const loose = TankPostLooseClock(it->second.looseSinceMs,", reserve)
        self.assertIn("it->second.unhittableSinceMs, onOwner,", reserve)
        self.assertIn("!target->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE), "
                      "tank->IsValidAttackTarget(target),", reserve)
        self.assertIn("it->second.looseSinceMs = loose.looseSinceMs;", reserve)
        self.assertIn("it->second.unhittableSinceMs = loose.unhittableSinceMs;", reserve)
        self.assertIn("looseMs = loose.looseSinceMs == 0 ? 0 : nowMs - loose.looseSinceMs;", reserve)
        # The clock is advanced on every read, owner or not: a subject back on its tank clears the persisted start
        # before the post's own update runs, so a later loss starts a fresh grace (the round-4 ordering case).
        self.assertNotIn("if (!onOwner)", reserve)
        self.assertLess(reserve.index("TankPostLooseClock(it->second.looseSinceMs,"),
                        reserve.index("tank->IsValidAttackTarget(target),"))
        # A lapsed reservation writes one row per lapse event, throttled in the record and re-armed when it holds.
        self.assertIn("lapse = TankPostLapseOf(sPlayerbotAIConfig.dungeonRunTankPostOwnsSubject, rangeOk, onOwner, "
                      "looseMs);", reserve)
        self.assertIn("logLapse = TankPostLapseDue(lapse, it->second.lapseLogged);", reserve)
        self.assertIn("if (logLapse)\n            LogTankPostLapse(ai, bossEntry, targetEntry, priority, ownerYd, "
                      "looseMs, lapse);", reserve)
        # The action's store keeps the clock current through the same rule: on its tank clears both fields, and a
        # subject in the air neither starts the clock nor loses the start it had.
        self.assertIn("TankPostLooseClockState const loose = TankPostLooseClock(priorLoose, priorUnhittable, "
                      "in.subjectOnTank,", ACTIONS)
        self.assertIn("subject != nullptr && bot->IsValidAttackTarget(subject), nowMs);", ACTIONS)
        self.assertIn("stored.looseSinceMs = loose.looseSinceMs;", ACTIONS)
        self.assertIn("stored.unhittableSinceMs = loose.unhittableSinceMs;", ACTIONS)
        self.assertIn("std::uint32_t looseSinceMs = 0;", ACTIONS)
        self.assertIn("std::uint32_t unhittableSinceMs = 0;", ACTIONS)
        self.assertIn("bool lapseLogged = false;", ACTIONS)
        self.assertIn("std::uint8_t priority = 0;", ACTIONS)
        # The post's own store re-arms the throttle as soon as the reservation holds again, and keeps it while the
        # same lapse lasts: a recovery no other tank read must not swallow the next lapse row.
        self.assertIn("inline bool TankPostLapseOwnerUpdate(bool keyOn, bool ownerWithinRange, bool subjectOnOwner,",
                      POLICY)
        self.assertIn("std::uint32_t looseMs, bool& lapseLogged)", POLICY)
        self.assertIn("bool const ownerWithinRange = sPlayerbotAIConfig.dungeonRunTankPostOwnsSubject && subject &&",
                      ACTIONS)
        self.assertIn("subject->GetPositionY(), subject->GetPositionZ()) <= kTankPostReserveRangeYd;", ACTIONS)
        self.assertIn("bool lapseLogged = stored.lapseLogged;", ACTIONS)
        self.assertIn("TankPostLapseOwnerUpdate(sPlayerbotAIConfig.dungeonRunTankPostOwnsSubject, ownerWithinRange, "
                      "in.subjectOnTank,", ACTIONS)
        self.assertIn("loose.looseSinceMs == 0 ? 0 : nowMs - loose.looseSinceMs, lapseLogged);", ACTIONS)
        self.assertIn("stored.lapseLogged = lapseLogged;", ACTIONS)
        self.assertNotIn("stored.lapseLogged = priorLapseLogged;", ACTIONS)
        # The lapse row names the post and its limits.
        lapse_log = body(ACTIONS, "void LogTankPostLapse(")
        self.assertIn('"event=lapse,reason=post_lapsed,owner_yd=%.1f,loose_ms=%u,limit=%s"', lapse_log)
        self.assertIn('lapse == TankPostLapse::Range ? "range" : "grace"', lapse_log)
        self.assertIn('sPlayerbotAIConfig.logEvent(ai, "BossTacticsTankPost", info1, info2)', lapse_log)
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
        self.assertIn("TankPostEndsAsSlot(row != nullptr, slotMoved, "
                      "sPlayerbotAIConfig.dungeonRunTankPostOwnsSubject)", find)
        self.assertIn("inline bool TankPostEndsAsSlot(bool rowStillHeld, bool slotMoved, bool ownsSubjectKeyOn)",
                      POLICY)
        # The record carries the post's slot for its rows, kept out of the row compare (a renumbering ends the post).
        self.assertIn("record.priority = row->priority;", ACTIONS)
        self.assertIn("closed.priority = record.priority;", ACTIONS)
        self.assertIn("endedRow.priority = dead.second.priority;", ACTIONS)
        self.assertIn("in.slotMoved = slotMoved;", find)
        end = body(ACTIONS, "const char* TankPostEndReason(")
        self.assertIn("TankPostEndWord(in.live, in.slotMoved, in.tankCanAct, in.windowExpired, in.subjectFound,", end)

    def test_take_subject_taunts_the_subject_back(self):
        # AiPlayerbot.DungeonRunTankPostTaunts: the Engage step taunts the awaited unit back off whoever holds it,
        # before the plain attack. The rule is pure (TankPostTauntStep, TankPostTauntWord) and tested with the tests'
        # mutation case in tools/test_boss_tactics_policy.cpp; these pins prove the live code uses it.
        action = body(ACTIONS, "bool BossTacticsTankPostAction::Execute(Event& event)")
        engage = action[action.index("if (task.step == TankPostDecision::Engage)"):]
        engage = engage[:engage.index("return Attack(bot, task.subject);")]
        self.assertIn("if (sPlayerbotAIConfig.dungeonRunTankPostTaunts && TakeSubject(task, current))\n"
                      "        {\n", engage)
        self.assertLess(engage.index("TakeSubject(task, current)"), engage.index("if (current == task.subject)"))
        take = body(ACTIONS, "bool BossTacticsTankPostAction::TakeSubject(const BossTankPost& task, Unit* current)")
        self.assertIn("TauntRefusedByGate(ai, bot, subject, taunt)", take)
        self.assertIn("AreaTauntRefusedByPost(ai, bot, area)", take)
        self.assertIn("TauntRefusedByGate(ai, bot, subject, area)", take)
        self.assertIn("KnownTaunt(ai)", take)
        self.assertIn('"challenging shout"', take)
        self.assertIn('"challenging roar"', take)
        self.assertIn('ai->HasAura("defensive stance", bot)', take)
        self.assertIn("PlayerbotAI::IsTank(static_cast<Player*>(victim))", take)
        self.assertIn("sPlayerbotAIConfig.dungeonRunTankPostOwnsSubject", take)
        self.assertIn("TankPostTauntStep(in)", take)
        # No gate row for a taunt that cannot go out: the attemptable verdict comes before every gate read, and the
        # area inputs only after the single taunt was found not usable.
        self.assertIn("if (!TankPostTauntAttemptable(in))\n        return false;", take)
        self.assertLess(take.index("TankPostTauntAttemptable(in)"),
                        take.index("TauntRefusedByGate(ai, bot, subject, taunt)"))
        area = take[take.index("if (step == TankPostTaunt::NotReady)"):]
        area = area[:area.index("\n    }")]
        self.assertIn("AreaTauntRefusedByPost(ai, bot, area)", area)
        self.assertIn("bot->GetDistance(subject) <= kAreaTauntRadiusYd", area)
        self.assertIn("step = TankPostTauntStep(in);", area)
        # The area gate scans the radius and advances other posts' clocks: read it only for a castable, in-range area
        # taunt off a non-tank, after the cheap inputs.
        self.assertIn("if (TankPostAreaTauntGateNeeded(in))", area)
        guard = area.index("if (TankPostAreaTauntGateNeeded(in))")
        self.assertLess(area.index("in.areaCastable"), guard)
        self.assertLess(area.index("in.areaInRange"), guard)
        self.assertLess(guard, area.index("step = TankPostTauntStep(in);"))
        self.assertIn("AreaTauntRefusedByPost(ai, bot, area)", area[guard:])
        self.assertIn("inline bool TankPostAreaTauntGateNeeded(const TankPostTauntIn& in)", POLICY)
        self.assertIn("return in.areaCastable && in.areaInRange && !in.victimIsTank;", POLICY)
        self.assertIn("if (current != subject)\n                Attack(bot, subject);", take)
        self.assertIn("cast = ai->CastSpell(taunt, subject);", take)
        self.assertIn('cast = ai->CastSpell("defensive stance", bot);', take)
        self.assertIn("cast = ai->CastSpell(area, bot);", take)
        self.assertIn("LogTankPostTaunt(ai, bot, *task.row, step, spell, TankPostVictimWord(victim), cast);", take)
        # The header declares it, and the pure step ships every word the log can name.
        self.assertIn("bool TakeSubject(const BossTankPost& task, Unit* current);", ACTIONS_H)
        for needle in ("enum class TankPostTaunt : std::uint8_t { None, Taunt, Stance, AreaTaunt, Gate, NotReady };",
                       "struct TankPostTauntIn", "inline TankPostTaunt TankPostTauntStep(const TankPostTauntIn& in)",
                       "inline const char* TankPostTauntWord(TankPostTaunt t)"):
            self.assertIn(needle, POLICY)
        for word in ('"none"', '"taunt"', '"stance"', '"area"', '"gate"', '"not_ready"'):
            self.assertIn("return " + word + ";", POLICY)

    def test_taunt_key_is_read_templated_and_documented(self):
        self.assertIn('dungeonRunTankPostTaunts = config.GetBoolDefault("AiPlayerbot.DungeonRunTankPostTaunts", '
                      'false);', CONFIG_CPP)
        self.assertIn("bool dungeonRunTankPostTaunts = false;", CONFIG_H)
        self.assertRegex(TEMPLATE, r"(?m)^AiPlayerbot\.DungeonRunTankPostTaunts = 0$")
        self.assertIn("AiPlayerbot.DungeonRunTankPostTaunts", DOCS)
        section = DOCS[DOCS.index("### `AiPlayerbot.DungeonRunTankPostTaunts`"):][:900]
        self.assertIn("`0` off", section)
        self.assertNotIn("recommended on", section)  # walk 2e keys are documented without it
        self.assertIn("DungeonRunTankPostTaunts", BOSS_DOCS)
        self.assertIn("DungeonRunTankPostTaunts", CHANGELOG)
        # The row: one per cast, and a word that did not cast at most once per bot and word per 5 s, under the lock.
        log = body(ACTIONS, "void LogTankPostTaunt(")
        self.assertIn('"event=taunt,step=%s,spell=%s,victim=%s,cast=%d"', log)
        self.assertIn("TankPostTauntWord(step)", log)
        self.assertIn('sPlayerbotAIConfig.logEvent(ai, "BossTacticsTankPost", info1, info2)', log)
        self.assertIn("std::lock_guard<std::mutex> lock(g_tankPostMutex);", log)
        self.assertIn("if (!cast)", log)
        self.assertIn("it->second.tauntLogMs[static_cast<std::size_t>(step)]", log)
        self.assertIn("nowMs - last < kTankPostTauntLogGapMs", log)
        self.assertIn("std::array<std::uint32_t, 6> tauntLogMs{};", ACTIONS)
        self.assertIn("stored.tauntLogMs = priorTauntLog;", ACTIONS)
        # The radius cap went 40 -> 100 for the same branch.
        self.assertIn("row.param < 5.0f || row.param > 100.0f", POLICY)

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

    def test_engage_step_walks_to_its_subject(self):
        # AiPlayerbot.DungeonRunTankPostWalks: the Engage step walks to a subject out of melee reach instead of ending
        # the tick on the attack (walk 2f a-2: slot 1 on the post 40 yd from Cindarion). The rule is pure
        # (TankPostEngageWalks) and tested in tools/test_boss_tactics_policy.cpp; these pins prove the live use.
        action = body(ACTIONS, "bool BossTacticsTankPostAction::Execute(Event& event)")
        engage = action[action.index("if (task.step == TankPostDecision::Engage)"):]
        engage = engage[:engage.index("return Attack(bot, task.subject);")]
        self.assertIn("TankPostEngageWalks(sPlayerbotAIConfig.dungeonRunTankPostWalks, false,", engage)
        self.assertIn("bot->CanReachWithMeleeAutoAttack(task.subject), task.standActive);", engage)
        # After the taunt, before the plain attack: a taunt that cast ends the tick, a walk only starts without one.
        self.assertLess(engage.index("TakeSubject(task, current)"), engage.index("TankPostEngageWalks("))
        self.assertLess(engage.index("TankPostEngageWalks("), engage.index("if (current == task.subject)"))
        # A taunt that cast keeps the walk's launch point with the key on; with it off the old reset stands.
        self.assertIn("if (!sPlayerbotAIConfig.dungeonRunTankPostWalks)\n                hasLast = false;", engage)
        # The destination is a point at melee distance on the line to the subject, not MoveTo(Unit*) (a hostile unit's
        # MoveTo aims the stance's formation spot): the point is checked for ground and the walk issues it itself.
        self.assertIn("TankPostMeleePoint(bot->GetPositionX(), bot->GetPositionY(),", engage)
        self.assertIn("bot->GetCombatReach(task.subject, false, 0.0f), x, y)", engage)
        self.assertIn("float const floor = map ? map->GetHeight(x, y, z + 5.0f) : INVALID_HEIGHT;", engage)
        self.assertIn("walkPoint = floor > INVALID_HEIGHT;", engage)
        self.assertNotIn("MoveTo(task.subject)", engage)
        self.assertIn("if (!MoveTo(bot->GetMapId(), x, y, z, false, false, false, true))\n                return false;",
                      engage)
        # The subject is taken first (the result ignored), then the tank walks and the move's result is returned.
        self.assertIn("if (current != task.subject)\n                Attack(bot, task.subject);", engage)
        self.assertLess(engage.index("if (!MoveTo(bot->GetMapId()"), engage.index("lastX = x;"))
        # The spline guard is the walk's own, with the issued destination and 3 yd.
        self.assertIn("hasLast && !bot->IsStopped() && !bot->movespline->Finalized() &&", engage)
        self.assertIn("std::fabs(lastX - x) < 3.0f && std::fabs(lastY - y) < 3.0f", engage)
        self.assertIn("lastX = x;\n            lastY = y;", engage)
        # One row per walk episode, through the post's own logger.
        self.assertIn("if (!walkLogged)\n            {\n                LogTankPostWalk(ai, *task.row,", engage)
        self.assertIn("walkLogged = true;", engage)
        self.assertIn("if (task.step != TankPostDecision::Engage)\n        walkLogged = false;", action)
        self.assertIn("bool walkLogged = false;", ACTIONS_H)
        walk_log = body(ACTIONS, "void LogTankPostWalk(")
        self.assertIn('"event=walk,subject_yd=%.1f", subjectYd', walk_log)
        self.assertIn('sPlayerbotAIConfig.logEvent(ai, "BossTacticsTankPost", info1, info2)', walk_log)
        self.assertIn('sPlayerbotAIConfig.hasLog("bot_events.csv")', walk_log)
        self.assertIn("inline bool TankPostEngageWalks(bool keyOn, bool tauntWentOut, bool inMeleeReach, "
                      "bool standActive)", POLICY)
        self.assertIn("return keyOn && !tauntWentOut && !inMeleeReach && !standActive;", POLICY)
        self.assertIn("inline bool TankPostMeleePoint(float botX, float botY, float subjectX, float subjectY, "
                      "float reachYd, float& x,", POLICY)
        # The key: read with the row's mode, defaulted off, templated and documented without "recommended on".
        self.assertIn('dungeonRunTankPostWalks = config.GetBoolDefault("AiPlayerbot.DungeonRunTankPostWalks", false);',
                      CONFIG_CPP)
        self.assertIn("bool dungeonRunTankPostWalks = false;", CONFIG_H)
        self.assertRegex(TEMPLATE, r"(?m)^AiPlayerbot\.DungeonRunTankPostWalks = 0$")
        self.assertIn("AiPlayerbot.DungeonRunTankPostWalks", DOCS)
        section = DOCS[DOCS.index("### `AiPlayerbot.DungeonRunTankPostWalks`"):][:900]
        self.assertIn("`0` off", section)
        self.assertNotIn("recommended on", section)
        self.assertIn("DungeonRunTankPostWalks", BOSS_DOCS)
        self.assertIn("DungeonRunTankPostWalks", CHANGELOG)

    def test_a_post_record_owns_one_unit(self):
        # AiPlayerbot.DungeonRunTankPostOwnsUnit: two posts on the Hatcher entry 50144 reserved each other's units and
        # the post's own update reset the shared loose clock, so the 15 s grace never ended. The rule is pure
        # (TankPostRecordCovers) and tested in tools/test_boss_tactics_policy.cpp; this pin proves the live use.
        reserve = body(ACTIONS, "bool ai::TankPostReservesSubject(PlayerbotAI* ai, Player* bot, Unit* target)")
        self.assertIn("!TankPostRecordCovers(sPlayerbotAIConfig.dungeonRunTankPostOwnsUnit,", reserve)
        self.assertIn("entry.second.arrivedSubjectGuid, target->GetObjectGuid().GetRawValue()) ||", reserve)
        # The per-unit filter sits in the key-collecting loop, beside the entry and liveness tests.
        collect = reserve[reserve.index("for (std::pair<std::uint64_t const, TankPostRecord>& entry : g_tankPost)"):]
        collect = collect[:collect.index("others.push_back(entry.first);")]
        self.assertIn("entry.second.targetEntry != target->GetEntry()", collect)
        self.assertIn("TankPostRecordCovers(", collect)
        self.assertLess(collect.index("TankPostRecordCovers("), collect.index("nowMs - entry.second.lastReadMs"))
        self.assertIn("inline bool TankPostRecordCovers(bool keyOn, std::uint64_t latchedGuid, "
                      "std::uint64_t targetGuid)", POLICY)
        self.assertIn("return !keyOn || latchedGuid == 0 || latchedGuid == targetGuid;", POLICY)
        # The pick keeps each post on its own unit: this record's latched unit wins, another live post's is no
        # candidate, so two posts on one entry do not trade Hatchers read by read.
        find = body(ACTIONS, "BossTankPost ai::FindBossTankPost")
        self.assertIn("std::unordered_set<std::uint64_t> otherLatched;", find)
        self.assertIn("if (sPlayerbotAIConfig.dungeonRunTankPostOwnsUnit)\n    {\n        "
                      "std::lock_guard<std::mutex> lock(g_tankPostMutex);", find)
        self.assertIn("entry.second.arrivedSubjectGuid == 0 ||", find)
        self.assertIn("nowMs - entry.second.lastReadMs >= kTankPostLiveMs)", find)
        self.assertIn("otherLatched.insert(entry.second.arrivedSubjectGuid);", find)
        self.assertIn("const std::uint64_t ownLatched = haveRecord ? record.arrivedSubjectGuid : 0;", find)
        self.assertIn("inline int PickTankPostUnit(const std::vector<TankPostUnit>& units, float spotX, float spotY, "
                      "float spotZ,", POLICY)
        self.assertIn("if (ownsUnit && latchedGuid != 0)", POLICY)
        self.assertIn("if (units[i].guid == latchedGuid && units[i].alive)", POLICY)
        self.assertIn("if (ownsUnit && unit.guid != latchedGuid && otherLatched.count(unit.guid) != 0)", POLICY)
        self.assertIn('dungeonRunTankPostOwnsUnit = '
                      'config.GetBoolDefault("AiPlayerbot.DungeonRunTankPostOwnsUnit", false);', CONFIG_CPP)
        self.assertIn("bool dungeonRunTankPostOwnsUnit = false;", CONFIG_H)
        self.assertRegex(TEMPLATE, r"(?m)^AiPlayerbot\.DungeonRunTankPostOwnsUnit = 0$")
        self.assertIn("AiPlayerbot.DungeonRunTankPostOwnsUnit", DOCS)
        section = DOCS[DOCS.index("### `AiPlayerbot.DungeonRunTankPostOwnsUnit`"):][:900]
        self.assertIn("`0` off", section)
        self.assertNotIn("recommended on", section)
        self.assertIn("DungeonRunTankPostOwnsUnit", BOSS_DOCS)
        self.assertIn("`AiPlayerbot.DungeonRunTankPostOwnsUnit = 1`", BOSS_DOCS)
        self.assertIn("DungeonRunTankPostOwnsUnit", CHANGELOG)


    def test_p3_main_tank_post_keeps_its_row_and_waits(self):
        # Walk 2g P3: the slot-0 tank targets a whelp in 42 % of its frames, all five post ends were `reason=slot` (a
        # tank ahead died and the row pick followed the live index), and no post could run before Onyxia was
        # targetable. The rules are pure (TankPostRowUsable, TankPostKeepsRow, TankPostUnitPick, the step's wait) and
        # tested in tools/test_boss_tactics_policy.cpp; these pins prove the live code uses them.
        find = body(ACTIONS, "BossTankPost ai::FindBossTankPost")
        # The bot's record is read before the row pick: the kept-row rule needs its latched unit.
        self.assertLess(find.index("TankPostRecord record;"), find.index("const BossTacticRow* row = nullptr;"))
        self.assertIn("TankPostKeepsRow(sPlayerbotAIConfig.dungeonRunTankPostKeepsUnit, haveRecord, "
                      "record.arrivedSubjectGuid,", find)
        self.assertIn("latchedUnit && sServerFacade.IsAlive(latchedUnit)", find)
        # The kept row is the record's own row, with no priority test, and the slot pick runs only when it is gone,
        # when another live post holds it, or when this bot is now the main tank with an open slot-0 row.
        self.assertIn("candidate->kind != BossTacticKind::TankPost || !TankPostSameRow(record, *candidate)", find)
        self.assertIn("!TankPostKeptRowYields(roles.botIndex == 0, slotRow != nullptr)", find)
        self.assertIn("const BossTacticRow* slotRow = nullptr;", find)
        self.assertIn("TankPostSlotRow(sPlayerbotAIConfig.dungeonRunTankPostMain, candidate->priority, "
                      "roles.botIndex)", find)
        self.assertIn("if (!row && slotRow)\n    {\n        row = slotRow;\n        boss = slotBoss;", find)
        self.assertIn("inline bool TankPostSlotRow(bool mainKeyOn, std::uint32_t priority, int botIndex)", POLICY)
        self.assertIn("inline bool TankPostRowUsable(bool mainKeyOn, std::uint32_t priority)", POLICY)
        self.assertIn("return priority != 0 || mainKeyOn;", POLICY)
        self.assertIn("inline bool TankPostKeepsRow(bool keyOn, bool haveRecord, std::uint64_t latchedGuid, "
                      "bool latchedAlive)", POLICY)
        self.assertIn("return keyOn && haveRecord && latchedGuid != 0 && latchedAlive;", POLICY)
        # A waiting hold_spot post: the input, the start without a subject, and the action's subjectless steps.
        self.assertIn("in.waits = sPlayerbotAIConfig.dungeonRunTankPostWaits && holdSpot &&", find)
        self.assertIn("(!haveRecord || record.arrivedSubjectGuid == 0);", find)
        self.assertIn("bool waits = false;", POLICY)
        self.assertIn("if (!in.waits && (!in.subjectFound || !in.subjectAlive || in.subjectEvading ||", find)
        action = body(ACTIONS, "bool BossTacticsTankPostAction::Execute(Event& event)")
        self.assertIn("if (!TankPostActionRuns(task.valid, task.row != nullptr, task.subject != nullptr, task.step))",
                      action)
        self.assertIn("inline bool TankPostActionRuns(bool valid, bool haveRow, bool haveSubject, "
                      "TankPostDecision step)", POLICY)
        step = body(POLICY, "inline TankPostDecision TankPostStep(const TankPostIn& in)")
        waits = step[step.index("if (in.waits)"):]
        waits = waits[:waits.index("return in.started ?")]
        self.assertIn("if (in.seated)\n                    return TankPostDecision::Yield;", waits)
        self.assertIn("return in.botToSpotYd > kTankPostDragYd ? TankPostDecision::Leash : "
                      "TankPostDecision::Hold;", waits)
        # The tank's own pick keeps the latched unit before any rule that could switch it off (the seek source still
        # reads first), and only when the key is on.
        is_better = TANK_TARGET[TANK_TARGET.index("bool IsBetter(Unit* newUnit, Unit* oldUnit)"):]
        is_better = is_better[:is_better.index("float newThreat")]
        self.assertIn("::ai::TankPostUnitPick(sPlayerbotAIConfig.dungeonRunTankPostKeepsUnit,", is_better)
        self.assertLess(is_better.index("TankPostUnitPick("), is_better.index("IsTankHoldPickup("))
        self.assertGreater(is_better.index("TankPostUnitPick("),
                           is_better.index("::ai::IsSeekSourceForTankPickup(ai, bot, oldUnit)"))
        self.assertIn("if (postPick > 0)\n            return true;", is_better)
        self.assertIn("if (postPick < 0)\n            return false;", is_better)
        self.assertIn("inline int TankPostUnitPick(bool keyOn, std::uint64_t latched, std::uint64_t newGuid, "
                      "std::uint64_t oldGuid)", POLICY)
        # The latched unit is read from the live record under the post lock.
        latched = body(ACTIONS, "std::uint64_t ai::TankPostLatchedUnit(Player* bot)")
        self.assertIn("std::lock_guard<std::mutex> lock(g_tankPostMutex);", latched)
        self.assertIn("WorldTimer::getMSTime() - it->second.lastReadMs >= kTankPostLiveMs", latched)
        self.assertIn("return it->second.arrivedSubjectGuid;", latched)
        self.assertIn("std::uint64_t TankPostLatchedUnit(Player* bot);", ACTIONS_H)
        # The three keys: read, defaulted off, templated and documented without "recommended on".
        for key in ("DungeonRunTankPostMain", "DungeonRunTankPostKeepsUnit", "DungeonRunTankPostWaits"):
            self.assertIn('config.GetBoolDefault("AiPlayerbot.%s", false);' % key, CONFIG_CPP)
            self.assertIn("bool dungeonRun%s = false;" % key[len("DungeonRun"):], CONFIG_H)
            self.assertRegex(TEMPLATE, r"(?m)^AiPlayerbot\.%s = 0$" % key)
            self.assertIn("AiPlayerbot.%s" % key, DOCS)
            self.assertIn("`0` off",
                          DOCS[DOCS.index("### `AiPlayerbot.%s`" % key):][:900])
            self.assertNotIn("recommended on",
                             DOCS[DOCS.index("### `AiPlayerbot.%s`" % key):][:900])
            self.assertIn(key, BOSS_DOCS)
            self.assertIn(key, CHANGELOG)
        # The latched unit is read only with the key on: the key is tested before the record's mutex is taken.
        self.assertIn("sPlayerbotAIConfig.dungeonRunTankPostKeepsUnit ? ::ai::TankPostLatchedUnit(bot) : 0;",
                      is_better)
        self.assertIn("it->second.done ||", latched)

    def test_new_main_tank_takes_the_boss_row_and_a_wait_ends_once(self):
        # Fable H1 (nobody takes Onyxia after the main tank dies), Codex T (a subjectless Hold was rejected), Fable M1 (a
        # waiting post restarted forever). The rules are pure (TankPostRowHeldByOther, TankPostKeptRowYields,
        # TankPostSlotRow, TankPostActionRuns, TankPostKeepsDoneRecord and the step's done input) and tested in
        # tools/test_boss_tactics_policy.cpp; these pins prove the live code uses them.
        find = body(ACTIONS, "BossTankPost ai::FindBossTankPost")
        # H1a: a row another live post holds is no row for this bot, and a live post is a record read within the
        # reservation's 3 s whose tank is in this instance and can act; only the keys leave the lock.
        self.assertIn("std::vector<std::pair<std::uint64_t, TankPostRecord> > others;", find)
        self.assertIn("others.push_back(entry);", find)
        self.assertIn("TankPostRowHeldByOther(true, TankPostTankCanAct(tank))", find)
        self.assertIn("if (TankPostSameRow(other, candidate))\n                return true;", find)
        self.assertIn("!candidateBoss || rowHeldByOther(*candidate)", find)
        self.assertIn("inline bool TankPostRowHeldByOther(bool liveRecord, bool otherTankCanAct)", POLICY)
        self.assertIn("return liveRecord && otherTankCanAct;", POLICY)
        # H1b: the new main tank gives its kept row up for an open slot-0 row; the key gates the slot-0 row itself.
        self.assertIn("inline bool TankPostKeptRowYields(bool botIsMain, bool slotRowOpen)", POLICY)
        self.assertIn("return botIsMain && slotRowOpen;", POLICY)
        # Codex T: a subjectless Hold reaches the action, and the Hold branch stops the tank's move.
        action = body(ACTIONS, "bool BossTacticsTankPostAction::Execute(Event& event)")
        self.assertIn("if (!TankPostActionRuns(task.valid, task.row != nullptr, task.subject != nullptr, task.step))",
                      action)
        hold = action[action.index("if (task.step == TankPostDecision::Hold)"):]
        self.assertIn("if (!bot->IsStopped())\n            ai->StopMoving();", hold)
        # The subjectless Hold is let through before the branch that stops the tank: the guard runs first.
        self.assertLess(action.index("TankPostActionRuns("),
                        action.index("if (task.step == TankPostDecision::Hold)"))
        self.assertIn("inline bool TankPostActionRuns(bool valid, bool haveRow, bool haveSubject, "
                      "TankPostDecision step)", POLICY)
        self.assertIn("(haveSubject || step == TankPostDecision::Leash || step == TankPostDecision::Hold);", POLICY)
        # M1: an ending waiting post keeps a done record; the step does nothing with it, and the action refreshes its
        # read time so the idle sweep cannot drop it inside the band.
        self.assertIn("bool done = false;", ACTIONS)
        self.assertIn("in.waitRow = sPlayerbotAIConfig.dungeonRunTankPostWaits && holdSpot;", find)
        self.assertIn("in.done = haveRecord && record.done;", find)
        self.assertIn("if (TankPostKeepsDoneRecord(in.waitRow, step))", find)
        self.assertIn("stored.done = true;", find)
        self.assertIn("if (in.done)\n        {\n            std::lock_guard<std::mutex> lock(g_tankPostMutex);", find)
        self.assertIn("if (!record.done)\n        {\n            BossTacticRow closed;", find)
        self.assertIn("inline bool TankPostKeepsDoneRecord(bool waitRow, TankPostDecision step)", POLICY)
        self.assertIn("return waitRow && step == TankPostDecision::End;", POLICY)
        self.assertIn("if (in.done)\n            return TankPostDecision::None;", POLICY)
        self.assertIn("bool done = false;                // the post already ended", POLICY)
        # A done record holds nothing: no latched unit, no reservation, no unit for another post's pick, no second
        # end row on the sweep.
        self.assertIn('if (it == g_tankPost.end() || it->second.done ||', ACTIONS)
        reserve = body(ACTIONS, "bool ai::TankPostReservesSubject(PlayerbotAI* ai, Player* bot, Unit* target)")
        self.assertIn("entry.first == self || entry.second.done ||", reserve)
        owns = find[find.index("std::unordered_set<std::uint64_t> otherLatched;"):]
        self.assertIn("entry.first == self || entry.second.done ||", owns)
        self.assertIn("if (!it->second.done)\n                        ended.push_back(", ACTIONS)

    def test_a_done_record_survives_a_renumbering(self):
        # Codex M: a renumbering erased the waiting post's done record before its band closed, and the row started a
        # fresh post inside the same band. The rule is pure (TankPostDoneRecordStays) and tested in
        # tools/test_boss_tactics_policy.cpp; this pin proves the live code keeps the record while its band stands.
        find = body(ACTIONS, "BossTankPost ai::FindBossTankPost")
        self.assertIn("if (TankPostDoneRecordStays(record.done, bandOpen))", find)
        self.assertIn("inline bool TankPostDoneRecordStays(bool done, bool bandOpen) { return done && bandOpen; }",
                      POLICY)
        # The band stands while the record's row is in the fight, whatever slot index it moved to, and its boss lives.
        self.assertIn("if (record.done && record.map == mapId)", find)
        self.assertIn("Unit* const recordBoss = record.bossGuid != 0 ? ai->GetUnit(ObjectGuid(record.bossGuid)) : "
                      "nullptr;", find)
        self.assertIn("if (recordBoss && sServerFacade.IsAlive(recordBoss))", find)
        self.assertIn("candidate->kind == BossTacticKind::TankPost && TankPostSameRow(record, *candidate)", find)
        # The keep refreshes the read time instead of erasing, so the idle sweep cannot drop it; the closed band still
        # erases without a second end row.
        keep = find[find.index("if (TankPostDoneRecordStays(record.done, bandOpen))"):]
        keep = keep[:keep.index("g_tankPost.erase(self);")]
        self.assertIn("it->second.lastReadMs = nowMs;", keep)
        self.assertIn("if (!record.done)\n        {\n            BossTacticRow closed;", find)


if __name__ == "__main__":
    unittest.main()
