"""Wiring pins for the creature-charm yield, the charmed-ally area veto and taunt_after_cast until_tank.

A bot a creature charms (Hakkar's Cause Insanity 24327) is hostile and still carries its own PlayerbotAI: the core's
charm handler gives a PlayerControlledAI to a player charmer only, so a creature charm leaves both AIs driving the same
player. AiPlayerbot.DungeonRunCharmYield therefore makes the bot's AI yield entirely, stands down an area action that
would land within 10 yd of a charmed group member and any harmful single-target action aimed at it, keeps every pick,
attack order, interrupt and pet order off it, parks every pet and guardian passive with its combat cleared while such
a member is up (the core's PetAI acquires targets on its own), and lets the row's tank take the boss back (taunt_after_cast, mode
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
CHARM_POLICY_TEST = (ROOT / "tools/test_charm_yield_policy.cpp").read_text()
ATTACKERS_H = (ROOT / "ai/playerbot/strategy/values/AttackersValue.h").read_text()
ATTACKERS = (ROOT / "ai/playerbot/strategy/values/AttackersValue.cpp").read_text()
POSSIBLE_ATTACK_TARGETS = (ROOT / "ai/playerbot/strategy/values/PossibleAttackTargetsValue.cpp").read_text()
INVALID_TARGET = (ROOT / "ai/playerbot/strategy/values/InvalidTargetValue.cpp").read_text()
ATTACK_ACTION = (ROOT / "ai/playerbot/strategy/actions/AttackAction.cpp").read_text()
INTERRUPT_VALUE = (ROOT / "ai/playerbot/strategy/values/InterruptTargetValue.cpp").read_text()
INTERRUPT_POLICY = (ROOT / "ai/playerbot/policy/combat/InterruptTargetPolicy.h").read_text()
INTERRUPT_TEST = (ROOT / "tools/test_interrupt_target_policy.cpp").read_text()
CAST_CUSTOM = (ROOT / "ai/playerbot/strategy/actions/CastCustomSpellAction.cpp").read_text()
GENERIC_ACTIONS = (ROOT / "ai/playerbot/strategy/actions/GenericActions.cpp").read_text()

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

    def test_a_pet_stands_down_while_a_charmed_ally_is_up(self):
        # The core's PetAI picks its own targets: a refusal in CanPetAttack neither stops the pet that reacts to the
        # member on its own nor the attack it already had. The per-update hold parks the pet and guardians passive
        # and clears their combat, before every engine and every early return in UpdateAI.
        update = body(PLAYERBOT_AI_CPP, "void PlayerbotAI::UpdateAI(uint32 elapsed, bool minimal)")
        self.assertIn("UpdateCharmedAllyPetHold();", update)
        self.assertLess(update.index("UpdateCharmedAllyPetHold();"), update.index("UpdateFaceTarget("))
        hold = body(PLAYERBOT_AI_CPP, "void PlayerbotAI::UpdateCharmedAllyPetHold()")
        self.assertIn("CharmedAllyParksPet(sPlayerbotAIConfig.dungeonRunCharmYield, charmedAllyUp)", hold)
        self.assertIn("for (Player* member : LiveGroupMembers(bot->GetGroup()))", hold)
        self.assertIn("if (charmer.IsEmpty() || charmer.IsPlayer())", hold)
        self.assertIn("member->GetMap() != bot->GetMap()", hold)
        self.assertIn("pet->SetReactState(REACT_PASSIVE);", hold)
        self.assertIn("pet->AttackStop();", hold)
        self.assertIn("bot->CallForAllControlledUnits(ParkGuardian{charmHoldGuardianStances_}, CONTROLLED_GUARDIANS)", hold)
        self.assertIn("if (!creature || creature->IsTotem())", hold)
        self.assertIn("creature->SetReactState(REACT_PASSIVE);", hold)
        self.assertIn("creature->AttackStop();", hold)
        # Only our own park is undone: the pull hold or the player owns a stance set meanwhile.
        self.assertIn("pet->GetReactState() == REACT_PASSIVE", hold)
        self.assertIn("pullHoldPetStance_ == kNoSavedStance", hold)
        self.assertIn("pet->getObjectGuid() == charmHoldPetGuid_", hold)
        # While the hold is up it owns the stance: no engine flips the parked pet back to defensive in the same tick.
        self.assertIn("bool IsCharmPetHoldActive() const { return charmHoldActive_; }",
                      (ROOT / "ai/playerbot/PlayerbotAI.h").read_text())
        self.assertEqual(ATTACK_ACTION.count("&& !ai->IsCharmPetHoldActive()"), 1)
        self.assertEqual(GENERIC_ACTIONS.count("&& !ai->IsCharmPetHoldActive()"), 1)
        stance = body(GENERIC_ACTIONS, "bool SetPetStanceAction::Execute(Event& /*event*/)")
        self.assertIn("if (ai->IsCharmPetHoldActive())", stance)
        self.assertLess(stance.index("if (ai->IsCharmPetHoldActive())"), stance.index("pet->SetReactState(REACT_DEFENSIVE);"))
        self.assertIn("CHECK(CharmedAllyParksPet(true, true));", CHARM_POLICY_TEST)
        self.assertIn("CHECK(!CharmedAllyParksPet(false, true));", CHARM_POLICY_TEST)

    def test_success_facade_ischarmed_stays_player_only(self):
        start = SERVER_FACADE.index("        bool IsCharmed(Unit *unit)")
        self.assertEqual(SERVER_FACADE[start:start + len(CHARM_BODY)], CHARM_BODY)
        # The yield reads the guid itself; it never borrows the facade's player-only answer.
        update = body(PLAYERBOT_AI_CPP, "void PlayerbotAI::UpdateAIInternal(")
        self.assertNotIn("IsCharmed(", update)

    def test_hold_multiplier_vetoes_area_and_targeted_actions_on_a_charmed_ally(self):
        veto = body(ACTIONS, "bool CharmedAllyVeto(PlayerbotAI* ai, Player* bot, Action* action)")
        self.assertIn("if (!sPlayerbotAIConfig.dungeonRunCharmYield || !action)", veto)
        roster = body(ACTIONS, "const CharmedAllyCache& CharmedAllies(PlayerbotAI* ai, Player* bot)")
        self.assertIn("for (Player* member : LiveGroupMembers(group))", roster)
        self.assertIn("ObjectGuid const charmer = member->GetCharmerGuid();", roster)
        self.assertIn("if (charmer.IsEmpty() || charmer.IsPlayer())", roster)
        # A harmful single-target action aimed at the member is refused by identity, beside the area reach test.
        self.assertIn("IsHarmfulTargetedAction(action)", veto)
        self.assertIn("Unit* const aimed = action->GetTarget();", veto)
        self.assertIn("charmed.members[i] == aimed", veto)
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
        self.assertIn("return CharmedAllyVeto(ai, bot, action) ? 0.0f : 1.0f;", charm)
        hold = body(ACTIONS, "float BossTacticsHoldMultiplier::GetValue(Action* action)")
        self.assertNotIn("CharmedAllyVeto", hold)
        self.assertIn("class CharmedAllyHoldMultiplier : public Multiplier", ACTIONS_H)
        self.assertIn("Multiplier(ai, \"charmed ally hold\")", ACTIONS_H)
        self.assertIn("multipliers.push_back(new CharmedAllyHoldMultiplier(ai));", COMBAT_STRATEGY)
        self.assertIn("multipliers.push_back(new CharmedAllyHoldMultiplier(ai));", DUNGEON_STRATEGY)

    def test_a_charmed_ally_is_never_picked_attacked_or_interrupted(self):
        # Target selection: the attackers list and every pick that bypasses it.
        self.assertIn("static bool CharmedAllyOffLimits(Unit* target, Player* player);", ATTACKERS_H)
        attackers = body(ATTACKERS, "bool AttackersValue::CharmedAllyOffLimits(Unit* target, Player* player)")
        self.assertIn("if (!sPlayerbotAIConfig.dungeonRunCharmYield || !target || !player)", attackers)
        self.assertIn("bool const sameGroup = group && ally->GetGroup() == group;", attackers)
        self.assertIn("ObjectGuid const charmer = ally->GetCharmerGuid();", attackers)
        self.assertIn("CharmedAllyIsOffLimits(true, sameGroup, !charmer.IsEmpty(), charmer.IsPlayer())", attackers)
        valid = body(ATTACKERS, "bool AttackersValue::IsValid(Unit* target, Player* player, Player* owner,")
        self.assertIn("if (target && CharmedAllyOffLimits(target, playerToCheckAgainst))", valid)
        possible = body(POSSIBLE_ATTACK_TARGETS, "bool PossibleAttackTargetsValue::IsValid(Unit* target, Player* player,")
        self.assertIn("if (target && player && AttackersValue::CharmedAllyOffLimits(target, player))", possible)
        # A target already held is dropped through the invalid-target path (select new target clears it).
        invalid = body(INVALID_TARGET, "bool InvalidTargetValue::Calculate()")
        self.assertIn("if (AttackersValue::CharmedAllyOffLimits(target, bot))", invalid)
        # The attack/assist path: the order gate and the pet order.
        target_valid = body(ATTACK_ACTION, "bool AttackAction::IsTargetValid(Player* requester, Unit* target, float maxDistance)")
        self.assertIn("else if (AttackersValue::CharmedAllyOffLimits(target, bot))", target_valid)
        pet = body(ATTACK_ACTION, "bool AttackAction::CanPetAttack(PlayerbotAI* ai, Pet* pet, Unit* target)")
        self.assertIn("if (AttackersValue::CharmedAllyOffLimits(target, bot))", pet)
        # An explicit ".bot cast" order does not outrank the rule either; a positive (heal or buff) spell is allowed.
        self.assertIn("if (target && target != bot && !pSpellInfo->IsPositiveSpell() &&\n        AttackersValue::CharmedAllyOffLimits(target, bot))",
                      CAST_CUSTOM)
        # The interrupt finder rejects the cast outright (tier -1), key off included in the pure test.
        interrupt = body(INTERRUPT_VALUE, "Unit* InterruptTargetValue::Calculate()")
        self.assertIn("bool const offLimits = AttackersValue::CharmedAllyOffLimits(unit, bot);", interrupt)
        self.assertIn("InterruptTargetTier(priority, claimed, offLimits)", interrupt)
        self.assertIn("if (offLimits || claimedByOther)", INTERRUPT_POLICY)
        self.assertIn("CHECK(InterruptTargetTier(true, false, true) == kInterruptTargetRejected);", INTERRUPT_TEST)
        self.assertIn("CHECK(!picked(true, true, true, false));", CHARM_POLICY_TEST)
        self.assertIn("CHECK(picked(false, true, true, false));", CHARM_POLICY_TEST)

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
        self.assertIn("TauntEpisodeTick(tauntable, onNonTank, victimIsTank,", ACTIONS)
        # A subject the game makes immune to taunts opens no episode: the tank keeps the attack and the taunt.
        self.assertIn("bool tauntable,", POLICY)
        # Only until_tank is implemented; blink and the spell triggers stay parsed and ignored.
        self.assertIn("row->kind != BossTacticKind::TauntAfterCast || row->mode != TacticMode::UntilTank", ACTIONS)
        self.assertIn("bool BossTacticsTauntAfterTrigger::IsActive()", TRIGGERS)
        self.assertIn("FindBossTauntAfter(ai, bot, ScanBossTactics(ai, bot))", TRIGGERS)
        # The class taunt set of the 1.12 cores, the Gluth pattern.
        self.assertIn('return "taunt";', ACTIONS)
        self.assertIn('return "growl";', ACTIONS)

    def test_the_taunt_immune_skip_is_logged_once_per_fight(self):
        # A taunt-immune subject opens no episode, so before this the row looked applied while the game refused every
        # taunt (walk 40: 108 TauntAfter rows, taunted=0). The scan now says why, once per fight per subject and row.
        scan = body(ACTIONS, "BossTauntAfter ai::FindBossTauntAfter(")
        self.assertIn('sLog.outString("taunt_after skipped: target taunt-immune (map %u, entry %u)",', scan)
        self.assertIn("const bool tauntable = !TargetTauntImmune(unit);", scan)
        self.assertIn("TauntEpisodeTick(tauntable, onNonTank, victimIsTank,", scan)
        self.assertLess(scan.index("if (!tauntable)"), scan.index("TauntEpisodeTick(tauntable"))
        # Once per fight: the report lives in its own map keyed like the episode (no bot in the key, so the other
        # tanks' scans of the same subject stay quiet), it is kept alive while the fight reads it, and it is pruned
        # with the episodes when the subject leaves the fight.
        self.assertIn("std::map<TauntAfterKey, std::uint32_t> g_tauntAfterImmune;", ACTIONS)
        self.assertIn("auto const reported = g_tauntAfterImmune.find(key);", scan)
        self.assertIn("if (reported == g_tauntAfterImmune.end())", scan)
        self.assertIn("g_tauntAfterImmune.erase(it)", ACTIONS)

    def test_taunt_after_log_row(self):
        self.assertIn('sPlayerbotAIConfig.logEvent(ai, "BossTacticsTauntAfter", info1, info2);', ACTIONS)
        self.assertIn('"trigger=until_tank,tank=%d,regained_ms=%s,taunted=%d"', ACTIONS)
        self.assertIn("NoteBossTauntAfterCast(ai, bot, task);", ACTIONS)
        # A subject between victims (or on a pet) is mid-transition: only a tank ends the episode early, so a rival
        # retarget never hands the off-tank a second full window.
        scan = body(ACTIONS, "BossTauntAfter ai::FindBossTauntAfter(")
        # The episode's transitions are the pure machine's; a tank having the subject back ends it early.
        self.assertIn("TauntEpisodeTick(tauntable, onNonTank, victimIsTank,", scan)
        self.assertIn("TauntEpisodeStep::Regained", scan)
        self.assertIn("state = TauntAfterEpisode();", scan)
        # A creature-charmed tank is out of the order, and the row needs its own key as well (M5, L8).
        self.assertIn("TauntAfterTankUsable(", scan)
        self.assertIn("sPlayerbotAIConfig.dungeonRunCharmYield", scan)
        self.assertIn("TauntAfterTankIndex", POLICY)
        self.assertIn("TauntAfterDue", POLICY)
        # The pure cases are pinned in the policy test file too.
        self.assertIn("CHECK(TauntAfterTankIndex(onlyFirst, 1) == 0);", POLICY_TEST)
        self.assertIn("CHECK(!TauntAfterDue(true, 10000, 10000, false));", POLICY_TEST)


if __name__ == '__main__':
    unittest.main()
