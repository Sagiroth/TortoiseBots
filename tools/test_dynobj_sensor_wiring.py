"""Wiring pins for the dynamic-object (ground cloud) sensor.

A persistent-area-aura dynamic object (Venoxis' Poison Cloud 23861, a mob's
Blizzard/Rain of Fire/Consecration) is a hostile ground circle the bot must step
out of, but the sensor behind "nearest dynamic objects" was a stub that returned
an empty list: a bot could not see any ground cloud in any dungeon or raid.

These pins only prove the live code is wired to the rules in
ai/playerbot/AvoidAoePolicy.h (their behaviour is tested in
tools/test_avoid_aoe_policy.cpp):

 - the dynobj scan visits the grid container only (dynamic objects live nowhere
   else), and the check filters TYPEID_DYNAMICOBJECT because the searcher calls
   it for every container type;
 - the "no los" value is registered with the hazard scan range (30 yd), not the
   60 yd sight distance;
 - step-out landing hazards come only from hostile, non-positive circles, so a
   friendly mage's Blizzard never refuses a landing, and the dynobj circles are
   merged into the landing list the strafe actually checks;
 - the tank drag uses the policy step and its landing test, and the caller skips
   the drag when the cap is below the landing test's need;
 - the proactive step-out sensor and the melee hold see creature-cast clouds
   only (open world / PvP against players is unchanged); the hold has no tank
   exemption, so a tank whose boss stays in the cloud does not walk back in;
 - the fallback flee of the "has area debuff" row is its own action ("flee from
   area"): only it is skipped while a hostile cloud covers the bot (the strafe
   owns that case and a plain flee ignores the landing filter) and for a tank
   that holds its boss; the shared "flee" action has no such clause, so panic,
   outnumbered, critical-health and range-control flees still work for a tank;
 - the "aoe threat nearby" trigger gets a proactive dynobj centre test, so the
   step fires before the cloud's first tick;
 - the reactive "has area debuff" -> "flee from area" row sits below the strafe,
   so the step-out wins the reaction pick.

Run: python3 -m unittest tools/test_dynobj_sensor_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STRATEGY = ROOT / "ai/playerbot/strategy"
MOVEMENT = (STRATEGY / "actions/MovementActions.cpp").read_text()
MOVEMENT_H = (STRATEGY / "actions/MovementActions.h").read_text()
ACTION_CONTEXT = (STRATEGY / "actions/ActionContext.h").read_text()
FLEE_STRATEGY = (STRATEGY / "generic/FleeStrategy.cpp").read_text()
ACTIONS = (STRATEGY / "actions/BossTacticsActions.cpp").read_text()
COMBAT_STRATEGY = (STRATEGY / "generic/CombatStrategy.cpp").read_text()
TRIGGERS = (STRATEGY / "triggers/GenericTriggers.cpp").read_text()
VALUES = (STRATEGY / "values/NearestGameObjects.cpp").read_text()
VALUE_CONTEXT = (STRATEGY / "values/ValueContext.h").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class DynObjScanWiring(unittest.TestCase):
    def test_the_dynobj_scan_visits_the_grid(self):
        fn = body(VALUES, "std::list<ObjectGuid> NearestDynamicObjects::Calculate()")
        self.assertIn("WorldObjectListSearcher", fn)
        # A dynamic object lives only in the grid container; the world
        # containers (players, creatures, corpses) never hold one.
        self.assertIn("Cell::VisitGridObjects(", fn)
        self.assertNotIn("Cell::VisitAllObjects(", fn)
        self.assertNotIn("Leave the list", fn)
        self.assertNotIn("Penqle has no", fn)
        # WorldObjectListSearcher calls the check for every container type
        # (player, creature, corpse, game object, dynamic object), so the check
        # takes a WorldObject* and filters by type id - the
        # MaNGOS::AllWorldObjectsInRange shape.
        check = body(VALUES, "class DynamicObjectsInRangeCheck", "\n};\n")
        self.assertIn("operator()(WorldObject* u)", check)
        self.assertIn("TYPEID_DYNAMICOBJECT", check)

    def test_the_scan_range_is_the_hazard_range(self):
        rows = [line for line in VALUE_CONTEXT.splitlines()
                if '"nearest dynamic objects no los"' in line]
        self.assertEqual(len(rows), 1)
        self.assertIn("kTrapScanRangeYd", rows[0])

    def test_the_far_point_dynobj_query_visits_the_grid(self):
        fn = body(VALUES, "void ai::CollectDamagingDynObjHazardsAt(")
        self.assertIn("Cell::VisitGridObjects(", fn)
        self.assertNotIn("Cell::VisitAllObjects(", fn)


class DynObjHazardWiring(unittest.TestCase):
    def test_landing_hazards_skip_friendly_casters(self):
        fn = body(MOVEMENT, "bool AvoidAoeAction::StrafeToSafety(")
        self.assertIn("CollectDamagingDynObjHazards(ai, bot", fn)
        # A circle is built only for a hostile, non-positive caster, and both
        # collectors (cached list, far-point query) share the same filter, so a
        # friendly mage's Blizzard never refuses a landing. The friendly
        # exemption is decided against a player-controlled caster.
        push = body(VALUES, "void PushDynObjHazard(")
        self.assertIn("IsFriendlyTo", push)
        self.assertIn("DynObjIsHostileHazard", push)
        self.assertIn("GetCharmerOrOwnerPlayerOrPlayerItself", push)
        for caller in ("void ai::CollectDamagingDynObjHazards(",
                       "void ai::CollectDamagingDynObjHazardsAt("):
            self.assertIn("PushDynObjHazard(", body(VALUES, caller))

    def test_the_landing_list_merges_the_dynobj_circles(self):
        fn = body(MOVEMENT, "bool AvoidAoeAction::StrafeToSafety(")
        self.assertIn(
            "damagingHazards.insert(damagingHazards.end(), dynObjHazards.begin(), dynObjHazards.end())", fn)
        check = "if (LandingInsideHazard(damagingHazards, cand.getX(), cand.getY(), cand.getZ()))"
        # Merged before any landing is checked against it.
        self.assertLess(fn.index("damagingHazards.insert("), fn.index(check))

    def test_the_tank_drag_uses_the_policy(self):
        fn = body(MOVEMENT, "bool AvoidAoeAction::StrafeToSafety(")
        self.assertIn("TankDragStep(", fn)
        self.assertIn("TankLandingDragsTargetOut(", fn)
        # The drag is skipped, not attempted with an unsatisfiable step, when
        # the cap is below what the landing test needs.
        self.assertIn("TankDragFitsCap(", fn)

    def test_the_proactive_step_is_creature_clouds_only(self):
        fn = body(MOVEMENT, "bool AvoidAoeAction::AvoidAuraWithDynamicObj(")
        self.assertIn("CollectDamagingDynObjHazards(ai, bot, clouds, true)", fn)
        self.assertIn("InsideAoeHazard(circle, bot->GetPositionX()", fn)

    def test_the_area_flee_is_skipped_under_a_hostile_cloud(self):
        fn = body(MOVEMENT, "bool FleeFromAreaAction::isUseful()")
        self.assertIn("CollectDamagingDynObjHazards(ai, bot, clouds, true)", fn)
        self.assertIn("InsideAoeHazard(cloud, bot->GetPositionX()", fn)

    def test_the_area_flee_skips_an_active_tank(self):
        fn = body(MOVEMENT, "bool FleeFromAreaAction::isUseful()")
        self.assertIn("PlayerbotAI::IsTank(bot) && target && target->GetVictim() == bot", fn)

    def test_the_shared_flee_has_no_tank_or_cloud_clause(self):
        # The shared "flee" serves panic, outnumbered, critical health and range
        # control: it must stay as before this branch, with no isUseful override.
        self.assertNotIn("FleeAction::isUseful", MOVEMENT)
        i = MOVEMENT_H.index("class FleeAction : public MovementAction")
        decl = MOVEMENT_H[i:MOVEMENT_H.index("};", i)]
        self.assertNotIn("isUseful", decl)

    def test_the_area_flee_is_a_flee_action_registered_live(self):
        i = MOVEMENT_H.index("class FleeFromAreaAction")
        decl = MOVEMENT_H[i:MOVEMENT_H.index("};", i)]
        self.assertIn("public FleeAction", decl)
        self.assertIn('"flee from area"', decl)
        self.assertIn("virtual bool isUseful() override;", decl)
        self.assertIn('creators["flee from area"] = [](PlayerbotAI* ai) { return new FleeFromAreaAction(ai); };', ACTION_CONTEXT)
        # The flee dampener keeps covering the area flee it used to dampen.
        self.assertIn('"flee from area"', FLEE_STRATEGY)

    def test_the_trigger_steps_out_proactively(self):
        fn = body(TRIGGERS, "bool AoeThreatNearbyTrigger::IsActive()")
        # The step fires as the cloud forms, on the centre test, not only once
        # the aura has landed ("has area debuff").
        self.assertIn("CollectDamagingDynObjHazards(ai, bot, clouds, true)", fn)
        self.assertIn("InsideAoeHazard(cloud, bot->GetPositionX()", fn)


class DynObjHoldWiring(unittest.TestCase):
    def test_the_hold_sees_dynobjs(self):
        fn = body(ACTIONS, "float TrapHoldMultiplier::GetValue(")
        self.assertIn("CollectDamagingDynObjHazardsAt(", fn)

    def test_the_hold_has_no_tank_exemption_for_clouds(self):
        fn = body(ACTIONS, "float TrapHoldMultiplier::GetValue(")
        # A tank whose boss stays in the cloud must be held at the edge too: it
        # is the drag, not a walk back in, that gets the boss out.
        self.assertNotIn("PlayerbotAI::IsTank(bot) && target->GetVictim() == bot", fn)
        self.assertIn("CollectDamagingDynObjHazardsAt(ai, bot, endpoint.x, endpoint.y, endpoint.z, dynObjHazards, true)", fn)


class FleeRowWiring(unittest.TestCase):
    def test_the_area_row_names_the_area_flee(self):
        i = COMBAT_STRATEGY.rindex('"has area debuff"')
        row = COMBAT_STRATEGY[i:i + 200]
        self.assertIn('new NextAction("flee from area"', row)
        self.assertNotIn('new NextAction("flee",', row)

    def test_the_flee_row_sits_below_the_strafe(self):
        i = COMBAT_STRATEGY.rindex('"has area debuff"')
        row = COMBAT_STRATEGY[i:i + 200]
        self.assertIn("ACTION_EMERGENCY + 4", row)
        self.assertNotIn("ACTION_EMERGENCY + 5", row)


if __name__ == "__main__":
    unittest.main()
