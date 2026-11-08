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
        call = healer.index("FindSeekNearestTake(ai, bot, roles, state)")
        old = healer.index("if (state.healerSon)")
        self.assertLess(gate, call)
        self.assertLess(call, old)
        self.assertIn("task.source = nearestSon;", healer)
        # Flag off: today's 3 s rule is the branch's only take.
        self.assertIn("task.source = state.healerSon;", healer[old:])
        # The boss-victim stand-down still runs before any take.
        self.assertLess(find.index("!SeekTankTakeAllowed(state.boss && state.boss->GetVictim() == bot)"),
                        find.index("FindSeekNearestTake(ai, bot, roles, state)"))

    def test_the_helper_reads_the_policy(self):
        fn = body(ACTIONS, "Unit* FindSeekNearestTake(PlayerbotAI* ai, Player* bot, const RaidTankRoles& roles,")
        self.assertIn("const int pick = PickSeekNearestTake(sPlayerbotAIConfig.dungeonRunSeekNearestTake, roles.botIndex, 2,", fn)
        self.assertIn("selfIsBossVictim, selfHoldsSon, cands);", fn)
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
        self.assertIn("if (sPlayerbotAIConfig.dungeonRunSeekNearestTake)\n        {", take)
        self.assertIn("taunt = KnownTaunt(ai);", take)
        # Flag off: today's warrior/druid switch.
        self.assertIn('case CLASS_WARRIOR: taunt = "taunt"; break;', take)
        self.assertIn("return ai->CastSpell(taunt, task.source);", take)
        known = body(ACTIONS, "const char* KnownTaunt(PlayerbotAI* ai)")
        self.assertIn('"hand of reckoning"', known)
        # KnownTaunt's own comment sits above it again.
        self.assertIn("// trainer taunt 51303; Righteous Defense is TBC and no 1.12 spell). Null when it knows none.\n"
                      "const char* KnownTaunt(PlayerbotAI* ai)", ACTIONS)


if __name__ == "__main__":
    unittest.main()
