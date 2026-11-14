"""Wiring pins for the reaction cast guard (AiPlayerbot.ReactionHealCastGuardPct).

UpdateAIReaction breaks a bot's cast as soon as FindReaction PICKS a reaction whose
action says it may, before that action's Execute can find it has nothing to do. The
guard rule lives in ai/playerbot/ReactionCastGuardPolicy.h (ReactionCastRunning,
ReactionCastPct, ReactionCastBreakDue; behaviour in tools/test_reaction_cast_guard_policy.cpp).
These pins only prove the live code feeds it and that nothing else changed:

 - the engine reads the picked action's ReactionYieldsToHealCast, which defaults to
   false on Action and is overridden to true by exactly two actions: the boss tactics
   seek and the raid spread. Every emergency (and every other movement action) keeps
   breaking the cast;
 - the call site still breaks the cast only through ReactionCastBreakDue, with the
   picked reaction's own ShouldInterruptCast, the bot's healer role, and the generic
   slot's read (its state, its countdown, the channel's own duration, its heal effects);
 - the read counts a cast bar in PREPARING and a channel in CASTING. The old guard read
   only CASTING, this core's channel phase, so a normal cast-time heal was never seen as
   a heal under way;
 - the key is read with default 0, clamped to 0-100, beside DungeonRunBossSpreadSlots
   in the loader the reload path re-runs, and the shipped template sets 0.

Run: python3 -m unittest tools/test_reaction_cast_guard_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PLAYERBOT_AI = (ROOT / "ai/playerbot/PlayerbotAI.cpp").read_text()
POLICY = (ROOT / "ai/playerbot/ReactionCastGuardPolicy.h").read_text()
POLICY_TEST = (ROOT / "tools/test_reaction_cast_guard_policy.cpp").read_text()
ACTION_H = (ROOT / "ai/playerbot/strategy/Action.h").read_text()
REACTION_ENGINE = (ROOT / "ai/playerbot/strategy/ReactionEngine.h").read_text()
MOVEMENT_H = (ROOT / "ai/playerbot/strategy/actions/MovementActions.h").read_text()
SEEK_H = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.h").read_text()
DUNGEON_H = (ROOT / "ai/playerbot/strategy/actions/DungeonActions.h").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
CONFIG_CPP = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()

CALL = PLAYERBOT_AI[PLAYERBOT_AI.index("bool PlayerbotAI::UpdateAIReaction"):]
CALL = CALL[:CALL.index("\n}")]

YIELD_TRUE = "bool ReactionYieldsToHealCast() const override { return true; }"


def body(text, start, end="\n    };"):
    """The text from `start` to the class body's end."""
    i = text.index(start)
    return text[i:text.index(end, i)]


class ReactionCastGuardWiring(unittest.TestCase):
    def test_action_default_does_not_yield(self):
        # The base says no: only the actions that opt in stand down for a heal.
        self.assertIn("virtual bool ReactionYieldsToHealCast() const { return false; }", ACTION_H)
        # A movement action still breaks the cast when picked, as before.
        self.assertIn("bool ShouldReactionInterruptCast() const override { return true; }", MOVEMENT_H)
        self.assertNotIn("ReactionYieldsToHealCast", MOVEMENT_H)

    def test_exactly_seek_and_spread_yield(self):
        self.assertIn(YIELD_TRUE, body(SEEK_H, "class BossTacticsSeekAction"))
        self.assertIn(YIELD_TRUE, body(DUNGEON_H, "class RaidSpreadAction"))
        overrides = [p.read_text() for p in sorted((ROOT / "ai").rglob("*.h"))]
        self.assertEqual(sum(t.count(YIELD_TRUE) for t in overrides), 2)

    def test_engine_forwards_the_flag(self):
        self.assertIn("bool YieldsToHealCast() const { return action ? action->ReactionYieldsToHealCast() : false; }",
                      REACTION_ENGINE)

    def test_state_read_accepts_a_cast_bar_and_a_channel(self):
        # Numeric core states, as InterruptCastingPolicy.h does: PREPARING is a cast bar, CASTING the channel.
        self.assertIn("REACTION_CAST_PREPARING = 1,", POLICY)
        self.assertIn("REACTION_CAST_CASTING = 2,", POLICY)
        self.assertIn("(cast.state == REACTION_CAST_PREPARING || cast.state == REACTION_CAST_CASTING) && "
                      "cast.remainingMs > 0", POLICY)
        # The policy test pins the two numbers against the core's own enum.
        self.assertIn("static_assert(kPreparing == 1, \"SPELL_STATE_PREPARING moved\");", POLICY_TEST)
        self.assertIn("static_assert(kCasting == 2, \"SPELL_STATE_CASTING moved\");", POLICY_TEST)

    def test_preparing_heal_at_four_percent_keeps_the_cast(self):
        # A healer's Flash Heal (2.5 s) 100 ms into a PREPARING bar, with a seek reaction and the key at 1: the
        # guard keeps the cast. This is the case the r1 guard missed by reading only CASTING.
        self.assertIn("ReactionCastRead const flashHeal4 = Prepare(2500, 2400);", POLICY_TEST)
        self.assertIn("CHECK(!ReactionCastBreakDue(true, kYields, 1, kHealer, &flashHeal4));", POLICY_TEST)
        # The live read feeds the state, not a CASTING gate.
        self.assertIn("cast.state = spell->getState();", CALL)
        self.assertNotIn("if (cast->getState() ==", CALL)

    def test_preparing_heal_with_key_off_is_cut(self):
        # The same cast with the key at 0: cut, as before.
        self.assertIn("CHECK(ReactionCastBreakDue(true, kYields, 0, kHealer, &flashHeal4));", POLICY_TEST)

    def test_call_site_feeds_the_rule(self):
        # One break site, and it goes through the policy with all five inputs.
        self.assertEqual(CALL.count("InterruptSpell()"), 1)
        self.assertIn("if (ReactionCastBreakDue(interruptCast, reaction->YieldsToHealCast(),", CALL)
        self.assertIn("sPlayerbotAIConfig.reactionHealCastGuardPct, IsHeal(bot), &cast))", CALL)
        self.assertIn("const bool interruptCast = reaction->ShouldInterruptCast();", CALL)
        # The read comes from the generic slot's own state and clock.
        self.assertIn("ReactionCastRead cast;", CALL)
        self.assertIn("Spell* spell = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL)", CALL)
        self.assertIn("cast.remainingMs = spell->GetCastedTime();", CALL)
        # A channel counts its duration down (GetCastTime holds the pre-channel cast time, usually 0); a bar
        # counts its cast time down.
        self.assertIn("cast.totalMs = cast.state == SPELL_STATE_CASTING && spell->m_spellInfo", CALL)
        self.assertIn("? spell->m_spellInfo->CalculateDuration(bot) : spell->GetCastTime();", CALL)
        # A heal by its effects: a direct heal or a periodic one.
        self.assertIn("cast.isHeal = ReactionCastIsHeal(spell->m_spellInfo);", CALL)
        self.assertIn("entry->Effect[i] == SPELL_EFFECT_HEAL", PLAYERBOT_AI)
        self.assertIn("entry->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_HEAL", PLAYERBOT_AI)

    def test_policy_is_core_free_and_tested(self):
        self.assertNotIn("#include <game", POLICY)
        self.assertIn("inline bool ReactionCastBreakDue(", POLICY)
        self.assertIn("if (!shouldInterrupt) return false;", POLICY)
        self.assertIn("if (guardPct <= 0 || !yieldsToHeal || !botIsHealer) return true;", POLICY)
        self.assertIn("if (!cast || !cast->isHeal) return true;", POLICY)
        self.assertIn("return pct < 0 || pct < guardPct;", POLICY)

    def test_config_key(self):
        # Default 0: with the key off every reaction breaks as before.
        self.assertIn(
            'reactionHealCastGuardPct =\n'
            '        std::min(100, std::max(0, config.GetIntDefault("AiPlayerbot.ReactionHealCastGuardPct", 0)));',
            CONFIG_CPP)
        self.assertIn("int reactionHealCastGuardPct = 0;", CONFIG_H)
        # Read in LoadDungeonRunKeys, which `.reload config` re-runs, beside the spread-slots key.
        loader = CONFIG_CPP[CONFIG_CPP.index("void PlayerbotAIConfig::LoadDungeonRunKeys()"):]
        loader = loader[:loader.index("\n}\n")]
        self.assertIn('dungeonRunBossSpreadSlots = config.GetBoolDefault("AiPlayerbot.DungeonRunBossSpreadSlots", false);', loader)
        self.assertIn('config.GetIntDefault("AiPlayerbot.ReactionHealCastGuardPct", 0)', loader)
        # The shipped template ships it off.
        self.assertRegex(TEMPLATE, re.compile(r"^# 0 - off: .*\n(?:#.*\n)*AiPlayerbot\.ReactionHealCastGuardPct = 0$",
                                              re.MULTILINE))


if __name__ == "__main__":
    unittest.main()
