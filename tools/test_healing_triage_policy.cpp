#include "../ai/playerbot/HealingTriagePolicy.h"

#include <cstdlib>
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
    std::cout << "healing triage policy: OK\n";
}
