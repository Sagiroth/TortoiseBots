"""Wiring pins for the creature-charm yield, the charmed-ally area veto and taunt_after_cast until_tank.

A bot a creature charms (Hakkar's Cause Insanity 24327) is hostile and still carries its own PlayerbotAI: the core's
charm handler gives a PlayerControlledAI to a player charmer only, so a creature charm leaves both AIs driving the same
player. AiPlayerbot.DungeonRunCharmYield therefore makes the bot's AI yield entirely, stands down an area action that
would land within 10 yd of a charmed group member, and lets the row's tank take the boss back (taunt_after_cast, mode
until_tank). The rules are pure in ai/playerbot/CharmYieldPolicy.h and ai/playerbot/BossTacticsPolicy.h
(TauntAfterTankIndex, TauntAfterDue); their behaviour is tested in tools/test_charm_yield_policy.cpp and
tools/test_boss_tactics_policy.cpp. These pins only prove the live code calls them, and that
ServerFacade::IsCharmed (player charmers only, other callers depend on it) is untouched.

Run: python3 -m unittest tools/test_charm_yield_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PLAYERBOT_AI_CPP = (ROOT / "ai/playerbot/PlayerbotAI.cpp").read_text()
SERVER_FACADE = (ROOT / "ai/playerbot/ServerFacade.h").read_text()
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
ACTIONS_H = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.h").read_text()
TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/BossTacticsTriggers.cpp").read_text()
TRIGGERS_H = (ROOT / "ai/playerbot/strategy/triggers/BossTacticsTriggers.h").read_text()
STRATEGY = (ROOT / "ai/playerbot/strategy/generic/BossTacticsStrategy.cpp").read_text()
ACTION_CONTEXT = (ROOT / "ai/playerbot/strategy/actions/ActionContext.h").read_text()
TRIGGER_CONTEXT = (ROOT / "ai/playerbot/strategy/triggers/TriggerContext.h").read_text()
POLICY = (ROOT / "ai/playerbot/BossTacticsPolicy.h").read_text()
COMBAT_STRATEGY = (ROOT / "ai/playerbot/strategy/generic/CombatStrategy.cpp").read_text()
DUNGEON_STRATEGY = (ROOT / "ai/playerbot/strategy/generic/DungeonStrategy.cpp").read_text()
POLICY_TEST = (ROOT / "tools/test_boss_tactics_policy.cpp").read_text()

# ServerFacade::IsCharmed byte for byte: its MANGOS branch counts player charmers only and other callers depend on it.
CHARM_BODY = """        bool IsCharmed(Unit *unit)
        {
#ifdef MANGOS
            return unit->GetCharmerGuid() && unit->GetCharmerGuid().IsPlayer();
#endif
#ifdef CMANGOS
            return unit->GetCharmerGuid().IsEmpty() == false;
#endif
        }"""


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class CharmYieldWiring(unittest.TestCase):
    def test_update_ai_internal_yields_to_a_creature_charm(self):
        update = body(PLAYERBOT_AI_CPP, "void PlayerbotAI::UpdateAIInternal(")
        self.assertIn('#include "playerbot/CharmYieldPolicy.h"', PLAYERBOT_AI_CPP)
        yield_at = update.index("CreatureCharmYields(")
        # First statement after the null checks: before the evade probe, the engines and every action.
        self.assertLess(update.index("if (bot->IsBeingTeleported() || !bot->IsInWorld())"), yield_at)
        self.assertLess(yield_at, update.index("UpdateEvadeProbe("))
        self.assertLess(yield_at, update.index("DoNextAction("))
        self.assertIn("sPlayerbotAIConfig.dungeonRunCharmYield", update)
        self.assertIn("!bot->GetCharmerGuid().IsEmpty()", update)
        self.assertIn("bot->GetCharmerGuid().IsPlayer()", update)
        # The yield is a plain return: it never reaches for the motion master, a cast or an engine.
        block = update[yield_at:update.index("UpdateEvadeProbe(")]
        self.assertIn("return;", block)
        self.assertNotIn("StopMoving", block)
        self.assertNotIn("InterruptSpell", block)
        self.assertNotIn("DoNextAction", block)

    def test_success_facade_ischarmed_stays_player_only(self):
        start = SERVER_FACADE.index("        bool IsCharmed(Unit *unit)")
        self.assertEqual(SERVER_FACADE[start:start + len(CHARM_BODY)], CHARM_BODY)
        # The yield reads the guid itself; it never borrows the facade's player-only answer.
        update = body(PLAYERBOT_AI_CPP, "void PlayerbotAI::UpdateAIInternal(")
        self.assertNotIn("IsCharmed(", update)

    def test_hold_multiplier_vetoes_area_around_a_charmed_ally(self):
        veto = body(ACTIONS, "bool CharmedAllyAreaVeto(PlayerbotAI* ai, Player* bot, Action* action)")
        self.assertIn("if (!sPlayerbotAIConfig.dungeonRunCharmYield || !action)", veto)
        roster = body(ACTIONS, "const CharmedAllyCache& CharmedAllies(PlayerbotAI* ai, Player* bot)")
        self.assertIn("for (Player* member : LiveGroupMembers(group))", roster)
        self.assertIn("ObjectGuid const charmer = member->GetCharmerGuid();", roster)
        self.assertIn("if (charmer.IsEmpty() || charmer.IsPlayer())", roster)
        self.assertIn("IsCleaveToggleAction(action)", veto)
        self.assertIn("IsAreaDamageAction(ai, action)", veto)
        self.assertIn('action->GetTargetName() == "self target"', veto)
        self.assertIn("CharmedAllyInArea(toPoint, bot->GetDistance(member), selfCentred)", veto)
        # One roster read per AI update, whatever the number of actions the engine asks about.
        self.assertIn("t_charmedAllies.tick == tick", ACTIONS)
        self.assertIn("if (charmed.count == 0)", veto)
        # The key alone decides: the veto lives in its own multiplier on the always-on combat strategies, never in
        # the boss-tactics multiplier (that one needs a tactic row, a fight and the boss tactics opt-in).
        charm = body(ACTIONS, "float CharmedAllyHoldMultiplier::GetValue(Action* action)")
        self.assertIn("return CharmedAllyAreaVeto(ai, bot, action) ? 0.0f : 1.0f;", charm)
        hold = body(ACTIONS, "float BossTacticsHoldMultiplier::GetValue(Action* action)")
        self.assertNotIn("CharmedAllyAreaVeto", hold)
        self.assertIn("class CharmedAllyHoldMultiplier : public Multiplier", ACTIONS_H)
        self.assertIn("Multiplier(ai, \"charmed ally hold\")", ACTIONS_H)
        self.assertIn("multipliers.push_back(new CharmedAllyHoldMultiplier(ai));", COMBAT_STRATEGY)
        self.assertIn("multipliers.push_back(new CharmedAllyHoldMultiplier(ai));", DUNGEON_STRATEGY)

    def test_taunt_after_is_registered_and_runs_until_tank(self):
        self.assertIn('creators["boss tactics taunt after"] = [](PlayerbotAI* ai) { return new BossTacticsTauntAfterAction(ai); };',
                      ACTION_CONTEXT)
        self.assertIn('creators["boss tactics taunt after"] = [](PlayerbotAI* ai) { return new BossTacticsTauntAfterTrigger(ai); };',
                      TRIGGER_CONTEXT)
        self.assertIn("class BossTacticsTauntAfterAction : public AttackAction", ACTIONS_H)
        self.assertIn("class BossTacticsTauntAfterTrigger : public Trigger", TRIGGERS_H)
        # ACTION_HIGH + 7, combat engine only, as the row's row slot.
        self.assertIn('"boss tactics taunt after",\n        NextAction::array(0, new NextAction("boss tactics taunt after", ACTION_HIGH + 7), NULL)));',
                      STRATEGY)
        self.assertIn("BossTauntAfter ai::FindBossTauntAfter(", ACTIONS)
        self.assertIn("TauntAfterTankIndex(alive, static_cast<int>(row->priority))", ACTIONS)
        self.assertIn("TauntEpisodeTick(onNonTank, victimIsTank, state.open, state.spent,", ACTIONS)
        # Only until_tank is implemented; blink and the spell triggers stay parsed and ignored.
        self.assertIn("row->kind != BossTacticKind::TauntAfterCast || row->mode != TacticMode::UntilTank", ACTIONS)
        self.assertIn("bool BossTacticsTauntAfterTrigger::IsActive()", TRIGGERS)
        self.assertIn("FindBossTauntAfter(ai, bot, ScanBossTactics(ai, bot))", TRIGGERS)
        # The class taunt set of the 1.12 cores, the Gluth pattern.
        self.assertIn('return "taunt";', ACTIONS)
        self.assertIn('return "growl";', ACTIONS)

    def test_taunt_after_log_row(self):
        self.assertIn('sPlayerbotAIConfig.logEvent(ai, "BossTacticsTauntAfter", info1, info2);', ACTIONS)
        self.assertIn('"trigger=until_tank,tank=%d,regained_ms=%s,taunted=%d"', ACTIONS)
        self.assertIn("NoteBossTauntAfterCast(ai, bot, task);", ACTIONS)
        # A subject between victims (or on a pet) is mid-transition: only a tank ends the episode early, so a rival
        # retarget never hands the off-tank a second full window.
        scan = body(ACTIONS, "BossTauntAfter ai::FindBossTauntAfter(")
        # The episode's transitions are the pure machine's; a tank having the subject back ends it early.
        self.assertIn("TauntEpisodeTick(onNonTank, victimIsTank, state.open, state.spent,", scan)
        self.assertIn("TauntEpisodeStep::Regained", scan)
        self.assertIn("state = TauntAfterEpisode();", scan)
        # The scan runs on aura tactics alone (Arlokk's threat wipe charms nothing): the gate never names the charm
        # key, and a creature-charmed tank only leaves the order while the yield key is on (M5, L8).
        self.assertIn("if (!ai || !bot || !sPlayerbotAIConfig.dungeonRunAuraTactics)", scan)
        self.assertNotIn("||\n        !sPlayerbotAIConfig.dungeonRunCharmYield", scan)
        self.assertIn("sPlayerbotAIConfig.dungeonRunCharmYield &&", scan)
        self.assertIn("!charmer.IsEmpty() && !charmer.IsPlayer()", scan)
        self.assertIn("TauntAfterTankIndex", POLICY)
        self.assertIn("TauntAfterDue", POLICY)
        # The pure cases are pinned in the policy test file too.
        self.assertIn("CHECK(TauntAfterTankIndex(onlyFirst, 1) == 0);", POLICY_TEST)
        self.assertIn("CHECK(!TauntAfterDue(true, 10000, 10000, false));", POLICY_TEST)


if __name__ == '__main__':
    unittest.main()
