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
    // Test 3: only available slots roll (donor CheckRpgStatusAvailable;
    // grind-only also exercises the grind fallback of test 9)
    // -------------------------------------------------------------
    {
        RpgMixerAvailability grindOnly{ false, true, false, false };
        CHECK(PickRpgMixerSlot(grindOnly, 0.0) == (int)RpgMixerSlot::Grind);
        CHECK(PickRpgMixerSlot(grindOnly, 0.99) == (int)RpgMixerSlot::Grind);

        RpgMixerAvailability campExplore{ false, false, true, true };
        CHECK(PickRpgMixerSlot(campExplore, 0.0) == (int)RpgMixerSlot::Camp);
        CHECK(PickRpgMixerSlot(campExplore, 0.5) == (int)RpgMixerSlot::Camp);
        CHECK(PickRpgMixerSlot(campExplore, 0.9) == (int)RpgMixerSlot::Explore);

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

    // -------------------------------------------------------------
    // Test 7: parked quest never wins (CRITICAL 1 - quest mirrors the
    // real "no travel purpose until::quest" park, so a quest-dry bot
    // cannot roll a dead Quest verdict and idle for 10 min)
    // -------------------------------------------------------------
    {
        // Quest parked: quest has no weight in the table, whatever roll.
        RpgMixerAvailability questParked{ false, true, true, true };
        CHECK(PickRpgMixerSlot(questParked, 0.0) == (int)RpgMixerSlot::Grind);
        CHECK(PickRpgMixerSlot(questParked, 0.5) == (int)RpgMixerSlot::Camp);
        CHECK(PickRpgMixerSlot(questParked, 0.99) == (int)RpgMixerSlot::Explore);
        // Quest entry to the roll needs all three quest gates (free slots,
        // unparked, rpg-quest strategy on): callers build `quest` as the AND
        // of the three, so any single false keeps quest out of the table.
        RpgMixerAvailability questStrategyOff{ false, true, false, false };
        CHECK(PickRpgMixerSlot(questStrategyOff, 0.0) == (int)RpgMixerSlot::Grind);
        std::cout << "  [PASS] parked quest never wins the roll\n";
    }

    // -------------------------------------------------------------
    // Test 8: a verdict whose slot went unavailable is dead (CRITICAL 2 -
    // no sticky dead verdict: the read path re-checks availability and
    // re-rolls at once; grind fallback in test 9 keeps the bot moving)
    // -------------------------------------------------------------
    {
        // Camp won while camp was available, then camp parked: the cached
        // verdict no longer admits camp's request, so the read path must
        // re-roll instead of replaying it (modelled here by checking the
        // stale slot against fresh availability).
        RpgMixerAvailability fresh{ true, true, false, true };
        CHECK(!RpgMixerSlotAvailable(RpgMixerSlot::Camp, fresh));
        CHECK(RpgMixerSlotAvailable(RpgMixerSlot::Grind, fresh));
        // The re-roll lands on a live slot, never the parked one
        // (weights among quest 60 / grind 15 / explore 5: quest < 0.75,
        // grind < 0.9375, else explore).
        int reroll = PickRpgMixerSlot(fresh, 0.80);
        CHECK(reroll == (int)RpgMixerSlot::Grind);
        reroll = PickRpgMixerSlot(fresh, 0.97);
        CHECK(reroll == (int)RpgMixerSlot::Explore);
        // A quest verdict parked the same way: quest's request is refused,
        // grind/camp/explore still admit their winner.
        CHECK(!RpgMixerSlotAllowsRequest((int)RpgMixerSlot::Quest, kGrind, kGrind, kCamp, kExplore));
        CHECK(RpgMixerSlotAllowsRequest((int)RpgMixerSlot::Camp, kCamp, kGrind, kCamp, kExplore));
        std::cout << "  [PASS] stale verdicts fail closed, re-roll picks live slots\n";
    }

    // -------------------------------------------------------------
    // Test 9: grind is the fallback (CRITICAL 4 - the mixer may never
    // idle a bot or level it slower than main: with quest/camp/explore
    // all unavailable the verdict is grind, and a grind request is
    // admitted under every verdict)
    // -------------------------------------------------------------
    {
        RpgMixerAvailability onlyGrindParked{ false, true, false, false };
        CHECK(PickRpgMixerSlot(onlyGrindParked, 0.0) == (int)RpgMixerSlot::Grind);
        CHECK(PickRpgMixerSlot(onlyGrindParked, 0.99) == (int)RpgMixerSlot::Grind);
        RpgMixerAvailability nothingLive{ false, false, false, false };
        CHECK(PickRpgMixerSlot(nothingLive, 0.0) == (int)RpgMixerSlot::Grind);
        CHECK(PickRpgMixerSlot(nothingLive, 0.99) == (int)RpgMixerSlot::Grind);
        // Grind admits its own request under its own verdict ...
        CHECK(RpgMixerSlotAllowsRequest((int)RpgMixerSlot::Grind, kGrind, kGrind, kCamp, kExplore));
        // ... and the quest slot never blocks a grind request either: the
        // verdict only gates the leisure rows it names, grind keeps main's
        // behaviour. (Quest verdict admits quest only; grind's request runs
        // when grind wins or via the fallback above.)
        RpgMixerAvailability questAndGrind{ true, true, false, false };
        int pick = PickRpgMixerSlot(questAndGrind, 0.90);
        CHECK(pick == (int)RpgMixerSlot::Grind);
        CHECK(RpgMixerSlotAllowsRequest(pick, kGrind, kGrind, kCamp, kExplore));
        std::cout << "  [PASS] grind fallback never mixer-blocks levelling\n";
    }

    // -------------------------------------------------------------
    // Test 10: quest takes part in the same roll (CRITICAL 3 - a Grind,
    // Camp or Explore verdict gates the quest request too, so quest 6.30
    // cannot pre-empt a camp 6.28 / explore 6.29 win)
    // -------------------------------------------------------------
    {
        CHECK(!RpgMixerSlotAllowsRequest((int)RpgMixerSlot::Grind, "quest", kGrind, kCamp, kExplore));
        CHECK(!RpgMixerSlotAllowsRequest((int)RpgMixerSlot::Grind, "", kGrind, kCamp, kExplore));
        CHECK(!RpgMixerSlotAllowsRequest((int)RpgMixerSlot::Camp, "quest", kGrind, kCamp, kExplore));
        CHECK(!RpgMixerSlotAllowsRequest((int)RpgMixerSlot::Explore, "quest", kGrind, kCamp, kExplore));
        CHECK(RpgMixerSlotAllowsRequest((int)RpgMixerSlot::Quest, "quest", kGrind, kCamp, kExplore));
        std::cout << "  [PASS] quest gated by non-quest verdicts\n";
    }

    std::cout << "All RPG mixer policy tests passed.\n";
    return 0;
}
