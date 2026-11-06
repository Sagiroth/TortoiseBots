#include "../ai/playerbot/HealingTriagePolicy.h"

#include <algorithm>
#include <cstdlib>
#include <vector>
#include <iostream>
#include <limits>

#define CHECK(x) do { if (!(x)) { std::cerr << "line " << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)

using namespace ai;

int main()
{
    HealingBands const bands{20, 50, 70, 90};
    HealingCandidate tank{1, 95000, 100000, 0, true, false, false};
    HealingCandidate caster{2, 300, 3000, 0, false, false, false};
    CHECK(!NeedsPartyHealing(tank, bands));
    CHECK(NeedsPartyHealing(caster, bands));
    CHECK(HealingPriority(caster, tank, bands)); // percent emergency beats a larger raw deficit

    tank.maxHealth = 10000;
    tank.health = 1900;
    caster.health = 60;
    CHECK(HealingPriority(caster, tank, bands)); // a 2% caster beats a 19% tank
    tank.health = 2500;
    caster.health = 900;
    CHECK(HealingPriority(tank, caster, bands)); // tank preference within the low band
    caster.health = 300;
    CHECK(HealingPriority(caster, tank, bands)); // tank bonus never crosses an urgency band

    HealingCandidate pet{3, 1000, 1000, 0, false, true, false};
    CHECK(!NeedsPartyHealing(pet, bands));
    pet.health = 1;
    CHECK(NeedsPartyHealing(pet, bands));
    caster.health = 2400;
    CHECK(HealingPriority(caster, pet, bands)); // pets cannot consume injured players' slots
    pet.health = 800;
    pet.incomingHeal = true;
    CHECK(!NeedsPartyHealing(pet, bands));

    caster.incomingHeal = true;
    CHECK(!NeedsPartyHealing(caster, bands)); // don't duplicate a routine heal in flight
    caster.health = 1200;
    CHECK(NeedsPartyHealing(caster, bands)); // but still help a member in danger
    caster.health = 2100;
    CHECK(!NeedsPartyHealing(caster, bands)); // medium boundary is not an emergency
    caster.incomingHeal = false;
    CHECK(NeedsPartyHealing(caster, bands));
    caster.health = 2700;
    CHECK(!NeedsPartyHealing(caster, bands));

    tank.health = tank.maxHealth;
    tank.incomingDamage = 6000;
    CHECK(NeedsPartyHealing(tank, bands)); // intentional preheal remains supported
    CHECK(PredictedHealHealth(tank) == 40.0f);
    tank.incomingDamage = std::numeric_limits<std::uint32_t>::max();
    CHECK(PredictedHealHealth(tank) == 0.0f); // no unsigned wrap
    tank.health = 0;
    CHECK(!NeedsPartyHealing(tank, bands));
    tank.maxHealth = 0;
    CHECK(!NeedsPartyHealing(tank, bands));
    CHECK(PredictedHealHealth(tank) == 100.0f); // no divide by zero

    tank = {1, 4000, 10000, 0, true, false, false};
    caster = {2, 300, 3000, 0, false, false, false};
    std::vector<HealingCandidate> raid{tank, caster};
    std::sort(raid.begin(), raid.end(), [&](auto const& a, auto const& b) { return HealingPriority(a, b, bands); });
    for (std::size_t healer = 0; healer < 12; ++healer)
        CHECK(raid[HealingAssignment(raid, bands, healer)].guid == 2); // all help the lone emergency
    tank.health = 1000;
    raid = {caster, tank};
    std::sort(raid.begin(), raid.end(), [&](auto const& a, auto const& b) { return HealingPriority(a, b, bands); });
    CHECK(raid[HealingAssignment(raid, bands, 0)].guid == 1); // tied emergency: tank first
    CHECK(raid[HealingAssignment(raid, bands, 1)].guid == 2);
    CHECK(raid[HealingAssignment(raid, bands, 2)].guid == 1);
    CHECK(HealingAssignment({}, bands, 8) == 0);

    auto equal = caster;
    equal.guid = 4;
    CHECK(HealingPriority(caster, equal, bands)); // reproducible tie, not roster insertion order
    CHECK(!HealingPriority(equal, caster, bands));
    CHECK(!HealingPriority(caster, caster, bands));
    // t1105h J1: a member a Shade of Jin'do hunts (Shadow Shock 602 every 2 s and its melee) is healed one band earlier
    // and with the tank's in-band lead; an emergency elsewhere still comes first.
    HealingCandidate hunted{10, 5500, 10000, 0, false, false, false};  // 55 %: the medium band
    HealingCandidate dps{11, 4800, 10000, 0, false, false, false};     // 48 %: the low band
    CHECK(HealingPriority(dps, hunted, bands));          // unhunted: the lower band first, as before
    hunted.hunted = true;
    CHECK(HealingUrgency(hunted, bands) == 1);
    CHECK(HealingPriority(hunted, dps, bands));          // hunted: same band, and its lead wins
    HealingCandidate critical{12, 1500, 10000, 0, false, false, false}; // 15 %: critical
    HealingCandidate huntedLow{13, 3000, 10000, 0, false, false, false, true, true}; // 30 %: low -> critical
    CHECK(HealingUrgency(huntedLow, bands) == 0);
    CHECK(HealingPriority(critical, huntedLow, bands));  // in an emergency the lowest health wins
    HealingCandidate huntedTop{14, 9950, 10000, 0, false, false, false, true, true};
    CHECK(!NeedsPartyHealing(huntedTop, bands));         // the weighting never makes a full member a heal target
    // Opus 1 (t1105h review): the boost never lifts a non-tank over a tank in a more urgent band. CapHuntedBoost runs on
    // the whole candidate list before the sort: a hunted member whose raw band a tank's band equals or beats keeps only
    // the in-band lead, so the order stays a per-candidate key (the sort's comparator stays transitive).
    {
        HealingBands const live{25, 45, 65, 90};
        auto order = [&](std::vector<HealingCandidate> list)
        {
            CapHuntedBoost(list, live);
            std::sort(list.begin(), list.end(), [&](HealingCandidate const& l, HealingCandidate const& r)
                { return HealingPriority(l, r, live); });
            std::vector<std::uint64_t> guids;
            for (HealingCandidate const& c : list)
                guids.push_back(c.guid);
            return guids;
        };
        HealingCandidate tank26{20, 2600, 10000, 0, true, false, false};   // 26 %: low
        HealingCandidate hunted44{21, 4400, 10000, 0, false, false, false, true, true}; // 44 %: low
        HealingCandidate tank40{22, 4000, 10000, 0, true, false, false};   // 40 %: low
        HealingCandidate hunted35{23, 3500, 10000, 0, false, false, false, true, true};
        HealingCandidate tank60{24, 6000, 10000, 0, true, false, false};   // 60 %: medium
        HealingCandidate dps26{25, 2600, 10000, 0, false, false, false};   // 26 %: low
        // The 44 % hunted DPS stays behind the 26 % tank: it is not lifted into a more urgent band than the tank's.
        CHECK((order({hunted44, tank26}) == std::vector<std::uint64_t>{20, 21}));
        {
            std::vector<HealingCandidate> two{hunted44, tank26};
            CapHuntedBoost(two, live);
            CHECK(HealingUrgency(two[0], live) == 1);
            std::sort(two.begin(), two.end(), [&](HealingCandidate const& l, HealingCandidate const& r)
                { return HealingPriority(l, r, live); });
            // Same band as without the hunt: the first healer takes the tank, a second one shares the band as before.
            CHECK(two[HealingAssignment(two, live, 0)].guid == 20);
            CHECK(HealingUrgency(two[1], live) == HealingUrgency(two[0], live));
        }
        // Same raw band as a tank: only the in-band lead, so the lower one first (35 % before the 40 % tank).
        CHECK((order({tank40, hunted35}) == std::vector<std::uint64_t>{23, 22}));
        // A tank in a less urgent band is passed by the hunted member, as before.
        CHECK((order({tank60, hunted44}) == std::vector<std::uint64_t>{21, 24}));
        // No tank in the list: the full boost (one band up) over an unhunted member of its raw band.
        CHECK((order({dps26, hunted44}) == std::vector<std::uint64_t>{21, 25}));
        // Three-way with a tank: a total order, no cycle (tank 40, hunted 44, dps 26).
        CHECK((order({tank40, hunted44, dps26}) == std::vector<std::uint64_t>{25, 22, 21}));
    }
    std::cout << "healing triage policy: OK\n";
}
