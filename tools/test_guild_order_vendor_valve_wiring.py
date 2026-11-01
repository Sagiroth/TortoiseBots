"""Guild-order bag-pressure vendor valve wiring (SPEC-8 gap 7).

The bag-pressure journey for a bot with a running guild order must pass the
relief rule (BagPressureSaleRelievesBags) scoped to running orders only, so a
bot whose bags are held full by order stock does not walk to town for every
looted grey.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = (ROOT / "ai/playerbot/strategy/values/MaintenanceValues.cpp").read_text()


def valve_slice():
    start = SRC.index('bool ai::BagPressureVendorTrip(PlayerbotAI* ai)')
    end = SRC.index("\n}\n", start)
    return SRC[start:end]


def helper_body():
    start = SRC.index("static uint32 SellableVendorSlots(")
    end = SRC.index("\n}\n", start)
    return SRC[start:end]


class GuildOrderVendorValveWiringTest(unittest.TestCase):
    def test_valve_calls_relief_rule_in_order(self):
        body = valve_slice()
        self.assertIn("BagPressureSaleRelievesBags(", body)
        self.assertLess(body.index('AI_VALUE(bool, "can sell")'),
                        body.index("BagPressureSaleRelievesBags("))
        self.assertLess(body.index("BagPressureSaleRelievesBags("),
                        body.index("VendorWithinNearbyServiceRange"))

    def test_valve_scoped_to_running_order(self):
        body = valve_slice()
        self.assertIn("GuildOrders::Find(", body)
        self.assertIn("IsFinished(", body)
        self.assertIn("->cancelled", body)

    def test_valve_uses_pressure_constant(self):
        self.assertIn("NEARBY_SERVICE_BAG_PRESSURE", valve_slice())

    def test_full_bag_trip_is_rate_limited(self):
        # Codex round 2: full -> sell one -> loot one grey -> full must not loop.
        body = valve_slice()
        self.assertIn('"order full-bag vendor trip"', body)
        self.assertIn("30 * MINUTE", body)

    def test_sellable_slots_counts_slots(self):
        body = helper_body()
        self.assertIn("SellPrice", body)
        self.assertIn("ITEM_USAGE_VENDOR", body)
        self.assertNotIn("GetCount()", body)


if __name__ == "__main__":
    unittest.main()
