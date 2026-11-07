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
   the wrong time (SeekHoldDecision);
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
SPELL_ACTIONS = (ROOT / "ai/playerbot/strategy/actions/GenericSpellActions.cpp").read_text()
SPELL_ACTIONS_H = (ROOT / "ai/playerbot/strategy/actions/GenericSpellActions.h").read_text()
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
        for rule in ("SeekCycleDueMs(", "SeekCycleAccepts(", "SeekWindowOpen(", "SeekForceBurn(", "SeekWanted(",
                     "IsSoughtCloud(", "SeekCloudReachable(", "SeekGatherReady(", "SeekHoldDecision(", "SeekFinishableInReach(", "SeekLatchedGatherSource(", "SeekClassPetMustLeave(",
                     "SeekTankPullDue(", "SeekActionConsumesTick(", "SeekLatchedGatherSource(", "SeekClassPetMustLeave(",
                     "SeekSourceSearchYd(", "SeekClockStartOver(", "SeekTankPullStep(", "SeekPullLatchKeeps(",
                     "SeekPullRoleOf(", "SeekPullHitExemptOnLatch(", "SeekRolledDueMs(", "SeekLateEngagedBurn(",
                     "SeekHealerClockTick(", "SeekHealerTakeGuid("):
            self.assertIn(rule, ACTIONS, rule)
        # SeekTakeFightingSonOnHealer is called by SeekHealerTakeGuid, the state policy's own rule.
        self.assertIn("SeekTakeFightingSonOnHealer(", STATE_POLICY)
        self.assertIn("kSeekLatencyMs", POLICY)
        self.assertIn("kSeekGatherMaxMs", POLICY)


class FightStateWiring(unittest.TestCase):
    """One record per fight, exported to the run leader, swept and erased on an evade."""

    def test_the_first_siphon_is_counted_from_the_combat_start(self):
        # The core's 90 s siphon timer only runs in combat, so cycle 1 is due a cycle after the boss is first seen in
        # combat with the group. The victim alone is not enough: a boss in combat with the group can be without one on
        # that tick, and then no due time exists and cycle 1 gets no window.
        read = body(ACTIONS, "bool ReadSeekFight(")
        self.assertIn("record.engageMs == 0 && (boss->IsInCombat() || boss->GetVictim())", read)
        self.assertIn("SeekCycleDueMs(record.engageMs, record.lastCycleMs, cycleMs)", read)
        # The exported fight state times cycle 1 the same way for the run leader.
        self.assertIn("SeekCycleDueMs(record.engageMs, record.lastCycleMs, record.cycleMs)", ACTIONS)

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

    def test_the_gather_latch_survives_a_forced_burn(self):
        # Two Sons near the spot and damage spread between them: the record's pick holds while its Son lives inside
        # the reach, whatever the search radius or the forced burn, so the gather does not flip every read.
        read = body(ACTIONS, "bool ReadSeekFight(")
        self.assertIn("SeekLatchedGatherSource(latchInReach, gatherGuid, atSpot)", read)
        self.assertNotIn("out.forceBurn || belowHold ? 0 : gatherGuid", read)
        self.assertIn("kSeekFallbackYd", read)

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
        # The pet side and the boss's own auras, read again two seconds later.
        self.assertIn("pets_heal=%u,pets_dmg=%u", ACTIONS)
        self.assertIn("totems=%u,guardians=%u", ACTIONS)
        self.assertIn("CountSiphonSummons(bot, totems, guardians)", ACTIONS)
        self.assertIn("hakkar_stacks=%u,", ACTIONS)
        self.assertIn("hakkar_dmg=%u", ACTIONS)
        self.assertIn("kSeekSiphonSecondMs", ACTIONS)
        # A third read nine seconds after the sighting sees the whole 8 s heal, which the +1/+3 reads under-count.
        self.assertIn("kSeekSiphonThirdMs = 9000", ACTIONS)
        self.assertIn("record.siphonThirdAtMs = nowMs + kSeekSiphonThirdMs;", ACTIONS)
        self.assertIn("pendingSiphonAtSec = 9;", ACTIONS)
        # The note's clock is the real elapsed time since the cycle's first sighting, and the +3 s read is kept beside
        # the +1 s one instead of overwriting the run line's counts.
        self.assertIn("SeekSiphonAtLabel(elapsedMs)", ACTIONS)
        self.assertIn("record.siphonSeenMs = nowMs;", ACTIONS)
        self.assertIn("pendingSiphonElapsedMs = nowMs - record.siphonSeenMs;", ACTIONS)
        self.assertIn("SeekStoreSiphonRead(it->second", ACTIONS)


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
        # The third off-tank owns the job, and with fewer than three tanks alive the off-tank takes the Son over while
        # the raid's nearest hunter (else a ranged dealer) pulls it.
        self.assertIn("GetRaidTankRoles(ai)", find)
        self.assertIn("tanks[2]", find)
        self.assertIn("tanks[1]", find)
        self.assertIn("FindSeekFallbackPuller(", find)
        self.assertIn("SeekPullRoleOf(", POLICY)
        # The pull rule and the gather radius are the policy's, and the pull's pick (latch included) is the read's.
        self.assertIn("SeekTankPullDue(", body(ACTIONS, "bool ReadSeekFight("))
        self.assertIn("state.pullTarget", find)
        # The pickup search and the drag's arrival are the policy's own distances.
        self.assertIn("kSeekPickupYd", find)
        self.assertIn("kSeekTankArriveYd", find)
        # The pull branch walks the Son down itself; the attack order alone never moved the tank (it returns true with
        # the target set, so the rotation went back to the boss and the Son stayed out of the fight).
        self.assertIn("SeekTankPullStep(", tank)
        self.assertIn("SeekPullStep::Walk", tank)
        self.assertIn("MoveTo(bot->GetMapId(), x, y, z, false, false, false, true)", tank)
        self.assertIn("DoSpecificAction(\"shoot\", event, true)", tank)
        # The gather's own radius comes from the spot's read; without the third tank the search widens to the
        # fallback (M3).
        read = body(ACTIONS, "bool ReadSeekFight(")
        self.assertIn("SeekSourceSearchYd(roles.tanks.size() >= 3, out.forceBurn, belowHold, engagedSon)", read)
        # A Son already fighting inside the reach is the pick wherever it stands (11-25 yd from the spot on the
        # healers was never selected at the spot's radius).
        self.assertIn("engagedSon = true", read)
        self.assertIn("unit->IsInCombat() && reachYd <= kSeekFallbackYd", read)

    def test_the_pull_walk_moves_the_tank(self):
        # The Walk step is the tank's own movement: the branch calls the same MoveTo the drag back to B uses, aimed at
        # the Son, and takes the tick. Attack alone returns true with the target set, so the rotation went back to the
        # boss and the tank never left the spot (walk 41: 27-54 yd to the Son in every undosed window).
        tank = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(")
        pull = tank[tank.index("if (task.pull)"):tank.index("if (task.walkToSpot)")]
        self.assertIn("SeekTankPullStep(task.source->IsInCombat(), task.source->GetVictim() == bot, yd)", pull)
        # The Walk step's own MoveTo (the anchored ranged shot's walk to its stand comes before it in the branch).
        walk = pull.index("if (step == SeekPullStep::Walk)")
        self.assertGreater(pull.find("MoveTo(bot->GetMapId(), x, y, z", walk), walk)
        self.assertIn("MoveTo(bot->GetMapId(), x, y, z, false, false, false, true)", pull)
        self.assertIn("return Attack(bot, task.source);", pull)  # the Hit step
        self.assertIn("LogSeekPull(ai, bot, task.source, yd, step)", pull)
        self.assertIn('"seek pull: %s -> %u %.1f yd %s"', ACTIONS)
        self.assertIn("BossTacticsSeekPull", ACTIONS)
        self.assertIn("kSeekPullLatchYd", POLICY)
        self.assertIn("kSeekPullLatchMs", POLICY)
        self.assertIn("kSeekPullHitYd", POLICY)

    def test_the_late_engaged_son_forces_the_burn(self):
        # A picked Son that joins the fight with the siphon close is burnt at once: the read forces the burn from the
        # pick, and the gather's own logic then spends it (SeekLateEngagedBurn).
        read = body(ACTIONS, "bool ReadSeekFight(")
        self.assertIn("SeekLateEngagedBurn(pickedSon && pickedSon->IsInCombat(), dueMs, out.dueInMs)", read)
        self.assertIn("out.forceBurn = true;", read)
        self.assertIn("inline bool SeekLateEngagedBurn(", POLICY)

    def test_the_healer_take_is_wired(self):
        # A Son camped on a non-tank is tanks[1]'s once the third off-tank cannot take it, timed in the fight record.
        read = body(ACTIONS, "bool ReadSeekFight(")
        # The Sons it judges are the fight-wide set (the "sole kill candidates" grid search plus the source walk), never
        # fight.units alone, which "focus rti targets" collapses to the raid mark.
        self.assertIn("SoleKillCandidates(ai, row->targetEntry)", read)
        self.assertNotIn("for (Unit* unit : fight.units)\n    {\n        if (unit->GetEntry() != row->targetEntry || !unit->IsInCombat())", read)
        # One read never erases a Son it did not see: the clock is the pure SeekHealerClockTick.
        self.assertIn("SeekHealerClockTick(record.healerHeldSince, seenSons, nowMs);", read)
        self.assertNotIn("record.healerHeldSince.erase(", read)
        self.assertIn("SeekHealerTakeGuid(record.healerHeldSince, nowMs)", read)
        # The Son is resolved from the map, not from this bot's attackers list.
        self.assertIn("GetCreature(ObjectGuid(healerTakeGuid))", read)
        self.assertIn("out.healerSon = son;", read)
        find = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(")
        self.assertIn("roles.tanks.size() >= 3 && roles.tanks[1] == bot", find)
        self.assertIn("if (state.healerSon)", find)
        self.assertIn("SeekTakeFightingSonOnHealer(", POLICY)
        self.assertIn("healerHeldSince", STATE_POLICY)

    def test_the_threat_cap_row_logs_the_off_tanks_target(self):
        # tanks[1] stalling on a Son stalls the cap reference: the row names what the first off-tank is fighting.
        values = (ROOT / "ai/playerbot/strategy/values/ThreatValues.cpp").read_text()
        self.assertIn("offtank_target=%s", values)
        self.assertIn("offTankPlayer && offTankPlayer->IsInWorld() ? offTankPlayer->GetVictim() : nullptr", values)
        self.assertIn("offTankVictim && offTankVictim->IsInWorld() ? offTankVictim : nullptr", values)
        self.assertIn("LogThreatCap(ai, ai->GetBot(), pick, offTankTarget);", values)

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
        # Every source but the selected one is held all fight, and the selected one obeys SeekHoldDecision.
        self.assertIn("SeekHoldsUnit(state, state.source)", kill)
        # The kill pick asks for the window before the pick: with it shut the Son is held at whatever health it has.
        self.assertIn("!state.windowOpen", kill)
        self.assertLess(kill.index("!state.windowOpen"), kill.index("SeekHoldsUnit(state, state.source)"))

    def test_the_sources_are_held_by_the_dps_hold_multiplier(self):
        hold = body(ACTIONS, "float BossTacticsHoldMultiplier::GetValue(")
        self.assertIn("SeekHoldsUnit(state, source)", hold)
        self.assertIn("seeksRow", hold)

    def test_only_the_pulled_son_is_exempt_from_the_hold(self):
        # The puller's identity and its one Son are read once per tick with the rest of the seek fight (the tank roles
        # are rebuilt there, not in every action's hold check), and the exemption names that Son: another Son and every
        # non-puller keep the hold. A DoT is checked before the exemption, so its ticks never run through the hold on
        # the pick.
        read = body(ACTIONS, "bool ReadSeekFight(")
        self.assertIn("const RaidTankRoles roles = GetRaidTankRoles(ai);", read)
        self.assertIn("out.puller = (thirdTank && !thirdTankIsBossVictim) || fallback;", read)
        # The pick is the running pull's latch while it holds (the Son patrols past the pickup radius), else the
        # tick's due pickup.
        self.assertIn("SeekPullLatchKeeps(true, latchedPull->GetDistance2dToCenter(row->spotX, row->spotY),", read)
        self.assertIn("out.pullTarget = latchedPull;", read)
        self.assertIn("out.pullTarget = pullDue ? nearest : nullptr;", read)
        self.assertNotIn("IsSeekPuller(", ACTIONS)
        hold = body(ACTIONS, "float BossTacticsHoldMultiplier::GetValue(")
        self.assertIn("SeekPullHitExemptOnLatch(state.puller, struck == state.pullTarget)", hold)
        self.assertLess(hold.index("SeekDotOnSource(state, action)"), hold.index("SeekPullHitExemptOnLatch("))
        drop = body(ACTIONS, "Unit* ai::FindBossHoldKill(")
        self.assertIn("SeekPullHitExemptOnLatch(state.puller, current == state.pullTarget)", drop)
        # The latch is the same record the hold reads, so the exemption outlives the tick's search and the Son joining
        # the fight.
        self.assertIn("inline bool SeekPullHitExemptOnLatch(bool puller, bool latchedTarget)", POLICY)
        self.assertIn("pullTargetGuid", STATE_POLICY)

    def test_the_fallback_pull_is_the_auto_shot_and_casts_no_dot(self):
        # The fallback puller's one hit must leave nothing ticking on the Son: a DoT's ticks run through the hold and
        # the shut window and kill the source the next siphon needs. The opener is the shoot action's weapon hit (the
        # classic auto shot for a bow, gun or crossbow, the wand's "shoot" otherwise) and the branch casts no spell at
        # all, so no periodic aura can be applied; Serpent Sting, the hunter's own class pull, is never cast here.
        tank = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(")
        pull = tank[tank.index("if (task.rangedPull)"):tank.index("const float yd =")]
        self.assertNotIn("serpent sting", pull.lower())
        self.assertNotIn("CastSpell(", pull)
        self.assertNotIn("CanCastSpell(", pull)
        self.assertIn('DoSpecificAction("shoot", event, true)', pull)
        # The shoot action is the codebase's ranged auto attack: it resolves the equipped weapon's own shot.
        self.assertIn('CastSpellAction(ai, "shoot")', SPELL_ACTIONS_H)
        update = body(SPELL_ACTIONS, "void CastShootAction::UpdateWeaponInfo()")
        for token in ('spellName += " gun"', 'spellName += " bow"', 'spellName += " crossbow"',
                      'spellName = "throw"'):
            self.assertIn(token, update)
        self.assertIn("CURRENT_AUTOREPEAT_SPELL", body(SPELL_ACTIONS, "bool CastShootAction::Execute("))

    def test_the_dot_and_off_tank_holds_answer_every_damage_cast(self):
        # A melee debuff on the attacker ("rend on attacker") resolves to "attacker without aura", so it reaches the
        # seek block through the every-damage-cast clause, and the classification is the policy's.
        hold = body(ACTIONS, "float BossTacticsHoldMultiplier::GetValue(")
        self.assertIn("(dpsAssist || aoe || attackerDebuff || area || seekSingle || seekDot)", hold)
        self.assertIn("SeekDotVeto(", ACTIONS)
        # The off-tank hold takes the tick only to stop a swing that is running on the Son; with none running the
        # rotation (Shield Block, the shouts, the taunt) gets it.
        tank = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(")
        self.assertIn("SeekTankHoldConsumesTick(", tank)
        self.assertIn("SeekTankHoldsSwing(", body(ACTIONS, "float BossTacticsSeekHoldMultiplier::GetValue("))

    def test_the_totems_leave_the_siphon(self):
        # Shaman totems are one more undosed unit each: unsummoned in the same window as the pets through the core's
        # own sweep, and no placement is allowed until the siphon has landed.
        node = '"boss tactics seek totem"'
        combat = body(STRATEGY, "void BossTacticsStrategy::InitCombatTriggers(")
        self.assertIn(node, combat)
        self.assertIn('new NextAction("boss tactics seek totem", ACTION_EMERGENCY + 6)', combat)
        self.assertIn('creators["boss tactics seek totem"]', ACTION_CONTEXT)
        self.assertIn('creators["boss tactics seek totem"]', TRIGGER_CONTEXT)
        act = body(ACTIONS, "bool BossTacticsSeekTotemAction::Execute(")
        self.assertIn("BossSeekTotemsMustLeave(ai, bot)", act)
        self.assertIn("bot->UnsummonAllTotems()", act)
        hold = body(ACTIONS, "float BossTacticsSeekPetHoldMultiplier::GetValue(")
        self.assertIn("SeekTotemSpell(cast->GetSpellName())", hold)
        self.assertIn("SeekPetCallAction(action->getName())", hold)
        # A warlock's demon summons and its Infernal/Doomguard are held for the whole fight, not just the window.
        self.assertIn("BossSeekPetMustLeave(ai, bot) ? 0.0f : 1.0f", hold)
        self.assertIn("inline bool SeekTotemSpell(", POLICY)
        self.assertIn("inline bool SeekGuardianSummonAction(", POLICY)

    def test_the_pet_dismissal_is_wired(self):
        # A pet is one more heal to the core's hit: the action dismisses it in the seek fight, the multiplier keeps the
        # class from calling or summoning it back before the siphon has landed.
        node = '"boss tactics seek pet"'
        combat = body(STRATEGY, "void BossTacticsStrategy::InitCombatTriggers(")
        self.assertIn(node, combat)
        self.assertIn('new NextAction("boss tactics seek pet", ACTION_EMERGENCY + 6)', combat)
        self.assertIn('creators["boss tactics seek pet"]', ACTION_CONTEXT)
        self.assertIn('creators["boss tactics seek pet"]', TRIGGER_CONTEXT)
        self.assertIn("new BossTacticsSeekPetHoldMultiplier(ai)",
                      body(STRATEGY, "void BossTacticsStrategy::InitCombatMultipliers("))
        self.assertIn("SeekPetMustLeave(", ACTIONS)
        self.assertIn("PET_SAVE_AS_CURRENT", body(ACTIONS, "bool BossTacticsSeekPetAction::Execute("))


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
            (ACTIONS_H, "bool BossSeekPetMustLeave(PlayerbotAI* ai, Player* bot);",
             ACTIONS, "bool ai::BossSeekPetMustLeave(PlayerbotAI* ai, Player* bot)"),
            (ACTIONS_H, "class BossTacticsSeekPetAction : public Action",
             ACTIONS, "bool BossTacticsSeekPetAction::Execute(Event&"),
            (ACTIONS_H, "class BossTacticsSeekPetHoldMultiplier : public Multiplier",
             ACTIONS, "float BossTacticsSeekPetHoldMultiplier::GetValue(Action* action)"),
        )
        for header, declaration, source, definition in pairs:
            self.assertIn(declaration, header)
            self.assertIn(definition, source)


class SeekReachFromAnchor(unittest.TestCase):
    # With the boss's anchor row the seek's reach is the nearer of B and the anchor's centre (SeekReachYd): a Son the
    # walk-out brings to the anchor's far edge stands 37-38 yd from B and was held through the siphon.
    def test_the_reach_reads_the_anchor_centre(self):
        reach = body(ACTIONS, "float SeekReachOf(const SeekFight& state, Unit* unit)")
        self.assertIn("SeekReachYd(fromSpotYd, fromAnchorYd, state.anchored, state.anchorRadius)", reach)
        read = body(ACTIONS, "bool ReadSeekFight(")
        self.assertIn("out.anchored = BossAnchorOfBoss(bot->GetMapId(), row->bossEntry, false, reachAnchor);", read)
        self.assertIn("out.anchorRadius = reachAnchor.radius;", read)
        self.assertLess(read.index("out.anchored = BossAnchorOfBoss("), read.index("const float reachYd = SeekReachOf(out, unit);"))
        self.assertIn("SeekFinishableInReach(unit->IsInCombat(), unit->GetHealthPercent(), holdPct, reachYd, out.seekers)",
                      read)
        self.assertIn("anchorReach ? SeekReachOf(out, candidate)", read)
        self.assertIn("SeekReachOf(out, latched) <= kSeekFallbackYd", read)
        hold = body(ACTIONS, "bool SeekHoldsUnit(const SeekFight& state, Unit* unit)")
        self.assertIn("SeekReachOf(state, unit)", hold)
        self.assertNotIn("distFromSpotYd", hold)


if __name__ == "__main__":
    unittest.main()
