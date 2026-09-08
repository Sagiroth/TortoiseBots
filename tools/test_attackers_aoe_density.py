#!/usr/bin/env python3
"""
Regression and diagnostic test suite for Issue #91:
Target Duplication in AttackersValue and False AoE Clustering in AoeCountValue.

Verifies:
1. Attackers deduplication across shared bot targets ("current target", "old target",
   "attack target", "pull target") ensures 1 mob = count 1.
2. 2 mobs (with duplicated references) produce count 2, not 3+ (AoE threshold of 3 is not met).
3. Real clustered 3 mobs produce count 3, reaching AoE eligibility (>= 3).
4. 3 spread mobs produce max density 1, remaining AoE ineligible.
5. Invariant: attackers collection always contains strictly distinct hostile units.
6. Target ordering preservation: first-seen stable deduplication maintains original sequence
   (never sorts by GUID), preserving target priority and AoE cluster tie-breaking.
"""

import math
import sys
import unittest


class ObjectGuid:
    def __init__(self, high: int, low: int):
        self.high = high
        self.low = low

    def __eq__(self, other):
        if not isinstance(other, ObjectGuid):
            return False
        return self.high == other.high and self.low == other.low

    def __hash__(self):
        return hash((self.high, self.low))

    def __repr__(self):
        return f"Guid({self.high:#x}:{self.low})"

    def is_empty(self):
        return self.high == 0 and self.low == 0


class MockUnit:
    def __init__(self, guid: ObjectGuid, name: str, x: float, y: float, z: float = 0.0, is_valid: bool = True):
        self.guid = guid
        self.name = name
        self.x = x
        self.y = y
        self.z = z
        self.is_valid = is_valid

    def get_guid(self) -> ObjectGuid:
        return self.guid

    def get_distance_2d(self, other: "MockUnit") -> float:
        dx = self.x - other.x
        dy = self.y - other.y
        return math.hypot(dx, dy)


def calculate_attackers_share_targets(
    copied_attackers: list[ObjectGuid],
    other_bot_specific: list[MockUnit | None],
    this_bot_specific: list[MockUnit | None],
    all_units: dict[ObjectGuid, MockUnit],
    get_one: bool = False,
) -> list[ObjectGuid]:
    """
    Python reference simulation of the fixed AttackersValue::Calculate() shareTargets path.
    Preserves existing sequence with first-seen stable deduplication.
    """
    result = list(copied_attackers)

    # Remove bot specific targets of the other bot.
    for target in other_bot_specific:
        if target:
            result = [g for g in result if g != target.get_guid()]

    # Append bot specific targets of this bot in natural priority order.
    for target in this_bot_specific:
        if target:
            result.append(target.get_guid())

    # Validate these targets and enforce the invariant of distinct hostile units,
    # preserving original sequence (first-seen stable deduplication).
    distinct_result: list[ObjectGuid] = []
    seen: set[ObjectGuid] = set()

    for guid in result:
        if guid in seen:
            continue

        target = all_units.get(guid)
        if target and target.is_valid:
            seen.add(guid)
            distinct_result.append(guid)
            if get_one:
                break

    return distinct_result


def find_max_density(
    bot: MockUnit,
    attackers: list[ObjectGuid],
    all_units: dict[ObjectGuid, MockUnit],
    range_limit: float = 100.0,
    aoe_radius: float = 10.0,
) -> list[ObjectGuid]:
    """
    Python reference simulation of the hardened AoeCountValue::FindMaxDensity().
    Preserves attackers priority sequence (first-seen stable deduplication).
    """
    max_count = 0
    max_group: ObjectGuid | None = None
    groups: dict[ObjectGuid, set[ObjectGuid]] = {}

    # Stable first-seen deduplication preserving priority order
    unique_units = []
    seen = set()
    for guid in attackers:
        if guid not in seen:
            seen.add(guid)
            unique_units.append(guid)

    for guid in unique_units:
        unit = all_units.get(guid)
        if not unit:
            continue

        distance_to_player = bot.get_distance_2d(unit)
        if distance_to_player <= range_limit:
            for other_guid in unique_units:
                other = all_units.get(other_guid)
                if not other:
                    continue

                d = unit.get_distance_2d(other)
                if d <= aoe_radius * 2.0:
                    if guid not in groups:
                        groups[guid] = set()
                    groups[guid].add(other_guid)

            if guid in groups and len(groups[guid]) > max_count:
                max_count = len(groups[guid])
                max_group = guid

    if max_count == 0 or max_group is None:
        return []

    # Return cluster members in original attackers priority order
    max_set = groups[max_group]
    return [g for g in unique_units if g in max_set]


def is_aoe_trigger_active(
    bot: MockUnit,
    attackers: list[ObjectGuid],
    all_units: dict[ObjectGuid, MockUnit],
    amount: int = 3,
    range_limit: float = 100.0,
    aoe_radius: float = 10.0,
) -> bool:
    """
    AoeTrigger::IsActive() evaluation: returns aoeEnemies.size() >= amount.
    """
    aoe_enemies = find_max_density(bot, attackers, all_units, range_limit, aoe_radius)
    return len(aoe_enemies) >= amount


class TestAttackersAoeDensity(unittest.TestCase):
    def setUp(self):
        self.bot = MockUnit(ObjectGuid(0x0, 1), "PlayerBot", 0.0, 0.0)
        self.mob1 = MockUnit(ObjectGuid(0xF130, 101), "Defias Rogue", 10.0, 0.0)
        self.mob2 = MockUnit(ObjectGuid(0xF130, 102), "Defias Bandit", 12.0, 2.0)
        self.mob3 = MockUnit(ObjectGuid(0xF130, 103), "Defias Highwayman", 11.0, -1.0)
        self.mob_far = MockUnit(ObjectGuid(0xF130, 104), "Defias Overseer", 60.0, 50.0)

        self.units = {
            self.bot.guid: self.bot,
            self.mob1.guid: self.mob1,
            self.mob2.guid: self.mob2,
            self.mob3.guid: self.mob3,
            self.mob_far.guid: self.mob_far,
        }

    def test_single_mob_deduplication_and_density(self):
        """
        Requirement: 1 mob = count 1.
        Engaged with 1 mob where current target, old target, attack target,
        and pull target all reference mob1.
        """
        copied_attackers = [self.mob1.guid]
        other_bot_targets = [self.mob1]
        # This bot has mob1 in all 4 target slots
        this_bot_targets = [self.mob1, self.mob1, self.mob1, self.mob1]

        attackers = calculate_attackers_share_targets(
            copied_attackers, other_bot_targets, this_bot_targets, self.units
        )

        self.assertEqual(len(attackers), 1, "AttackersValue must deduplicate to exactly 1 hostile unit")
        self.assertEqual(attackers[0], self.mob1.guid)

        density_group = find_max_density(self.bot, attackers, self.units)
        self.assertEqual(len(density_group), 1, "AoeCountValue::FindMaxDensity() must return count 1 for 1 mob")
        self.assertFalse(
            is_aoe_trigger_active(self.bot, attackers, self.units, amount=3),
            "AoE trigger (amount=3) must NOT trigger on 1 mob",
        )

    def test_two_mobs_do_not_trigger_aoe(self):
        """
        Requirement: 2 mobs != 3+.
        Engaged with 2 mobs. Even if both bots duplicate references,
        attackers count must be 2, density must be 2, and AoE trigger (3+) must NOT activate.
        """
        copied_attackers = [self.mob1.guid, self.mob2.guid]
        other_bot_targets = [self.mob1]
        this_bot_targets = [self.mob2, self.mob1, self.mob2, self.mob2]

        attackers = calculate_attackers_share_targets(
            copied_attackers, other_bot_targets, this_bot_targets, self.units
        )

        self.assertEqual(len(attackers), 2, "AttackersValue must contain exactly 2 distinct hostile units")
        self.assertCountEqual(attackers, [self.mob1.guid, self.mob2.guid])

        density_group = find_max_density(self.bot, attackers, self.units)
        self.assertEqual(len(density_group), 2, "Density must be 2 for two nearby mobs")
        self.assertNotEqual(len(density_group), 3, "2 mobs must not inflate to 3+")
        self.assertFalse(
            is_aoe_trigger_active(self.bot, attackers, self.units, amount=3),
            "AoE trigger (amount=3) must NOT trigger on 2 mobs",
        )

    def test_real_clustered_three_mobs_trigger_aoe(self):
        """
        Requirement: real clustered 3 mobs = AoE eligible.
        When 3 mobs are within AoE radius (<= 20 yd pairwise), max density must be 3,
        and AoE trigger (amount=3) must become active.
        """
        copied_attackers = [self.mob1.guid, self.mob2.guid, self.mob3.guid]
        other_bot_targets = [self.mob1]
        this_bot_targets = [self.mob1, self.mob2, self.mob3]

        attackers = calculate_attackers_share_targets(
            copied_attackers, other_bot_targets, this_bot_targets, self.units
        )

        self.assertEqual(len(attackers), 3, "All 3 distinct mobs must be retained")
        self.assertCountEqual(attackers, [self.mob1.guid, self.mob2.guid, self.mob3.guid])

        density_group = find_max_density(self.bot, attackers, self.units)
        self.assertEqual(len(density_group), 3, "Clustered mobs must yield density count 3")
        self.assertTrue(
            is_aoe_trigger_active(self.bot, attackers, self.units, amount=3),
            "Real clustered 3 mobs must be AoE eligible",
        )

    def test_three_spread_mobs_remain_ineligible(self):
        """
        Negative test: 3 mobs that are spread far apart (> aoeRadius * 2)
        must not cluster together, yielding density 1 each and leaving AoE trigger inactive.
        """
        mob_a = MockUnit(ObjectGuid(0xF130, 201), "Mob A", 10.0, 0.0)
        mob_b = MockUnit(ObjectGuid(0xF130, 202), "Mob B", 10.0, 40.0)  # 40 yds away
        mob_c = MockUnit(ObjectGuid(0xF130, 203), "Mob C", 10.0, 80.0)  # 80 yds away
        units = {
            self.bot.guid: self.bot,
            mob_a.guid: mob_a,
            mob_b.guid: mob_b,
            mob_c.guid: mob_c,
        }
        attackers = [mob_a.guid, mob_b.guid, mob_c.guid]

        density_group = find_max_density(self.bot, attackers, units, aoe_radius=10.0)
        self.assertEqual(len(density_group), 1, "Spread mobs must have cluster density 1")
        self.assertFalse(
            is_aoe_trigger_active(self.bot, attackers, units, amount=3, aoe_radius=10.0),
            "Spread mobs must NOT trigger AoE",
        )

    def test_get_one_qualifier(self):
        """
        Verification: attackers::1 returns exactly 1 distinct attacker even with multiple targets.
        """
        copied_attackers = [self.mob1.guid, self.mob2.guid, self.mob3.guid]
        this_bot_targets = [self.mob1, self.mob2]

        attackers = calculate_attackers_share_targets(
            copied_attackers, [], this_bot_targets, self.units, get_one=True
        )

        self.assertEqual(len(attackers), 1, "attackers::1 qualifier must return exactly 1 attacker")

    def test_defensive_find_max_density_with_raw_duplicate_inputs(self):
        """
        Harden requirement: even if input attackers list bypassed deduplication
        and contained 10 copies of the same mob, FindMaxDensity must return exactly 1.
        """
        duplicated_attackers = [self.mob1.guid] * 10
        density_group = find_max_density(self.bot, duplicated_attackers, self.units)

        self.assertEqual(
            len(density_group), 1, "FindMaxDensity must never inflate density on duplicated inputs"
        )
        self.assertFalse(
            is_aoe_trigger_active(self.bot, duplicated_attackers, self.units, amount=3),
            "Duplicated single mob must NEVER trigger AoE",
        )

    def test_target_ordering_is_strictly_preserved(self):
        """
        Sequence requirement: Deduplication must preserve first-seen target sequence,
        never sorting by GUID value.
        """
        # Create mobs whose GUID numerical values are intentionally opposite of insertion order
        high_guid_mob = MockUnit(ObjectGuid(0xFFFF, 999), "HighGuidTarget", 10.0, 0.0)
        mid_guid_mob = MockUnit(ObjectGuid(0x8888, 555), "MidGuidTarget", 10.5, 0.5)
        low_guid_mob = MockUnit(ObjectGuid(0x0001, 111), "LowGuidTarget", 11.0, 1.0)
        units = {
            self.bot.guid: self.bot,
            high_guid_mob.guid: high_guid_mob,
            mid_guid_mob.guid: mid_guid_mob,
            low_guid_mob.guid: low_guid_mob,
        }

        # Sequence ordered by priority: high -> mid -> low (with duplicates mixed in)
        raw_list = [
            high_guid_mob.guid,
            high_guid_mob.guid,
            mid_guid_mob.guid,
            low_guid_mob.guid,
            mid_guid_mob.guid,
        ]

        attackers = calculate_attackers_share_targets(raw_list, [], [], units)
        self.assertEqual(
            attackers,
            [high_guid_mob.guid, mid_guid_mob.guid, low_guid_mob.guid],
            "Attackers sequence must preserve first-seen order, NOT GUID-sorted order",
        )

        density_group = find_max_density(self.bot, attackers, units)
        self.assertEqual(
            density_group,
            [high_guid_mob.guid, mid_guid_mob.guid, low_guid_mob.guid],
            "AoeCountValue::FindMaxDensity must return group members in original priority order",
        )


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(TestAttackersAoeDensity)
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)
