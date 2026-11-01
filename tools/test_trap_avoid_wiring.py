"""Wiring pins for ground-fire avoidance (trap GOs such as Jeklik's Inferno 177764).

A trap GO's data2 radius is its activation radius, not the reach of its spell:
Zul'Gurub's Inferno casts Flames 19628, school damage at the source with
SpellRadius index 8 = 5 yd. The rules live in ai/playerbot/AvoidAoePolicy.h
(TrapHazardRadius, InsideAoeHazard, ReachEndpoint, AoeFallbackHeadings,
ChooseClearAoeLanding, HazardsHoldMove; their behaviour is tested in
tools/test_avoid_aoe_policy.cpp). These pins only prove the live code calls them:

 - the hazard radius comes from the trap spell's effect radius through
   sSpellRadiusStore (the GO's data2 radius is the fallback when the spell has
   no radius), and only for an effect that is damage at the source;
 - the step-out sensor (AvoidAoeAction) uses that radius and the same
   centre-to-centre test as the landing and the hold, so a bot cannot walk back
   and bounce out every tick;
 - the trap scan is wide enough for a held destination (30 yd), and a
   destination beyond it gets its own query around the point;
 - a step-out landing is rejected inside any damaging trap (and any damaging
   dynamic object), not only inside creature keep-outs; when the ordered
   candidates all stand in fire the search widens (16 headings, three step
   lengths) and only a landing clear of every fire is taken - nothing clear and
   pathable, the bot stays put;
 - a move whose destination lies in a fire is held (trap hold), the same shape
   as the avoid_cast hold, in combat only; attacks are never held and a reach
   is judged at the point where it stops, not at the target's centre; the
   step-out itself is never held. The hold hangs on the always-on strategies
   (avoid aoe, dungeon), so an uncommanded bot is covered too.

Run: python3 -m unittest tools/test_trap_avoid_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STRATEGY = ROOT / "ai/playerbot/strategy"
MOVEMENT = (STRATEGY / "actions/MovementActions.cpp").read_text()
ACTIONS = (STRATEGY / "actions/BossTacticsActions.cpp").read_text()
ACTIONS_H = (STRATEGY / "actions/BossTacticsActions.h").read_text()
TACTICS_STRATEGY = (STRATEGY / "generic/BossTacticsStrategy.cpp").read_text()
COMBAT_STRATEGY = (STRATEGY / "generic/CombatStrategy.cpp").read_text()
DUNGEON_STRATEGY = (STRATEGY / "generic/DungeonStrategy.cpp").read_text()
VALUES = (STRATEGY / "values/NearestGameObjects.cpp").read_text()
VALUES_H = (STRATEGY / "values/NearestGameObjects.h").read_text()
POLICY = (ROOT / "ai/playerbot/AvoidAoePolicy.h").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class TrapHazardRadiusWiring(unittest.TestCase):
    def test_radius_comes_from_the_trap_spell(self):
        fn = body(VALUES, "float ai::DamagingTrapHazardRadius(")
        self.assertIn("goInfo->trap.spellId", fn)
        self.assertIn("sServerFacade.LookupSpellInfo(", fn)
        self.assertIn("sSpellRadiusStore.LookupEntry(spellProto->EffectRadiusIndex[i])", fn)
        self.assertIn("TrapHazardRadius(spellRadius, trapRadius)", fn)
        # Only damage at the source widens the circle (Flames 19628: school
        # damage, TargetA/TargetB = the source AoE pair).
        self.assertIn("SPELL_EFFECT_SCHOOL_DAMAGE", fn)
        self.assertIn("SPELL_AURA_PERIODIC_DAMAGE", fn)
        self.assertIn("kAoeAtSourceTargetA", fn)
        self.assertIn("kAoeAtSourceTargetB", fn)
        # The GO's activation radius stays the fallback.
        self.assertIn("goInfo->trap.radius + go->GetCombatReach()", fn)
        self.assertLess(fn.index("float const trapRadius ="), fn.index("if (!spellProto)"))
        self.assertIn("return trapRadius;", fn)

    def test_policy_owns_the_margin_and_the_source_targets(self):
        self.assertIn("inline float TrapHazardRadius(float spellRadiusYd, float trapRadius)", POLICY)
        self.assertIn("kTrapHazardMarginYd = 2.0f", POLICY)
        self.assertIn("kAoeAtSourceTargetA = 22", POLICY)
        self.assertIn("kAoeAtSourceTargetB = 15", POLICY)
        self.assertIn("inline bool InsideAoeHazard(", POLICY)
        self.assertIn("inline bool HazardsHoldMove(", POLICY)
        self.assertIn("inline AoePoint ReachEndpoint(", POLICY)
        self.assertIn("inline int ChooseClearAoeLanding(", POLICY)
        self.assertIn("inline void AoeFallbackHeadings(", POLICY)
        # The least-bad landing is gone: a fallback landing must be clear.
        self.assertNotIn("ChooseAoeFallback", POLICY)
        self.assertIn("kAoeFallbackStepYd = 12.0f", POLICY)
        self.assertIn("kAoeFallbackWideStepYd = 20.0f", POLICY)
        self.assertIn("kAoeFallbackHeadings = 16", POLICY)
        self.assertIn("kTrapScanRangeYd = 30.0f", POLICY)

    def test_the_listed_traps_become_hazard_circles(self):
        fn = body(VALUES, "void ai::CollectDamagingTrapHazards(")
        self.assertIn('AI_VALUE(std::list<ObjectGuid>, "nearest damaging traps")', fn)
        self.assertIn("DamagingTrapHazardRadius(go)", fn)
        self.assertIn("hazard.radius", fn)
        self.assertIn("out.push_back(hazard)", fn)
        # One cached value read and GO lookups: no grid sweep per tick here.
        self.assertNotIn("VisitAllObjects", fn)
        self.assertIn("void CollectDamagingTrapHazards(", VALUES_H)

    def test_the_scan_covers_the_step_out_and_the_hold(self):
        fn = body(VALUES, "std::list<ObjectGuid> NearestDamagingTrapsValue::Calculate(")
        # 15 yd held only the fire under the bot; a held destination can stand
        # in the next one.
        self.assertIn("kTrapScanRangeYd", fn)
        self.assertNotIn("kMaxAoeAvoidRadiusYd", fn)
        for call in ("u_check(bot, kTrapScanRangeYd)", "searcher, kTrapScanRangeYd)"):
            self.assertIn(call, fn)

    def test_a_held_destination_gets_hazards_around_its_own_point(self):
        self.assertIn("void ai::CollectDamagingTrapHazardsAt(", VALUES)
        self.assertIn("void CollectDamagingTrapHazardsAt(", VALUES_H)
        fn = body(VALUES, "void ai::CollectDamagingTrapHazardsAt(")
        # Near the bot the cached list covers it; a destination beyond the
        # list gets one bounded query around the point, never per-tick grid
        # loading.
        self.assertIn("kTrapScanRangeYd", fn)
        self.assertIn("CollectDamagingTrapHazards(ai, bot, out)", fn)
        self.assertIn("Cell::VisitAllObjects(", fn)
        self.assertIn(", true)", fn)


class TrapSensorWiring(unittest.TestCase):
    def test_the_step_out_uses_the_spell_circle(self):
        fn = body(MOVEMENT, "bool AvoidAoeAction::AvoidDamagingTrap(")
        self.assertIn("DamagingTrapHazardRadius(go)", fn)
        self.assertNotIn("goInfo->trap.radius", fn)
        self.assertIn("IsAvoidableAoeRadius(radius)", fn)
        self.assertIn("InsideAoeHazard(circle, bot->GetPositionX()", fn)
        self.assertNotIn("bot->GetDistance(go)", fn)

    def test_every_sensor_uses_the_same_centre_metric(self):
        for sensor in ("bool AvoidAoeAction::AvoidAuraWithDynamicObj(",
                       "bool AvoidAoeAction::AvoidDamagingTrap(",
                       "bool AvoidAoeAction::AvoidTriggerNpc("):
            fn = body(MOVEMENT, sensor)
            self.assertIn("InsideAoeHazard(", fn)
        self.assertIn("InsideAoeHazard(circle, bot->GetPositionX()",
                      body(MOVEMENT, "bool AvoidAoeAction::AvoidAuraWithDynamicObj("))
        self.assertNotIn("bot->GetDistance(dyn)",
                         body(MOVEMENT, "bool AvoidAoeAction::AvoidAuraWithDynamicObj("))
        trigger = body(MOVEMENT, "bool AvoidAoeAction::AvoidTriggerNpc(")
        self.assertIn("InsideAoeHazard(", trigger)
        self.assertNotIn("bot->GetDistance(unit) > radius", trigger)


class LandingWiring(unittest.TestCase):
    def test_a_landing_inside_another_fire_is_rejected(self):
        fn = body(MOVEMENT, "bool AvoidAoeAction::StrafeToSafety(")
        self.assertIn("CollectDamagingTrapHazards(ai, bot, damagingHazards)", fn)
        self.assertIn('"nearest dynamic objects no los"', fn)
        self.assertIn("dyn->GetRadius()", fn)
        check = "if (LandingInsideHazard(damagingHazards, cand.getX(), cand.getY(), cand.getZ()))"
        self.assertIn(check, fn)
        # Built once, checked for every candidate, before the landing is accepted.
        self.assertLess(fn.index("CollectDamagingTrapHazards("), fn.index(check))
        self.assertLess(fn.index(check), fn.index("WorldPosition out = cand;"))

    def test_no_clearing_landing_widens_the_search_then_stays_put(self):
        fn = body(MOVEMENT, "bool AvoidAoeAction::StrafeToSafety(")
        # The ordered candidates all stood in fire: a full ring of headings at
        # three step lengths, nearest first.
        self.assertIn("AoeFallbackHeadings(headings)", fn)
        self.assertIn("float const steps[3] = { step, kAoeFallbackStepYd, kAoeFallbackWideStepYd };", fn)
        # Candidates still need sight and a path, and only a landing that clears
        # every fire (and the one being left) is taken.
        collect = fn[fn.index("float const steps[3]"):fn.index("ChooseClearAoeLanding(")]
        self.assertIn("IsWithinLOS(", collect)
        self.assertIn("canPathTo(cand, bot)", collect)
        self.assertIn("AoeHazardCircle const current{ hazardCenter.getX()", collect)
        self.assertIn("ChooseClearAoeLanding(damagingHazards, current, landings.data()", fn)
        self.assertNotIn("ChooseAoeFallback", MOVEMENT)
        # Nothing clear and pathable: the widened search is the last word; the
        # function gives up and the bot stays where it is.
        tail = fn[fn.index("ChooseClearAoeLanding(damagingHazards"):]
        self.assertNotIn("CreatureAuraStepAwayPoint", tail)
        self.assertEqual(tail.count("MoveTo("), 1)
        self.assertIn("return false;", tail)


class TrapHoldWiring(unittest.TestCase):
    def test_the_multiplier_is_registered_on_the_always_on_strategies(self):
        self.assertIn("class TrapHoldMultiplier : public Multiplier", ACTIONS_H)
        self.assertIn('Multiplier(ai, "trap hold")', ACTIONS_H)
        # The generic strategy that owns AvoidAoeAction carries it...
        avoid_aoe = body(COMBAT_STRATEGY, "void AvoidAoeStrategy::InitCombatMultipliers(")
        self.assertIn("multipliers.push_back(new TrapHoldMultiplier(ai));", avoid_aoe)
        # ...and so does the always-on combat strategy, because multipliers are
        # per engine (the avoid-aoe strategy is added to the reaction engine
        # only, so its list never sees the combat engine's reach/follow/stay).
        self.assertIn("multipliers.push_back(new TrapHoldMultiplier(ai));",
                      body(DUNGEON_STRATEGY, "void DungeonStrategy::InitCombatMultipliers("))
        self.assertIn("void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;",
                      (STRATEGY / "generic/DungeonStrategy.h").read_text())
        # Boss tactics no longer is the (only) path: the hold is not tied to an
        # encounter, and no engine carries a second copy.
        combat = body(TACTICS_STRATEGY, "void BossTacticsStrategy::InitCombatMultipliers(")
        self.assertNotIn("TrapHoldMultiplier", combat)
        self.assertIn("multipliers.push_back(new BossTacticsCastHoldMultiplier(ai));", combat)
        non_combat = body(TACTICS_STRATEGY, "void BossTacticsStrategy::InitNonCombatMultipliers(")
        self.assertNotIn("TrapHoldMultiplier", non_combat)
        self.assertIn("multipliers.push_back(new BossTacticsCastHoldMultiplier(ai));", non_combat)

    def test_attacks_are_never_held(self):
        fn = body(ACTIONS, "float TrapHoldMultiplier::GetValue(")
        self.assertIn("dynamic_cast<AttackAction*>(action)", fn)
        # Before any hazard read: target pick-up and engage are not walks into fire.
        self.assertLess(fn.index("dynamic_cast<AttackAction*>(action)"),
                        fn.index("CollectDamagingTrapHazards"))

    def test_the_hold_is_combat_only(self):
        fn = body(ACTIONS, "float TrapHoldMultiplier::GetValue(")
        self.assertIn("!bot->IsInCombat()", fn)
        self.assertLess(fn.index("!bot->IsInCombat()"), fn.index("CollectDamagingTrapHazards"))
        self.assertIn("dynamic_cast<ReturnToStayPositionAction*>(action)", fn)
        self.assertIn("dynamic_cast<FollowAction*>(action)", fn)

    def test_a_reach_is_judged_at_its_endpoint(self):
        fn = body(ACTIONS, "float TrapHoldMultiplier::GetValue(")
        self.assertIn("dynamic_cast<ReachTargetAction*>(action)", fn)
        self.assertIn("GetStopRange()", fn)
        self.assertIn("ReachEndpoint(", fn)
        self.assertIn("CollectDamagingTrapHazardsAt(", fn)
        self.assertIn("HazardsHoldMove(false, false, true, endpoint", fn)
        self.assertNotIn("HazardsHoldMove(false, target == bot, true, target->GetPositionX()", fn)

    def test_the_stay_and_follow_spots_are_destinations(self):
        fn = body(ACTIONS, "float TrapHoldMultiplier::GetValue(")
        self.assertIn('AI_VALUE(PositionMap&, "position")', fn)
        self.assertIn("HazardsHoldMove(false, false, true, stay->second.x, stay->second.y, stay->second.z",
                      fn)
        self.assertIn('AI_VALUE(WorldPosition, "formation position")', fn)
        self.assertIn("HazardsHoldMove(false, false, true, spot.getX(), spot.getY(), spot.getZ()", fn)
        # Each is checked against the hazards around its own point.
        self.assertGreaterEqual(fn.count("CollectDamagingTrapHazardsAt("), 3)

    def test_the_step_out_itself_is_never_held(self):
        fn = body(ACTIONS, "float TrapHoldMultiplier::GetValue(")
        self.assertIn("dynamic_cast<AvoidAoeAction*>(action)", fn)
        self.assertIn("dynamic_cast<BossTacticsAvoidCastAction*>(action)", fn)
        self.assertLess(fn.index("dynamic_cast<AvoidAoeAction*>(action)"), fn.index("CollectDamagingTrapHazards"))
        self.assertIn("class AvoidAoeAction : public MovementAction", (STRATEGY / "actions/MovementActions.h").read_text())


class TrapHoldEndpointReviewTest(unittest.TestCase):
    """Round 3: the hold projects Execute's real chase distance, and the destination check leaves the cached list
    one hazard reach inside its edge."""

    def test_stop_range_is_the_chase_distance(self):
        reach = (ROOT / "ai/playerbot/strategy/actions/ReachTargetActions.h").read_text()
        i = reach.index("float GetStopRange() const")
        body = reach[i:i + 300]
        self.assertIn("range * 0.75f - sPlayerbotAIConfig.contactDistance", body)

    def test_destination_check_uses_a_margin_inside_the_list(self):
        cpp = (ROOT / "ai/playerbot/strategy/values/NearestGameObjects.cpp").read_text()
        self.assertIn("kTrapDestMarginYd = 15.0f", cpp)
        self.assertIn("kTrapScanRangeYd - kTrapDestMarginYd", cpp)


if __name__ == "__main__":
    unittest.main()
