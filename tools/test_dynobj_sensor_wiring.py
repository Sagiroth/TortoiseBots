"""Wiring pins for the dynamic-object (ground cloud) sensor.

A persistent-area-aura dynamic object (Venoxis' Poison Cloud 23861, a mob's
Blizzard/Rain of Fire/Consecration) is a hostile ground circle the bot must step
out of, but the sensor behind "nearest dynamic objects" was a stub that returned
an empty list: a bot could not see any ground cloud in any dungeon or raid.

These pins only prove the live code is wired to the rules in
ai/playerbot/AvoidAoePolicy.h (their behaviour is tested in
tools/test_avoid_aoe_policy.cpp):

 - the dynobj scan visits the grid: MaNGOS::WorldObjectListSearcher visits the
   DynamicObject container, and the check filters TYPEID_DYNAMICOBJECT because
   the searcher calls it for every container type;
 - the "no los" value is registered with the hazard scan range (30 yd), not the
   60 yd sight distance;
 - step-out landing hazards come only from hostile, non-positive circles, so a
   friendly mage's Blizzard never refuses a landing;
 - the melee hold sees those clouds at a reach/chase endpoint too;
 - the reactive "has area debuff" -> flee row sits below the strafe, so the
   step-out wins the reaction pick.

Run: python3 -m unittest tools/test_dynobj_sensor_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STRATEGY = ROOT / "ai/playerbot/strategy"
MOVEMENT = (STRATEGY / "actions/MovementActions.cpp").read_text()
ACTIONS = (STRATEGY / "actions/BossTacticsActions.cpp").read_text()
COMBAT_STRATEGY = (STRATEGY / "generic/CombatStrategy.cpp").read_text()
VALUES = (STRATEGY / "values/NearestGameObjects.cpp").read_text()
VALUE_CONTEXT = (STRATEGY / "values/ValueContext.h").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class DynObjScanWiring(unittest.TestCase):
    def test_the_dynobj_scan_visits_the_grid(self):
        fn = body(VALUES, "std::list<ObjectGuid> NearestDynamicObjects::Calculate()")
        self.assertIn("WorldObjectListSearcher", fn)
        self.assertIn("Cell::VisitAllObjects", fn)
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


class DynObjHazardWiring(unittest.TestCase):
    def test_landing_hazards_skip_friendly_casters(self):
        fn = body(MOVEMENT, "bool AvoidAoeAction::StrafeToSafety(")
        self.assertIn("CollectDamagingDynObjHazards(ai, bot", fn)
        # A circle is built only for a hostile, non-positive caster, and both
        # collectors (cached list, far-point query) share the same filter, so a
        # friendly mage's Blizzard never refuses a landing.
        push = body(VALUES, "void PushDynObjHazard(")
        self.assertIn("IsFriendlyTo", push)
        self.assertIn("DynObjIsHostileHazard", push)
        for caller in ("void ai::CollectDamagingDynObjHazards(",
                       "void ai::CollectDamagingDynObjHazardsAt("):
            self.assertIn("PushDynObjHazard(", body(VALUES, caller))


class DynObjHoldWiring(unittest.TestCase):
    def test_the_hold_sees_dynobjs(self):
        fn = body(ACTIONS, "float TrapHoldMultiplier::GetValue(")
        self.assertIn("CollectDamagingDynObjHazardsAt(", fn)


class FleeRowWiring(unittest.TestCase):
    def test_the_flee_row_sits_below_the_strafe(self):
        i = COMBAT_STRATEGY.rindex('"has area debuff"')
        row = COMBAT_STRATEGY[i:i + 200]
        self.assertIn("ACTION_EMERGENCY + 4", row)
        self.assertNotIn("ACTION_EMERGENCY + 5", row)


if __name__ == "__main__":
    unittest.main()
