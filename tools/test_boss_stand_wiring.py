"""Wiring pins for the boss stand sets (ai_boss_stand, the generic seat engine).

The rules live in ai/playerbot/BossStandPolicy.h (pure, tested by
tools/test_boss_stand_policy.cpp). These pins prove the live code calls them:

 - the loader reads every column of ai_boss_stand (the migration's) at startup;
 - the translation (FindBossStand) gates on the map, the armed "boss tactics"
   strategy, the boss's cast and aura, and seats the bot among the bots of its
   group with the same role;
 - the stand action snaps the seat to the ground (a move whose z is off the
   floor is silently ignored by the movement code), stops the bot's cast and
   attack, and moves once;
 - the stand wins over the caster reach (no walk back under the boss while a row
   seats the bot) and over the dragon flank and raid spread reactions;
 - no boss name or entry in the generic code.

Run: python3 -m unittest tools/test_boss_stand_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
AI = ROOT / "ai/playerbot"
STRATEGY = AI / "strategy"
POLICY = (AI / "BossStandPolicy.h").read_text()
STAND_H = (AI / "BossTacticsStand.h").read_text()
MGR_H = (AI / "BossStandMgr.h").read_text()
MGR_CPP = (AI / "BossStandMgr.cpp").read_text()
CONFIG = (AI / "PlayerbotAIConfig.cpp").read_text()
ACTIONS_H = (STRATEGY / "actions/BossTacticsActions.h").read_text()
ACTIONS = (STRATEGY / "actions/BossTacticsActions.cpp").read_text()
TRIGGERS = (STRATEGY / "triggers/BossTacticsTriggers.cpp").read_text()
TRIGGERS_H = (STRATEGY / "triggers/BossTacticsTriggers.h").read_text()
STRATEGY_CPP = (STRATEGY / "generic/BossTacticsStrategy.cpp").read_text()
ACTION_CONTEXT = (STRATEGY / "actions/ActionContext.h").read_text()
TRIGGER_CONTEXT = (STRATEGY / "triggers/TriggerContext.h").read_text()
REACH = (STRATEGY / "actions/ReachTargetActions.h").read_text()
MOVEMENT_ACTIONS = (STRATEGY / "actions/MovementActions.cpp").read_text()
MOVEMENT_ACTIONS_H = (STRATEGY / "actions/MovementActions.h").read_text()
DUNGEON_ACTIONS = (STRATEGY / "actions/DungeonActions.cpp").read_text()
DUNGEON_ACTIONS_H = (STRATEGY / "actions/DungeonActions.h").read_text()
MIGRATIONS = sorted((ROOT / "data/sql/world").glob("*_world.sql"))


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


def stand_migration():
    for path in MIGRATIONS:
        text = path.read_text()
        if "CREATE TABLE IF NOT EXISTS `ai_boss_stand`" in text:
            return text
    raise AssertionError("no migration creates ai_boss_stand")


class BossStandWiring(unittest.TestCase):
    def test_loader_reads_every_column_of_the_table(self):
        create = body(stand_migration(), "CREATE TABLE IF NOT EXISTS `ai_boss_stand`", end="\n) ENGINE")
        columns = re.findall(r"^  `(\w+)`", create, re.M)
        columns.remove("comment")  # documentation only
        select = body(MGR_CPP, "std::uint32_t ai::ReadBossStandRows(", end="FROM `ai_boss_stand`")
        self.assertEqual(re.findall(r"`(\w+)`", select), columns)
        for index in range(len(columns)):
            self.assertIn("fields[%d]" % index, MGR_CPP)

    def test_loader_runs_at_startup_next_to_the_tactics(self):
        self.assertIn("ai::LoadBossStands();", CONFIG)
        self.assertLess(CONFIG.index("ai::LoadBossTactics();"), CONFIG.index("ai::LoadBossStands();"))
        self.assertIn("BossStandTable& GetBossStands();", MGR_H)
        fn = body(MGR_CPP, "void ai::LoadBossStands()")
        self.assertIn("if (table.Sealed())\n        return;", fn)
        self.assertIn("ReadBossStandRows(table)", fn)
        # The reader seals even an empty or missing table.
        self.assertIn("table.Seal();", body(MGR_CPP, "std::uint32_t ai::ReadBossStandRows("))

    def test_translation_gates_before_it_scans(self):
        fn = body(ACTIONS, "BossStandTarget ai::FindBossStand(")
        has_map = fn.index("table.HasMap(mapId)")
        strategy = fn.index('ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT)')
        role = fn.index("StandRoleOf(PlayerbotAI::IsTank(bot), PlayerbotAI::IsHeal(bot), ai->IsRanged(bot))")
        attackers = fn.index('AI_VALUE(std::list<ObjectGuid>, "attackers")')
        self.assertLess(has_map, strategy)
        self.assertLess(strategy, role)
        self.assertLess(role, attackers)

    def test_translation_reads_the_boss_cast_and_aura(self):
        fn = body(ACTIONS, "BossStandTarget ai::FindBossStand(")
        self.assertIn("table.Rows(mapId, unit->GetEntry())", fn)
        self.assertIn("unit->GetCurrentSpell(CURRENT_GENERIC_SPELL)", fn)
        self.assertIn("spell->m_spellInfo->Id", fn)
        self.assertIn("StandPointsNow(*rows, role, unit->GetPositionX(), unit->GetPositionY(), casting,", fn)
        self.assertIn("unit->HasAura(aura)", fn)

    def test_the_seat_outlives_the_cast_through_a_per_bot_hold(self):
        fn = body(ACTIONS, "BossStandTarget ai::FindBossStand(")
        # The hold is read before the scan, handed to the policy with the clock, and written back after it.
        read = fn.index("hold = it->second;")
        policy = fn.index("hold, nowMs, stoppedNoTarget);")
        write = fn.index("g_standHolds[selfKey] = hold;")
        self.assertLess(read, policy)
        self.assertLess(policy, write)
        self.assertIn("const uint32 nowMs = WorldTimer::getMSTime();", fn)
        self.assertIn("const uint64 selfKey = bot->GetObjectGuid().GetRawValue();", fn)
        self.assertIn("std::lock_guard<std::mutex> lock(g_standHoldMutex);", fn)
        self.assertEqual(fn.count("std::lock_guard<std::mutex> lock(g_standHoldMutex);"), 6)
        # A spent hold is dropped (no entry left behind for a bot that never sat again).
        self.assertIn("g_standHolds.erase(selfKey);", fn)
        # A tick without the boss keeps the hold only until its time is up.
        self.assertIn("(sawBoss || static_cast<uint32>(nowMs - hold.lastOpenMs) < hold.holdMs)", fn)

    def test_the_window_opens_on_the_state_a_triggered_cast_leaves(self):
        fn = body(ACTIONS, "BossStandTarget ai::FindBossStand(")
        # The cast is read first; the state is the second way in, read from the boss unit itself.
        self.assertLess(fn.index("unit->GetCurrentSpell(CURRENT_GENERIC_SPELL)"), fn.index("stoppedNoTarget"))
        self.assertIn("const bool stoppedNoTarget = unit->IsStopped() && unit->GetTargetGuid().IsEmpty();", fn)
        self.assertLess(fn.index("const bool stoppedNoTarget"), fn.index("StandPointsNow("))
        # The same signal the reactive dodge reads (OnyxiasLairDungeonTriggers.h).
        dodge = (STRATEGY / "triggers/OnyxiasLairDungeonTriggers.h").read_text()
        self.assertIn("attacker->IsStopped()", dodge)
        self.assertIn("attacker->GetTargetGuid().IsEmpty()", dodge)

    def test_the_side_is_the_side_of_the_lane(self):
        fn = body(ACTIONS, "BossStandTarget ai::FindBossStand(")
        self.assertIn("const StandLane lane = bossRows ? StandLaneOf(*bossRows, *points[0]) : StandLane();", fn)
        self.assertIn("bossRows = rows;", fn)
        # The policy no longer sends a bot across the lane when its own side is full.
        policy = body(POLICY, "inline StandSlot AssignStandSlot(", end="\n    }\n")
        self.assertNotIn("side = side == 0 ? 1 : 0;", policy)
        self.assertIn("lane.dx * (bot.y - points[0]->anchorY) - lane.dy * (bot.x - points[0]->anchorX)", policy)

    def test_a_refused_seat_is_given_up_after_three_tries(self):
        fn = body(ACTIONS, "BossStandTarget ai::FindBossStand(")
        self.assertIn("StandSeatGivenUp(failure->second, seat.x, seat.y)", fn)
        action = body(ACTIONS, "bool BossTacticsStandAction::Execute(Event& event)")
        self.assertIn("StandNoteMoveFailure(g_standFailures[key], stand.x, stand.y);", action)
        self.assertLess(action.index("MoveTo("), action.index("StandNoteMoveFailure("))
        self.assertIn("g_standFailures.erase(failure);", action)

    def test_the_heal_walk_gives_way_to_the_seat(self):
        fn = body(REACH, "class ReachPartyMemberToHealAction", end="\n    };\n")
        self.assertIn("if (FindBossStand(ai, bot).valid)\n                return false;\n            return ReachTargetAction::MoveIntoRange(target, distance);", fn)

    def test_a_dead_or_gone_bot_forgets_its_window(self):
        fn = body(ACTIONS, "BossStandTarget ai::FindBossStand(")
        dead = fn.index("!bot->IsInWorld() || bot->IsBeingTeleported() || !sServerFacade.IsAlive(bot)")
        self.assertLess(dead, fn.index("ForgetStand(selfKey);"))
        self.assertGreaterEqual(fn.count("ForgetStand(selfKey);"), 3)  # dead/teleporting, no rows on the map, strategy off
        forget = body(ACTIONS, "void ForgetStand(uint64 key)")
        self.assertIn("g_standHolds.erase(key);", forget)
        self.assertIn("g_standFailures.erase(key);", forget)
        # The cheap path costs one atomic read.
        self.assertIn("g_standStateCount.load(std::memory_order_relaxed) == 0", forget)

    def test_a_hold_reads_only_its_own_boss(self):
        fn = body(ACTIONS, "BossStandTarget ai::FindBossStand(")
        self.assertIn("if (hold.armed && hold.bossEntry != 0 && unit->GetEntry() != hold.bossEntry)\n            return false;", fn)
        self.assertLess(fn.index("unit->GetEntry() != hold.bossEntry"), fn.index("table.Rows(mapId, unit->GetEntry())"))

    def test_a_stand_set_alone_arms_the_strategy(self):
        self.assertIn("return GetBossTactics().HasMap(mapId) || GetBossStands().HasMap(mapId);", TRIGGERS)
        self.assertEqual(TRIGGERS.count("MapHasRows(bot->GetMapId())"), 2)
        self.assertNotIn("GetBossTactics().HasMap(bot->GetMapId())", TRIGGERS)

    def test_the_loader_logs_a_refused_stand_set(self):
        fn = body(MGR_CPP, "std::uint32_t ai::ReadBossStandRows(")
        seal = fn.index("table.Seal();")
        refused = fn.index("table.Refused()")
        self.assertLess(seal, refused)
        self.assertIn("refused as a whole", fn)

    def test_translation_seats_among_the_armed_bots_of_the_same_role(self):
        fn = body(ACTIONS, "BossStandTarget ai::FindBossStand(")
        self.assertIn("LiveGroupMembers(group)", fn)
        self.assertIn("PlayerbotAIStorage::Instance().GetAI(member)", fn)
        self.assertIn('memberAi->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT)', fn)
        self.assertIn("StandRoleOf(PlayerbotAI::IsTank(member), PlayerbotAI::IsHeal(member), ai->IsRanged(member)) != role", fn)
        # The bot itself is judged by the same rule as the others, first.
        self.assertLess(fn.index("const StandCandidate self = candidateOf(bot, heldBefore);"), fn.index("LiveGroupMembers(group)"))
        self.assertIn("selfCounts = StandMemberCounts(self, anchorX, anchorY);", fn)

    def test_only_bots_in_the_fight_with_the_boss_take_seats(self):
        fn = body(ACTIONS, "BossStandTarget ai::FindBossStand(")
        # One rule for every bot that evaluates the plan: engaged with THIS boss (its threat list or its victim) in
        # range of the anchor, or holding a stand for the same boss and anchor.
        self.assertIn("StandMemberEngaged(boss->GetThreatManager().getThreat(member) > 0.0f,", fn)
        self.assertIn("memberHold.armed && memberHold.bossEntry == bossEntry && memberHold.anchor == anchor", fn)
        self.assertIn("bots = SelectStandBots(candidates, anchorX, anchorY);", fn)
        # The anchor is the one the open window picked, and the boss the one that owns it.
        self.assertIn("const float anchorX = points[0]->anchorX;", fn)
        self.assertIn("boss = unit;", fn)
        # Every member candidate is built by the one lambda; nobody is pushed unchecked.
        self.assertNotIn("bots.push_back(", fn)
        # The others read the hold the member left; the self-judgement reads its hold as of the last tick.
        self.assertIn("const StandHold heldBefore = hold;", fn)
        self.assertLess(fn.index("const StandHold heldBefore = hold;"), fn.index("StandPointsNow("))
        # A bot that does not count leaves no hold behind.
        self.assertIn("(boss ? selfCounts :", fn)

    def test_stand_action_snaps_the_seat_to_the_ground_before_the_move(self):
        fn = body(ACTIONS, "bool BossTacticsStandAction::Execute(Event& event)")
        probe = fn.index("const float probeZ = StandProbeZ(seatZ, bot->GetPositionZ());")
        snap = fn.index("bot->UpdateGroundPositionZ(seatX, seatY, z);")
        keep = fn.index("z = StandSnappedZ(seatZ, probeZ, z);")
        move = fn.index("MoveTo(bot->GetMapId(), seatX, seatY, z, false, false, false, true)")
        self.assertLess(probe, snap)
        self.assertLess(snap, keep)
        self.assertLess(keep, move)
        self.assertIn("float z = probeZ;", fn)
        # Never the boss's (flight) height.
        self.assertNotIn("boss->GetPositionZ", fn)

    def test_stand_action_stops_the_cast_and_moves_once(self):
        fn = body(ACTIONS, "bool BossTacticsStandAction::Execute(Event& event)")
        self.assertIn("bot->AttackStop();", fn)
        self.assertIn("bot->CastStop();", fn)
        self.assertLess(fn.index("bot->CastStop();"), fn.index("MoveTo("))
        # A bot within 6 yd of its seat keeps the cast it is in.
        self.assertIn("if (StandShouldStopCast(bot->GetPositionX(), bot->GetPositionY(), seatX, seatY))\n        bot->CastStop();", fn)
        self.assertIn("StandArrived(bot->GetPositionX(), bot->GetPositionY(), seatX, seatY)", fn)
        # The seat the bot walks to is the row's, through a live boss anchor's clamp (test_boss_anchor_wiring.py).
        self.assertIn("float seatX = stand.x;", fn)
        self.assertLess(fn.index("ClampMoveToBossAnchor(ai, bot, seatX, seatY, seatZ)"), fn.index("StandArrived("))
        # A bot already walking to this seat is not re-pathed.
        self.assertIn("!bot->IsStopped() && !bot->movespline->Finalized()", fn)
        possible = body(ACTIONS, "bool BossTacticsStandAction::isPossible()")
        self.assertIn("ai->CanMove()", possible)

    def test_trigger_fires_only_for_a_bot_off_its_seat(self):
        fn = body(TRIGGERS, "bool BossTacticsStandTrigger::IsActive()")
        self.assertIn("if (!Ready(ai, bot))\n        return false;", fn)
        self.assertIn("stand.valid && !StandArrived(", fn)
        self.assertIn('Trigger(ai, "boss tactics stand", 1)', TRIGGERS_H)

    def test_strategy_runs_the_stand_before_casting(self):
        match = re.search(r'"boss tactics stand",\s*NextAction::array\(0, new NextAction\("boss tactics stand", (\w+)\)', STRATEGY_CPP)
        self.assertIsNotNone(match)
        self.assertEqual(match.group(1), "ACTION_EMERGENCY")
        self.assertIn(
            'creators["boss tactics stand"] = [](PlayerbotAI* ai) { return new BossTacticsStandAction(ai); };', ACTION_CONTEXT)
        self.assertIn(
            'creators["boss tactics stand"] = [](PlayerbotAI* ai) { return new BossTacticsStandTrigger(ai); };', TRIGGER_CONTEXT)

    def test_the_stand_wins_over_the_caster_reach(self):
        fn = body(REACH, "class ReachSpellAction", end="\n    };\n")
        stand = fn.index("FindBossStand(ai, bot).valid")
        walk = fn.index("return ReachTargetAction::MoveIntoRange(target, distance);")
        self.assertLess(stand, walk)
        self.assertIn("if (FindBossStand(ai, bot).valid)\n                return false;", fn)

    def test_a_picked_movement_reaction_stops_the_bot(self):
        # The mechanism the isUseful gates answer: if this stops being true the gates may be moved back to Execute.
        engine = (AI / "strategy/ReactionEngine.cpp").read_text()
        find = body(engine, "bool ReactionEngine::FindReaction(bool isStunned)")
        self.assertLess(find.index("reaction->isUseful()"), find.index("incomingReaction.SetAction(reaction);"))
        ai_cpp = (AI / "PlayerbotAI.cpp").read_text()
        react = body(ai_cpp, "bool PlayerbotAI::UpdateAIReaction(")
        self.assertIn("if (reaction->ShouldInterruptMovement())", react)
        movement_h = (STRATEGY / "actions/MovementActions.h").read_text()
        self.assertIn("bool ShouldReactionInterruptMovement() const override { return true; }", movement_h)

    def test_a_groupless_bot_plans_alone(self):
        # Codex round 3: every groupless bot keyed 0 shared one plan, built from the first bot alone, so the others found
        # no seat. A groupless bot keys its plan by its own guid.
        fn = body(ACTIONS, "BossTandTarget ai::FindBossStand(".replace("Tand", "Stand"))
        self.assertIn("group ? group->GetLeaderGuid().GetRawValue() : bot->GetObjectGuid().GetRawValue()", fn)

    def test_the_seats_come_from_one_plan_per_window(self):
        # The seats of a window are planned when the window opens, grown by the bots that join the fight later and by
        # the seat a death frees, never reshuffled: a seat once given never moves while its bot lives, and every bot of
        # the role reads the same plan.
        fn = body(ACTIONS, "BossStandTarget ai::FindBossStand(")
        self.assertIn("PlanStandSeats(points, bots, lane)", fn)
        self.assertIn("StandPlanStale(nowMs, it->second.lastReadMs)", fn)
        self.assertIn("it->second.lastReadMs = nowMs;", fn)
        # A bot that engages after the window opened grows the plan in the else of the stale check (the window's plan
        # already exists and is fresh), under the same lock, before the read is stamped.
        call = ("else\n            // a bot engaged after the window opened, or one whose seat a death freed, "
                "gets a free seat\n            ExtendStandPlan(it->second.seats, points, bots, lane, dead);")
        self.assertIn(call, fn)
        self.assertLess(fn.index("plan.seats = PlanStandSeats(points, bots, lane);"),
                        fn.index("ExtendStandPlan(it->second.seats, points, bots, lane, dead);"))
        self.assertLess(fn.index("ExtendStandPlan(it->second.seats, points, bots, lane, dead);"),
                        fn.index("it->second.lastReadMs = nowMs;"))
        self.assertNotIn("AssignStandSlot(", fn)
        declared = body(ACTIONS, "// The seat plan of an open window",
                        end="std::unordered_map<StandPlanKey, StandPlan, StandPlanKeyHash> g_standPlans;")
        self.assertIn("std::unordered_map<StandPlanKey, StandPlan, StandPlanKeyHash> g_standPlans;", ACTIONS)
        self.assertIn("Guarded by g_standHoldMutex.", declared)
        self.assertIn("struct StandPlanKey", POLICY)
        # The plan is keyed by the boss instance (map and instance id, not only the spawn guid), the window's anchor,
        # role and window spell, and the bot's group; the live translation passes all of them; a stale plan is swept
        # whatever its key.
        self.assertIn("Group* group = bot->GetGroup();", fn)
        self.assertIn("const uint64 groupGuid = group ? group->GetLeaderGuid().GetRawValue() : ", fn)
        self.assertIn("const StandPlanKey key{ boss->GetObjectGuid().GetRawValue(), boss->GetMapId(), "
                      "boss->GetInstanceId(),\n                                points[0]->anchor, "
                      "static_cast<uint8>(role), points[0]->windowSpell, groupGuid };", fn)
        self.assertIn("old = StandPlanStale(nowMs, old->second.lastReadMs) ? g_standPlans.erase(old) : "
                      "std::next(old);", fn)

    def test_a_dead_group_member_frees_its_seat(self):
        # Only death frees a seat: the loop that builds the role's members hands the plan the ids of the group's dead
        # members, so the plan can hand their spots to the bots still fighting.
        fn = body(ACTIONS, "BossStandTarget ai::FindBossStand(")
        self.assertIn("std::vector<std::uint64_t> dead;", fn)
        self.assertIn("if (!sServerFacade.IsAlive(member))\n                {\n                    "
                      "dead.push_back(member->GetObjectGuid().GetRawValue());\n                    continue;\n                }", fn)
        self.assertIn("ExtendStandPlan(it->second.seats, points, bots, lane, dead);", fn)
        # A death frees a seat and nothing else does: the plan's seat of a member that is absent from `bots` but not
        # dead is left alone (ExtendStandPlan removes entries of `dead` alone).
        policy = body(POLICY, "inline bool ExtendStandPlan(", end="\n    }\n")
        self.assertIn("if (!dead.empty())", policy)
        self.assertIn("std::find(dead.begin(), dead.end(), seat.id) != dead.end()", policy)

    def test_the_plan_is_filed_by_window_spell_and_group(self):
        # Two rows of one anchor and role can open on different window spells inside the idle time: the plan of one
        # window must not seat a bot of the other. The group keeps a bot outside the raid from reading the raid's plan.
        key = body(POLICY, "struct StandPlanKey", end="\n    };\n")
        self.assertIn("std::uint32_t windowSpell = 0;", key)
        self.assertIn("std::uint64_t group = 0;", key)
        self.assertIn("windowSpell == other.windowSpell", key)
        self.assertIn("group == other.group", key)
        hashed = body(POLICY, "struct StandPlanKeyHash", end="\n    };\n")
        self.assertIn("key.windowSpell", hashed)
        self.assertIn("key.group", hashed)

    def test_the_other_movement_reactions_give_way_to_the_seat(self):
        # The reaction engine (test_a_picked_movement_reaction_stops_the_bot) stops the bot's move when it PICKS a
        # movement reaction, so a flank or spread step must not be picked
        # while a stand set seats the bot.
        self.assertIn('#include "playerbot/BossTacticsStand.h"', DUNGEON_ACTIONS)
        for cls in ("DragonFlankAction", "RaidSpreadAction"):
            declared = body(DUNGEON_ACTIONS_H, "class %s" % cls, end="\n    };\n")
            self.assertIn("bool isUseful() override;", declared)
            fn = body(DUNGEON_ACTIONS, "bool %s::isUseful()" % cls)
            self.assertIn("if (!MovementAction::isUseful())\n        return false;", fn)
            self.assertIn("return !FindBossStand(ai, bot).valid;", fn)

    def test_stand_episode_rows_are_gated_by_their_key(self):
        # AiPlayerbot.BossStandEvents: one BossStand row per bot's episode, nothing read, written or logged without it.
        self.assertIn('bossStandEvents = config.GetBoolDefault("AiPlayerbot.BossStandEvents", false);', CONFIG)
        log = body(ACTIONS, "void LogBossStand(")
        self.assertIn('sPlayerbotAIConfig.hasLog("bot_events.csv")', log)
        self.assertIn('sPlayerbotAIConfig.logEvent(ai, "BossStand", info1, info2);', log)
        fn = body(ACTIONS, "BossStandTarget ai::FindBossStand(")
        # The recorded episode is read only with the key on, and the read is decided in one place.
        self.assertIn("if (events && recorded != g_standEpisodes.end())", fn)
        self.assertIn("const StandEpisodeOut step = StandEpisodeStep(in);", fn)
        # A start read stores the flags of the rows it logs, so a fresh assigned-and-arrived read logs each row once.
        self.assertIn("StandEpisodeKeep(fresh, step, nowMs);", fn)
        self.assertIn("logged = StandEpisodeKeep(it->second, step, nowMs);", fn)
        self.assertIn("return finishStandRead(target, false, nullptr);", fn)
        self.assertIn("return finishStandRead(target, false, selfCounts ? \"side_full\" : \"not_counted\");", fn)
        self.assertIn('return finishStandRead(target, false, "given_up");', fn)
        self.assertIn("return finishStandRead(target, true, nullptr);", fn)
        for function, reason in (("void ReleaseStandEpisode(", "dead"), ("BossStandTarget ai::FindBossStand(", "gone")):
            text = body(ACTIONS, function)
            if reason == "dead":
                self.assertLess(text.index("if (!sPlayerbotAIConfig.bossStandEvents)"), text.index("std::lock_guard"))
            else:
                self.assertIn('ReleaseStandEpisode(ai, selfKey, sServerFacade.IsAlive(bot) ? "gone" : "dead");', text)
                self.assertEqual(text.count('ReleaseStandEpisode(ai, selfKey, "gone");'), 2)
        self.assertIn("g_standEpisodes.erase(key);", body(ACTIONS, "void ForgetStand(uint64 key)"))

    def test_the_whole_raid_key_seats_the_wiped_and_keeps_the_clear(self):
        # AiPlayerbot.BossStandWholeRaid, S2: both keys are read; the engagement rule is the policy's; the stay sits
        # after the given-up check and before the target is handed out; the flee action asks before it flees.
        for key, member in (("AiPlayerbot.BossStandEvents", "bossStandEvents"),
                            ("AiPlayerbot.BossStandWholeRaid", "bossStandWholeRaid")):
            self.assertIn('%s = config.GetBoolDefault("%s", false);' % (member, key), CONFIG)
        fn = body(ACTIONS, "BossStandTarget ai::FindBossStand(")
        self.assertIn("candidate.engaged = StandMemberEngaged(", fn)
        self.assertIn("member->IsInCombat(), boss->IsInCombat());", fn)
        given_up = fn.index('return finishStandRead(target, false, "given_up");')
        stay = fn.index("StandAlreadyClear(lane, *points[0], *bossRows, bot->GetPositionX(),")
        valid = fn.index("target.valid = true;\n    target.x = seat.x;")
        self.assertLess(given_up, stay)
        self.assertLess(stay, valid)
        self.assertIn("stayApplied = true;", fn)
        self.assertIn("target.stay = true;", fn)
        # The stay compares the window's seats next to the bot along the lane, not the seat the plan gave it.
        self.assertIn("kBossStandStayAlongYd", POLICY)
        self.assertIn("return finishStandRead(target, true, nullptr);", fn)
        flee = body(MOVEMENT_ACTIONS, "bool FleeAction::isUseful()")
        gate = flee.index("sPlayerbotAIConfig.bossStandWholeRaid")
        self.assertLess(gate, flee.index("FindBossStand(ai, bot).valid"))
        self.assertIn("FleeYieldsToStand(true, FindBossStand(ai, bot).valid)", flee)
        self.assertIn('#include "playerbot/BossTacticsStand.h"', MOVEMENT_ACTIONS)
        self.assertIn('#include "playerbot/BossStandPolicy.h"', MOVEMENT_ACTIONS)
        # The step-out from a ground cloud keeps its own rule.
        area = body(MOVEMENT_ACTIONS, "bool FleeFromAreaAction::isUseful()")
        self.assertNotIn("FindBossStand", area)
        declared = body(MOVEMENT_ACTIONS_H, "class FleeAction", end="\n    };\n")
        self.assertIn("virtual bool isUseful() override;", declared)

    def test_generic_code_names_no_boss(self):
        for name, text in (("BossStandPolicy.h", POLICY), ("BossTacticsStand.h", STAND_H)):
            self.assertNotIn("Onyxia", text, name)
            self.assertNotIn("10184", text, name)
        fn = body(ACTIONS, "BossStandTarget ai::FindBossStand(")
        self.assertNotIn("10184", fn)
        self.assertNotIn("Onyxia", fn)
        self.assertNotIn("17131", fn)
        for stand_text in (body(MGR_CPP, "std::uint32_t ai::ReadBossStandRows("), body(MGR_CPP, "void ai::LoadBossStands()")):
            self.assertNotIn("10184", stand_text)
            self.assertNotIn("Onyxia", stand_text)


if __name__ == "__main__":
    unittest.main()
