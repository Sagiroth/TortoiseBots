"""Wiring pins for the nearest take (AiPlayerbot.DungeonRunSeekNearestTake, Hakkar walk 55 F3).

The rules are pure and tested in tools/test_seek_aura_policy.cpp (SeekNearestTakerIs, SeekNearestTakeSonOk). These
pins prove FindBossSeekTank calls them under the flag, in the first off-tank's branch and before today's 3 s take, and
that the flag defaults off.
Run: python3 -m unittest tools/test_seek_nearest_take_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
CONFIG = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
SETTINGS = (ROOT / "docs/server/settings/dungeons-raids.md").read_text()
POLICY = (ROOT / "ai/playerbot/SeekAuraPolicy.h").read_text()


def body(text, signature):
    start = text.index(signature)
    end = text.index("\n}\n", start)
    return text[start:end]


class SeekNearestTakeWiring(unittest.TestCase):
    def test_the_flag_defaults_off(self):
        self.assertIn('GetBoolDefault("AiPlayerbot.DungeonRunSeekNearestTake", false)', CONFIG)
        self.assertIn("bool dungeonRunSeekNearestTake = false;", CONFIG_H)
        self.assertIn("AiPlayerbot.DungeonRunSeekNearestTake = 0", TEMPLATE)
        self.assertIn("### `AiPlayerbot.DungeonRunSeekNearestTake`", SETTINGS)

    def test_find_boss_seek_tank_calls_the_helper_under_the_flag(self):
        find = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(")
        healer = find[find.index("    if (healerTaker)\n    {"):]
        gate = healer.index("if (sPlayerbotAIConfig.dungeonRunSeekNearestTake)")
        call = healer.index("FindSeekNearestTake(ai, bot, roles, state, viaReach)")
        old = healer.index("if (state.healerSon && (!sPlayerbotAIConfig.dungeonRunSeekNearestTakeReach ||")
        self.assertLess(gate, call)
        self.assertLess(call, old)
        self.assertIn("task.source = nearestSon;", healer)
        # Flag off: today's 3 s rule is the branch's only take.
        self.assertIn("task.source = state.healerSon;", healer[old:])
        # The boss-victim stand-down still runs before any take.
        self.assertLess(find.index("!SeekTankTakeAllowed(state.boss && state.boss->GetVictim() == bot)"),
                        find.index("FindSeekNearestTake(ai, bot, roles, state, viaReach)"))

    def test_the_helper_reads_the_policy(self):
        fn = body(ACTIONS, "Unit* FindSeekNearestTake(PlayerbotAI* ai, Player* bot, const RaidTankRoles& roles,")
        self.assertIn("const int pick = PickSeekNearestTake(sPlayerbotAIConfig.dungeonRunSeekNearestTake, roles.botIndex, 2,", fn)
        self.assertIn("selfIsBossVictim, selfHoldsSon, cands, selfCollapsesCap, &viaReach);", fn)
        self.assertIn("SeekNearestTakeSonOk(c.victimNonTank, c.openMeetSon, c.victimInside)", fn)
        self.assertIn("c.openMeetSon = state.meetOpen && entry.first == state.meetSon;", fn)
        self.assertIn("c.selfYd = bot->GetDistance(son);", fn)
        self.assertIn("c.receiverYd = receiverUp ? receiver->GetDistance(son) : std::numeric_limits<float>::max();", fn)
        self.assertIn("raid.radius += kSeekTakeEdgeSlackYd;", fn)
        self.assertIn("AnchorCircleOf(*anchorRow, false)", fn)
        self.assertIn("const bool selfIsBossVictim = state.boss && state.boss->GetVictim() == bot;", fn)
        self.assertIn("PlayerHoldsFightSon(state.sons, state.row->targetEntry, bot, 0)", fn)
        self.assertIn("Player* const receiver = roles.tanks[2];", fn)
        # Readiness is per Son, inside the loop.
        loop = fn[fn.index("for (const auto& entry : state.nonTankSince)"):fn.index("const int pick =")]
        self.assertIn("c.receiverTauntReady = receiverUp && SeekTankTauntReady(receiver);", loop)
        self.assertIn("c.selfTauntReady = SeekTankTauntReady(bot);", loop)
        # No anchor: no pick, the 3 s rule stands.
        self.assertIn("if (!anchorRow)\n        return nullptr;", fn)

    def test_the_reach_flag_defaults_off(self):
        self.assertIn('GetBoolDefault("AiPlayerbot.DungeonRunSeekNearestTakeReach", false)', CONFIG)
        self.assertIn("bool dungeonRunSeekNearestTakeReach = false;", CONFIG_H)
        self.assertIn("AiPlayerbot.DungeonRunSeekNearestTakeReach = 0", TEMPLATE)
        self.assertIn("### `AiPlayerbot.DungeonRunSeekNearestTakeReach`", SETTINGS)

    def test_the_reach_feeds_the_pick(self):
        # Walk 57 F4: the reach's inputs are read only with its key, per Son, and the reference guard is the cap's own.
        fn = body(ACTIONS, "Unit* FindSeekNearestTake(PlayerbotAI* ai, Player* bot, const RaidTankRoles& roles,")
        self.assertIn("const bool reachOn = sPlayerbotAIConfig.dungeonRunSeekNearestTakeReach;", fn)
        self.assertIn("if (reachOn)\n        SeekWalkoutReachRead(bot, walked, lostMeets, latch);", fn)
        self.assertIn("const bool selfCollapsesCap = reachOn && SeekTankLeaveCollapsesCap(ai, roles, state.boss, bot, &capInfo);",
                      fn)
        # Review L3 / round 2: a latched (started, under 10 s) take of a Son skips the collapse re-check.
        self.assertIn("c.capLatched = WalkoutCapTakeLatched(latch.taker, latch.son, latch.ms, self, entry.first, nowMs);", fn)
        loop = fn[fn.index("for (const auto& entry : state.nonTankSince)"):fn.index("const int pick =")]
        self.assertIn("c.meetLost = WalkoutMeetLostWithin(lostMeets, entry.first, nowMs, kSeekNearestTakeMeetLostMs);",
                      loop)
        self.assertIn("c.reachKey = c.meetLost || std::find(walked.begin(), walked.end(), entry.first) != walked.end();",
                      loop)
        self.assertIn("viaReach = false;", fn[:fn.index("if (!state.row")])
        # Review H1/M2/M3: the collapse check reads the cap's own list (the shared helper, nobody skipped).
        ref = body(ACTIONS, "bool SeekTankLeaveCollapsesCap(PlayerbotAI* ai, const RaidTankRoles& roles, Unit* boss, Player* bot,")
        self.assertIn("if (!ai || !boss || !bot || !ThreatSuccessorTarget(boss))\n        return false;", ref)
        self.assertIn("ThreatCapTanksOf(ai, boss, roles, nullptr, &who);", ref)
        # Walk 58 F3: the same check through ThreatCapLeaveReadOf, which also names the reference for the held row.
        self.assertIn("ThreatCapLeaveReadOf(tanks, selfPos, sPlayerbotAIConfig.dungeonRunThreatSuccessorNotHolder);", ref)
        self.assertIn("return read.collapses;", ref)

    def test_the_healer_clock_reads_the_cap_guard_only_with_the_reach_key(self):
        # Review H1: key off, the `||` short-circuits before any read (today's take); key on, the guard decides and a
        # passed take is latched for its Son.
        find = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(")
        self.assertIn("if (state.healerSon && (!sPlayerbotAIConfig.dungeonRunSeekNearestTakeReach ||\n"
                      "                                SeekHealerClockCapOk(ai, bot, roles, state, state.healerSon)))", find)
        ok = body(ACTIONS, "bool SeekHealerClockCapOk(")
        self.assertIn("SeekHealerClockTakeAllowed(true, SeekTankLeaveCollapsesCap(ai, roles, state.boss, bot, &capInfo),", ok)
        self.assertIn("return allowed;", ok)
        self.assertIn("WalkoutCapTakeLatched(latch.taker, latch.son, latch.ms, self, sonGuid, state.nowMs),", ok)
        # Round 2 NEW-1: the clock's override (6 s on the non-tank, or the victim under 40 %) feeds the guard.
        self.assertIn("HealerClockOverridesCap(onNonTankMs, victimHpPct));", ok)
        self.assertIn("state.nowMs - held->second.sinceMs", ok)
        self.assertIn("const float victimHpPct = victim ? victim->GetHealthPercent() : 100.0f;", ok)
        values = (ROOT / "ai/playerbot/strategy/values/ThreatValues.cpp").read_text()
        self.assertIn("ai::ThreatCapTanksOf(ai, target, roles, ai->GetBot(), nullptr);", values)
        read = body(ACTIONS, "void SeekWalkoutReachRead(")
        self.assertIn("std::lock_guard<std::mutex> lock(g_walkoutMutex);", read)
        self.assertIn("walked = it->second.state.walkedSons;", read)
        self.assertIn("lostMeets = it->second.state.lostMeets;", read)
        # Round 2 NEW-2: a latched Son dead, gone or on any tank ends the latch (compare-and-clear on Son and stamp).
        self.assertIn("if (!WalkoutCapTakeEnds(alive, victim && PlayerbotAI::IsTank(victim)))\n        return;", read)
        self.assertIn("it->second.state.capTakeMs == latch.ms)\n            WalkoutCapTakeClear(it->second.state);", read)

    def test_taunt_readiness_reuses_the_taunt_after_guards(self):
        ready = body(ACTIONS, "bool SeekTankTauntReady(Player* tank)")
        self.assertIn("TankTauntGuards(tank, inControl, silenced);", ready)
        # An explicit read, not the full cast check: the GCD is not "not ready", nothing is written into the tank's AI.
        self.assertNotIn("CanCastSpell", ready)
        self.assertIn("const char* const taunt = KnownTaunt(tankAi);", ready)
        self.assertIn("const bool spellCooldownReady = spellId && !tank->HasSpellCooldown(spellId);", ready)
        self.assertIn('tankAi->HasAura("defensive stance", tank)', ready)
        self.assertIn("SeekTauntFormOk(tank->GetClass(), defensiveStance, bearForm)", ready)
        self.assertIn('tankAi->HasAura("bear form", tank) || tankAi->HasAura("dire bear form", tank)', ready)
        self.assertIn("return SeekTankTauntReadyNow(inControl, silenced, taunt != nullptr, spellCooldownReady,", ready)
        # A human tank has no AI: not ready.
        self.assertIn("if (!tankAi)\n        return false;", ready)
        guards = body(ACTIONS, "void TankTauntGuards(Player* tank, bool& inControl, bool& silenced)")
        self.assertIn("UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL", guards)
        self.assertIn("UNIT_FLAG_SILENCED | UNIT_FLAG_PACIFIED", guards)
        taunt_after = body(ACTIONS, "BossTauntAfter ai::FindBossTauntAfter(")
        self.assertIn("TankTauntGuards(tank, inControl, silenced);", taunt_after)

    def test_the_receiver_leaves_only_a_son_on_the_first_off_tank(self):
        find = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(")
        self.assertIn("if (candidateVictim && roles.tanks[i] == candidateVictim)\n"
                      "                    victimTankIdx = static_cast<int>(i);", find)
        self.assertIn("if (!SeekReceiverSkipsFighting(sPlayerbotAIConfig.dungeonRunSeekNearestTake, thirdTank, victimTankIdx))\n"
                      "                fighting = candidate;", find)
        self.assertIn("return flagOn && isReceiver && victimTankIdx == 1;", POLICY)

    def test_the_take_casts_the_known_taunt_with_the_flag(self):
        fn = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(Event& event)")
        take = fn[fn.index("if (task.take)"):fn.index("if (task.pull)")]
        self.assertIn("if (SeekTakeUsesKnownTaunt(sPlayerbotAIConfig.dungeonRunSeekNearestTake, task.takeSrc))", take)
        self.assertIn("taunt = KnownTaunt(ai);", take)
        # Flag off: today's warrior/druid switch.
        self.assertIn('case CLASS_WARRIOR: taunt = "taunt"; break;', take)
        self.assertIn("const bool cast = ai->CastSpell(taunt, task.source);", take)
        self.assertIn("return cast;", take)
        # The relief take always casts it (round 1: the flag-off paladin only attacked).
        self.assertIn('std::strcmp(takeSrc, "victim_relief") == 0', POLICY)

    def test_the_cap_latch_is_set_only_once_the_take_starts(self):
        # Codex round 2: a cap-guarded take (`nearest_reach`, `healer_clock`, key on) latches only after the attack or
        # chase went out, the tank was already after it, or the taunt cast; a refused or failed one clears its latch.
        fn = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(Event& event)")
        take = fn[fn.index("if (task.take)"):fn.index("if (task.pull)")]
        self.assertIn("const bool capGuarded = sPlayerbotAIConfig.dungeonRunSeekNearestTakeReach && task.takeSrc &&", take)
        self.assertIn('(std::strcmp(task.takeSrc, "nearest_reach") == 0 || std::strcmp(task.takeSrc, "healer_clock") == 0);',
                      take)
        self.assertEqual(3, take.count("SeekCapTakeNoteExec("))
        self.assertIn("task.source->GetObjectGuid().GetRawValue(), true, WorldTimer::getMSTime());", take)
        self.assertIn("task.source->GetObjectGuid().GetRawValue(), started, WorldTimer::getMSTime());", take)
        self.assertIn("started || cast, WorldTimer::getMSTime());", take)
        self.assertIn("\n                return true;", take)
        self.assertIn("\n            return false;", take)
        self.assertIn("else if (!chaseRefused)\n            started = true; // already after it", take)
        # No latch is set at pick time any more.
        self.assertNotIn("SeekCapTakeLatch(", ACTIONS)
        note = body(ACTIONS, "void SeekCapTakeNoteExec(")
        self.assertIn("WalkoutCapTakeNoteExec(it->second.state, taker, son, started, nowMs);", note)
        known = body(ACTIONS, "const char* KnownTaunt(PlayerbotAI* ai)")
        self.assertIn('"hand of reckoning"', known)
        # KnownTaunt's own comment sits above it again.
        self.assertIn("// trainer taunt 51303; Righteous Defense is TBC and no 1.12 spell). Null when it knows none.\n"
                      "const char* KnownTaunt(PlayerbotAI* ai)", ACTIONS)


if __name__ == "__main__":
    unittest.main()
