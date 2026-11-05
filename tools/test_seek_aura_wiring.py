"""Wiring pins for the seek_aura and spread boss tactic kinds (the Hakkar fight).

AiPlayerbot.DungeonRunSeekAura arms the seek_aura row: the raid gathers in a dying Son of Hakkar's 8 yd cloud so the
90 s Blood Siphon hurts Hakkar instead of healing him. The rules are pure and live in ai/playerbot/SeekAuraPolicy.h
(their behaviour is tested in tools/test_seek_aura_policy.cpp); this file only proves the live code calls them and that
the row's key reaches a consumer:

 - the ground-hazard readers skip the row's cloud, so the bots stand in it on purpose (the step-out, the drag, the
   move hold and the landing tests all read the same two collectors);
 - the seek trigger runs in the combat and the reaction engine (a run-held bot, a bot in a pull's join window) and
   the move hold keeps the walk from being undone;
 - the second off-tank drags a source to the spot, a free DPS bot pre-damages and burns it, and nobody burns it at
   the wrong time (SeekSourceHeld);
 - the spread row widens the ranged spacing and also applies to a bot the run holds on a stay.

AiPlayerbot.DungeonRunBossSpread arms the spread row. AiPlayerbot.DungeonRunCharmYield is W3's and
AiPlayerbot.DungeonRunPullGate is W4's, so their allowlist lines stay.

Run: python3 -m unittest tools/test_seek_aura_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
ACTIONS_H = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.h").read_text()
POLICY = (ROOT / "ai/playerbot/SeekAuraPolicy.h").read_text()
STATE_POLICY = (ROOT / "ai/playerbot/SeekFightStatePolicy.h").read_text()
TACTICS_POLICY = (ROOT / "ai/playerbot/BossTacticsPolicy.h").read_text()
STRATEGY = (ROOT / "ai/playerbot/strategy/generic/BossTacticsStrategy.cpp").read_text()
TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/BossTacticsTriggers.cpp").read_text()
TRIGGERS_H = (ROOT / "ai/playerbot/strategy/triggers/BossTacticsTriggers.h").read_text()
ACTION_CONTEXT = (ROOT / "ai/playerbot/strategy/actions/ActionContext.h").read_text()
TRIGGER_CONTEXT = (ROOT / "ai/playerbot/strategy/triggers/TriggerContext.h").read_text()
NEAREST = (ROOT / "ai/playerbot/strategy/values/NearestGameObjects.cpp").read_text()
AOE = (ROOT / "ai/playerbot/strategy/values/AoeValues.cpp").read_text()
DUNGEON_TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/DungeonTriggers.cpp").read_text()
DUNGEON_ACTIONS = (ROOT / "ai/playerbot/strategy/actions/DungeonActions.cpp").read_text()
CONFIG_USAGE = (ROOT / "tools/verify_config_usage.py").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class CloudExemptionWiring(unittest.TestCase):
    """The row's cloud is the one ground effect the raid stands in."""

    def test_the_dynobj_reader_skips_the_sought_cloud(self):
        push = body(NEAREST, "void PushDynObjHazard(")
        self.assertIn("PlayerbotAI* ai", push.split(")", 1)[0])
        # First after the null tests, before anything is pushed: the exemption must not depend on the hazard verdict.
        self.assertLess(push.index("if (!dyn || !dyn->IsInWorld())"), push.index("if (IsSoughtCloudFor(ai, bot, dyn))"))
        self.assertLess(push.index("if (IsSoughtCloudFor(ai, bot, dyn))"),
                        push.index("DynObjIsHostileHazard("))
        # Its two callers hand their own ai down.
        self.assertIn("PushDynObjHazard(ai, bot, map->GetDynamicObject(guid), out, creatureCastersOnly)", NEAREST)

    def test_the_area_debuff_reader_skips_the_sought_cloud(self):
        calc = body(AOE, "bool HasAreaDebuffValue::Calculate()")
        self.assertIn("if (IsSoughtCloudFor(ai, bot, go))", calc)
        self.assertLess(calc.index("if (IsSoughtCloudFor(ai, bot, go))"),
                        calc.index("IsPositiveEffect(spellProto, go->GetEffIndex())"))

    def test_the_matcher_reads_both_the_spell_and_its_trigger(self):
        match = body(ACTIONS, "bool ai::IsSoughtCloudFor(")
        self.assertIn("sPlayerbotAIConfig.dungeonRunSeekAura", match)
        self.assertIn("HasSeekAuraMap(bot->GetMapId())", match)
        self.assertIn("EffectTriggerSpell[dyn->GetEffIndex()]", match)
        self.assertIn("IsSoughtCloud(dyn->GetSpellId(),", match)
        self.assertIn("BossTacticKind::SeekAura", match)
        # The pure rule owns the comparison, not a copy of it.
        self.assertIn("inline bool IsSoughtCloud(", POLICY)


class SeekWalkWiring(unittest.TestCase):
    """The walk in, its hold and the engine it runs on."""

    def test_the_seek_runs_in_the_combat_and_the_reaction_engine(self):
        combat = body(STRATEGY, "void BossTacticsStrategy::InitCombatTriggers(")
        reaction = body(STRATEGY, "void BossTacticsStrategy::InitReactionTriggers(")
        node = '"boss tactics seek"'
        for text in (combat, reaction):
            self.assertIn(node, text)
            self.assertIn('new NextAction("boss tactics seek", ACTION_EMERGENCY + 5)', text)
        # Above the stand walk, below the freeze (+7) and the flee/avoid_cast walk-outs (+6).
        self.assertIn("ACTION_EMERGENCY + 5", combat)
        self.assertLess(combat.index(node), combat.index('"boss tactics stand"'))
        self.assertGreater(combat.index(node), combat.index('"boss tactics avoid cast"'))

    def test_the_trigger_and_action_are_registered(self):
        self.assertIn('creators["boss tactics seek"] = [](PlayerbotAI* ai) { return new BossTacticsSeekTrigger(ai); };',
                      TRIGGER_CONTEXT)
        self.assertIn('creators["boss tactics seek"] = [](PlayerbotAI* ai) { return new BossTacticsSeekAction(ai); };',
                      ACTION_CONTEXT)
        self.assertIn("class BossTacticsSeekTrigger : public Trigger", TRIGGERS_H)
        self.assertIn("bool BossTacticsSeekTrigger::IsActive()", TRIGGERS)
        self.assertIn("FindBossSeek(ai, bot).valid", body(TRIGGERS, "bool BossTacticsSeekTrigger::IsActive()"))

    def test_the_walk_is_not_a_stay_walk(self):
        # MovementAction::isUseful is false on a stay, and the bots of a run fight on one: the avoid_cast and the
        # use_object finding's precedent.
        action = body(ACTIONS, "bool BossTacticsSeekAction::isUseful()")
        self.assertNotIn("MovementAction::isUseful()", action)
        self.assertIn("ai->CanMove()", action)

    def test_the_move_hold_stands_the_other_moves_down(self):
        hold = body(ACTIONS, "float BossTacticsSeekHoldMultiplier::GetValue(")
        self.assertIn("dynamic_cast<MovementAction*>(action)", hold)
        for exempt in ("BossTacticsSeekAction", "BossTacticsFreezeAction", "BossTacticsFleeAction",
                       "BossTacticsAvoidCastAction"):
            self.assertIn(exempt, hold)
        # The kill pick and any other attack is a MovementAction too: holding it would stop the burn.
        self.assertIn("dynamic_cast<AttackAction*>(action)", hold)
        self.assertIn("if (!BossSeeking(ai, bot))", hold)
        self.assertIn("return 0.0f;", hold)
        self.assertIn("new BossTacticsSeekHoldMultiplier(ai)", body(STRATEGY,
                      "void BossTacticsStrategy::InitCombatMultipliers("))

    def test_the_spread_trigger_stands_down_while_the_bot_seeks(self):
        trigger = body(DUNGEON_TRIGGERS, "bool RaidSpreadNeededTrigger::IsActive()")
        self.assertIn("if (BossSeeking(ai, bot))", trigger)
        self.assertLess(trigger.index("if (BossSeeking(ai, bot))"), trigger.index("ShouldRaidSurvivalSpread("))

    def test_the_policy_rules_all_have_a_live_caller(self):
        seek = body(ACTIONS, "namespace\n{\n// The seek_aura fight state")
        for rule in ("SeekDueMs(", "SeekCycleAccepts(", "SeekWindowOpen(", "SeekForceBurn(", "SeekWanted(",
                     "IsSoughtCloud(", "SeekCloudReachable(", "SeekGatherReady(", "SeekSourceHeld(",
                     "SeekTankPullDue(", "SeekActionConsumesTick(", "SeekKeepOrPickGatherSource(",
                     "SeekSourceSearchYd(", "SeekClockStartOver("):
            self.assertIn(rule, ACTIONS, rule)
        self.assertIn("kSeekLatencyMs", POLICY)
        self.assertIn("kSeekGatherMaxMs", POLICY)


class FightStateWiring(unittest.TestCase):
    """One record per fight, exported to the run leader, swept and erased on an evade."""

    def test_the_state_is_shared_keyed_and_swept(self):
        state = body(ACTIONS, "namespace\n{\n// The seek_aura fight state", "\n// A record nobody evaluated")
        self.assertIn("std::mutex g_seekMutex;", state)
        # The key is the row's primary key, never its address: a reload frees the old table.
        self.assertIn("using SeekRecordKey = std::tuple<", STATE_POLICY)
        self.assertIn("using SeekRecordMap = std::map<SeekRecordKey, SeekCycleRecord>;", STATE_POLICY)
        record = body(STATE_POLICY, "struct SeekCycleRecord", "\n    };")
        self.assertNotIn("BossTacticRow", record)
        self.assertIn("SeekRecordMap g_seekRecords;", state)
        self.assertNotIn("g_seekRecords.clear()", ACTIONS)
        self.assertIn("record.cycleMs = cycleMs;", ACTIONS)
        self.assertIn("record.cycleSpell = row->cycleSpell;", ACTIONS)
        self.assertIn("kSeekRecordIdleMs = 120000", ACTIONS)
        self.assertIn("nowMs - it->second.lastSeenMs > kSeekRecordIdleMs", ACTIONS)
        # The clock: the engage, the accepted siphon and the gather latch.
        self.assertIn("SeekCycleAccepts(record.lastCycleMs, nowMs, cycleMs)", ACTIONS)
        self.assertIn("++record.cycleNo;", ACTIONS)
        # The latch: a heal on the Son after the gather was ready does not hold it again.
        self.assertIn("record.ready = record.ready || SeekGatherReady(record.missing, record.gatherStartMs, nowMs, "
                      "forceBurn);", ACTIONS)

    def test_the_reload_keeps_a_live_record_and_drops_a_retimed_one(self):
        # The reload reconciles before the old table is retired (CommitPlanReload's callback), against the new rows.
        self.assertIn("ReconcileSeekRecords(g_seekRecords, next);", ACTIONS)
        self.assertIn("void ai::ReconcileSeekFightState(const BossTacticTable& next)", ACTIONS)
        self.assertIn("SeekRecordSurvivesReload(it->second, *row)", STATE_POLICY)
        self.assertIn("record.cycleSpell == row.cycleSpell", STATE_POLICY)
        self.assertIn("beforeInstall(*nextTactics);", (ROOT / "ai/playerbot/DungeonPlanReloadPolicy.h").read_text())

    def test_the_evade_erases_the_record(self):
        read = body(ACTIONS, "bool ReadSeekFight(")
        self.assertIn("!boss->IsInCombat()", read)
        self.assertIn("GetHealthPercent() >= 100.0f", read)
        self.assertIn("erase", read)

    def test_the_snapshot_is_exported_for_the_run_leader(self):
        self.assertIn("bool SeekCycleSnapshot(std::uint32_t instanceId, std::uint64_t bossGuid, SeekCycleView& out);",
                      ACTIONS_H)
        self.assertIn("bool ai::SeekCycleSnapshot(std::uint32_t instanceId, std::uint64_t bossGuid, SeekCycleView& out)",
                      ACTIONS)

    def test_each_accepted_cycle_logs_the_dosed_count(self):
        self.assertIn('"BossTacticsSiphon"', ACTIONS)
        self.assertIn("24323", ACTIONS)
        self.assertIn("24322", ACTIONS)
        self.assertIn("dosed=%u,undosed=%u", ACTIONS.replace("\\n", ""))


class SeekRolesWiring(unittest.TestCase):
    """Who fetches, who damages and who burns."""

    def test_the_second_off_tank_fetches_a_source(self):
        self.assertIn('creators["boss tactics seek tank"]', ACTION_CONTEXT)
        self.assertIn('creators["boss tactics seek tank"]', TRIGGER_CONTEXT)
        self.assertIn('new NextAction("boss tactics seek tank", ACTION_HIGH + 6)',
                      body(STRATEGY, "void BossTacticsStrategy::InitCombatTriggers("))
        tank = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(")
        self.assertIn("FindBossSeekTank(ai, bot)", tank)
        self.assertIn("MoveTo(", tank)
        find = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(")
        # Only tanks[2] owns the job.
        self.assertIn("GetRaidTankRoles(ai)", find)
        self.assertIn("tanks.size() < 3", find)
        self.assertIn("tanks[2]", find)
        # The pull rule and the gather radius are the policy's.
        self.assertIn("SeekTankPullDue(", find)
        # The pickup search and the drag's arrival are the policy's own distances.
        self.assertIn("kSeekPickupYd", find)
        self.assertIn("kSeekTankArriveYd", find)
        # The gather's own radius comes from the spot's read; without the third tank the search widens to the
        # fallback (M3).
        read = body(ACTIONS, "bool ReadSeekFight(")
        self.assertIn("SeekSourceSearchYd(roles.tanks.size() >= 3, out.forceBurn, belowHold)", read)

    def test_the_seek_leaves_exactly_the_three_tanks_out(self):
        # The main tank (the boss's victim) and the two off-tanks; a fourth tank has no such job and seeks with the
        # raid, so the exemption names slots 0-2 instead of every tank the roles list happens to hold.
        fn = body(ACTIONS, "bool IsSeekExemptTank(")
        self.assertIn("i < 3", fn)
        self.assertIn("roles.tanks[i] == member", fn)
        self.assertIn("IsSeekExemptTank(roles, bot)", ACTIONS)
        self.assertIn("IsSeekExemptTank(roles, member)", ACTIONS)

    def test_the_kill_pick_is_at_high_plus_two_and_obeys_the_hold(self):
        self.assertIn('creators["boss tactics seek kill"]', ACTION_CONTEXT)
        self.assertIn('new NextAction("boss tactics seek kill", ACTION_HIGH + 2)',
                      body(STRATEGY, "void BossTacticsStrategy::InitCombatTriggers("))
        kill = body(ACTIONS, "Unit* ai::FindBossSeekKill(")
        self.assertIn("IsFreeDps(ai, bot)", kill)
        # Every source but the one at the spot is held all window, and the one at the spot obeys SeekSourceHeld.
        self.assertIn("SeekHoldsUnit(state, state.source)", kill)

    def test_the_sources_are_held_by_the_dps_hold_multiplier(self):
        hold = body(ACTIONS, "float BossTacticsHoldMultiplier::GetValue(")
        self.assertIn("SeekHoldsUnit(state, source)", hold)
        self.assertIn("seeksRow", hold)


class SpreadKindWiring(unittest.TestCase):
    """The spread row widens the ranged spacing and outranks a stay."""

    def test_the_trigger_and_the_action_take_the_row_distance(self):
        trigger = body(DUNGEON_TRIGGERS, "bool RaidSpreadNeededTrigger::IsActive()")
        self.assertIn("BossSpreadYd(ai, bot, kSpreadSettledDistance)", trigger)
        self.assertNotIn("< 10.0f", trigger)
        act = body(DUNGEON_ACTIONS, "bool RaidSpreadAction::Execute(Event& event)")
        self.assertIn("BossSpreadYd(ai, bot, kSpreadSettledDistance)", act)
        self.assertNotIn("float radius = kSpreadSettledDistance;", act)

    def test_a_live_spread_row_drops_the_stay_exemption(self):
        for text in (body(DUNGEON_TRIGGERS, "bool RaidSpreadNeededTrigger::IsActive()"),
                     body(DUNGEON_ACTIONS, "bool RaidSpreadAction::Execute(Event& event)")):
            self.assertIn("BossSpreadLive(ai, bot)", text)
            self.assertIn('!spreadLive && ai->HasStrategy("stay"', text)

    def test_the_step_lands_in_the_stay_spot(self):
        act = body(DUNGEON_ACTIONS, "bool RaidSpreadAction::Execute(Event& event)")
        self.assertIn("BossSpreadLive(ai, bot)", act)
        self.assertIn('ai::PositionMap& posMap = ai->GetAiObjectContext()->GetValue<ai::PositionMap&>("position")->Get();', act)
        self.assertIn('posMap["stay"].Set(', act)

    def test_the_row_lookup_is_the_largest_live_param(self):
        live = body(ACTIONS, "bool ai::BossSpreadLive(")
        self.assertIn("dungeonRunBossSpread", live)
        self.assertIn("HasSpreadMap(bot->GetMapId())", live)
        self.assertIn("BossTacticKind::Spread", live)
        spread = body(ACTIONS, "float ai::BossSpreadYd(")
        self.assertIn("BossSpreadLive(ai, bot)", spread)
        self.assertIn("WidestSpreadYd(params, fallback)", spread)
        self.assertIn("WidestSpreadYd(", TACTICS_POLICY)


class KeyConsumerWiring(unittest.TestCase):
    """Both keys of this worker now have a consumer, so their allowlist lines are gone."""

    def test_the_hakkar_allowlist_lines_are_all_dropped(self):
        # Every Hakkar key has a consumer once the seek, spread, charm yield and pull gate kinds have landed.
        allow = body(CONFIG_USAGE, "ALLOWLIST = {", "\n}\n")
        for key in ("dungeonRunSeekAura", "dungeonRunBossSpread", "dungeonRunPullGate", "dungeonRunCharmYield"):
            self.assertNotIn(key, allow)

    def test_the_keys_are_read_where_they_act(self):
        self.assertIn("sPlayerbotAIConfig.dungeonRunSeekAura", ACTIONS)
        self.assertIn("sPlayerbotAIConfig.dungeonRunBossSpread", ACTIONS)


class DeclarationsMatchDefinitions(unittest.TestCase):
    """Every declaration this worker adds has its definition and its creator."""

    def test_the_declarations_are_defined(self):
        pairs = (
            (ACTIONS_H, "bool IsSoughtCloudFor(PlayerbotAI* ai, Player* bot, DynamicObject* dyn);",
             ACTIONS, "bool ai::IsSoughtCloudFor(PlayerbotAI* ai, Player* bot, DynamicObject* dyn)"),
            (ACTIONS_H, "SeekTask FindBossSeek(PlayerbotAI* ai, Player* bot);",
             ACTIONS, "SeekTask ai::FindBossSeek(PlayerbotAI* ai, Player* bot)"),
            (ACTIONS_H, "bool BossSeeking(PlayerbotAI* ai, Player* bot);",
             ACTIONS, "bool ai::BossSeeking(PlayerbotAI* ai, Player* bot)"),
            (ACTIONS_H, "float BossSpreadYd(PlayerbotAI* ai, Player* bot, float fallback);",
             ACTIONS, "float ai::BossSpreadYd(PlayerbotAI* ai, Player* bot, float fallback)"),
            (ACTIONS_H, "bool BossSpreadLive(PlayerbotAI* ai, Player* bot);",
             ACTIONS, "bool ai::BossSpreadLive(PlayerbotAI* ai, Player* bot)"),
            (ACTIONS_H, "Unit* FindBossSeekKill(PlayerbotAI* ai, Player* bot);",
             ACTIONS, "Unit* ai::FindBossSeekKill(PlayerbotAI* ai, Player* bot)"),
            (ACTIONS_H, "SeekTankTask FindBossSeekTank(PlayerbotAI* ai, Player* bot);",
             ACTIONS, "SeekTankTask ai::FindBossSeekTank(PlayerbotAI* ai, Player* bot)"),
            (ACTIONS_H, "class BossTacticsSeekAction : public MovementAction",
             ACTIONS, "bool BossTacticsSeekAction::Execute(Event& event)"),
            (ACTIONS_H, "class BossTacticsSeekTankAction : public AttackAction",
             ACTIONS, "bool BossTacticsSeekTankAction::Execute(Event& event)"),
            (ACTIONS_H, "class BossTacticsSeekKillAction : public AttackAction",
             ACTIONS, "bool BossTacticsSeekKillAction::Execute(Event& event)"),
            (ACTIONS_H, "class BossTacticsSeekHoldMultiplier : public Multiplier",
             ACTIONS, "float BossTacticsSeekHoldMultiplier::GetValue(Action* action)"),
        )
        for header, declaration, source, definition in pairs:
            self.assertIn(declaration, header)
            self.assertIn(definition, source)


if __name__ == "__main__":
    unittest.main()
