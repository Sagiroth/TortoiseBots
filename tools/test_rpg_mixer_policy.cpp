#include "../ai/playerbot/RpgMixerPolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::PickRpgMixerSlot;
using ai::RpgMixerAppliesToBot;
using ai::RpgMixerAvailability;
using ai::RpgMixerSlot;
using ai::RpgMixerSlotAllowsRequest;
using ai::RpgMixerSlotAvailable;
using ai::RpgMixerSlotWeight;
using ai::RpgMixerVerdictLive;

static const std::string kGrind = "4096";   // TravelDestinationPurpose::Grind
static const std::string kCamp = "64";      // TravelDestinationPurpose::GenericRpg
static const std::string kExplore = "262144"; // TravelDestinationPurpose::Explore

int main()
{
    std::cout << "Starting TortoiseBots RPG mixer policy tests...\n";

    // -------------------------------------------------------------
    // Test 1: donor weight table (quest 60 / grind 15 / camp 10 / explore 5)
    // -------------------------------------------------------------
    {
        CHECK(RpgMixerSlotWeight(RpgMixerSlot::Quest) == 60);
        CHECK(RpgMixerSlotWeight(RpgMixerSlot::Grind) == 15);
        CHECK(RpgMixerSlotWeight(RpgMixerSlot::Camp) == 10);
        CHECK(RpgMixerSlotWeight(RpgMixerSlot::Explore) == 5);
        std::cout << "  [PASS] weight table matches donor quest-heavy shape\n";
    }

    // -------------------------------------------------------------
    // Test 2: quest-heavy roll when everything is available
    // -------------------------------------------------------------
    {
        RpgMixerAvailability all{ true, true, true, true };
        // 60/90 of the table is quest: roll 0.0 and 0.5 both land quest.
        CHECK(PickRpgMixerSlot(all, 0.0) == (int)RpgMixerSlot::Quest);
        CHECK(PickRpgMixerSlot(all, 0.5) == (int)RpgMixerSlot::Quest);
        // 60-75 is grind, 75-85 camp, 85-90 explore (total 90).
        CHECK(PickRpgMixerSlot(all, 0.70) == (int)RpgMixerSlot::Grind);
        CHECK(PickRpgMixerSlot(all, 0.84) == (int)RpgMixerSlot::Camp);
        CHECK(PickRpgMixerSlot(all, 0.97) == (int)RpgMixerSlot::Explore);
        std::cout << "  [PASS] full table rolls quest-heavy\n";
    }

    // -------------------------------------------------------------
    // Test 3: only available slots roll (donor CheckRpgStatusAvailable)
    // -------------------------------------------------------------
    {
        RpgMixerAvailability grindOnly{ false, true, false, false };
        CHECK(PickRpgMixerSlot(grindOnly, 0.0) == (int)RpgMixerSlot::Grind);
        CHECK(PickRpgMixerSlot(grindOnly, 0.99) == (int)RpgMixerSlot::Grind);

        RpgMixerAvailability campExplore{ false, false, true, true };
        CHECK(PickRpgMixerSlot(campExplore, 0.0) == (int)RpgMixerSlot::Camp);
        CHECK(PickRpgMixerSlot(campExplore, 0.5) == (int)RpgMixerSlot::Camp);
        CHECK(PickRpgMixerSlot(campExplore, 0.9) == (int)RpgMixerSlot::Explore);

        RpgMixerAvailability none{ false, false, false, false };
        CHECK(PickRpgMixerSlot(none, 0.5) == -1);
        std::cout << "  [PASS] unavailable slots never win\n";
    }

    // -------------------------------------------------------------
    // Test 4: the winning slot admits only its own purpose
    // -------------------------------------------------------------
    {
        CHECK(RpgMixerSlotAllowsRequest((int)RpgMixerSlot::Quest, "quest", kGrind, kCamp, kExplore));
        CHECK(RpgMixerSlotAllowsRequest((int)RpgMixerSlot::Quest, "", kGrind, kCamp, kExplore));
        CHECK(!RpgMixerSlotAllowsRequest((int)RpgMixerSlot::Quest, kGrind, kGrind, kCamp, kExplore));
        CHECK(RpgMixerSlotAllowsRequest((int)RpgMixerSlot::Grind, kGrind, kGrind, kCamp, kExplore));
        CHECK(!RpgMixerSlotAllowsRequest((int)RpgMixerSlot::Grind, kCamp, kGrind, kCamp, kExplore));
        CHECK(RpgMixerSlotAllowsRequest((int)RpgMixerSlot::Camp, kCamp, kGrind, kCamp, kExplore));
        CHECK(!RpgMixerSlotAllowsRequest((int)RpgMixerSlot::Camp, kExplore, kGrind, kCamp, kExplore));
        CHECK(RpgMixerSlotAllowsRequest((int)RpgMixerSlot::Explore, kExplore, kGrind, kCamp, kExplore));
        CHECK(!RpgMixerSlotAllowsRequest((int)RpgMixerSlot::Explore, "quest", kGrind, kCamp, kExplore));
        // No verdict or a stale slot never gates: service errands pass.
        CHECK(RpgMixerSlotAllowsRequest(-1, kGrind, kGrind, kCamp, kExplore));
        CHECK(RpgMixerSlotAllowsRequest(-1, "1024", kGrind, kCamp, kExplore));
        std::cout << "  [PASS] verdict admits only the winning purpose\n";
    }

    // -------------------------------------------------------------
    // Test 5: pool-only scope and cached verdict lifetime
    // -------------------------------------------------------------
    {
        CHECK(RpgMixerAppliesToBot(true));
        CHECK(!RpgMixerAppliesToBot(false));
        time_t now = 1000000;
        CHECK(RpgMixerVerdictLive(now + 60, now));
        CHECK(!RpgMixerVerdictLive(now - 1, now));
        CHECK(!RpgMixerVerdictLive(0, now));
        std::cout << "  [PASS] pool-only scope, 10-min verdict window\n";
    }

    // -------------------------------------------------------------
    // Test 6: availability helper reads each slot
    // -------------------------------------------------------------
    {
        RpgMixerAvailability a{ true, false, false, false };
        CHECK(RpgMixerSlotAvailable(RpgMixerSlot::Quest, a));
        CHECK(!RpgMixerSlotAvailable(RpgMixerSlot::Grind, a));
        CHECK(!RpgMixerSlotAvailable(RpgMixerSlot::Explore, a));
        std::cout << "  [PASS] slot availability mapping\n";
    }

    std::cout << "All RPG mixer policy tests passed.\n";
    return 0;
}
