#include "../ai/playerbot/FishingSpotPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::FishingCastInRange;
using ai::FishingCellKey;
using ai::FishingSearchAllowed;
using ai::FishingSearchRadius;
using ai::FishingSessionActive;
using ai::FishingSessionAllowsSearch;
using ai::FishingSessionExpired;
using ai::FishingSessionStartable;
using ai::FishingSpotApplies;
using ai::FishingStandDry;
using ai::FishingWaterCacheFresh;
using ai::FishingWaterDeepEnough;
using ai::FishingZoneSkillOk;

int main()
{
    std::cout << "Starting TortoiseBots fishing-spot policy tests...\n";

    // -------------------------------------------------------------
    // (1) Scope: masterless random (pool) bots only. Owned/hired bots
    // keep player control and never take the direct-water path.
    // -------------------------------------------------------------
    {
        CHECK(FishingSpotApplies(true, false));
        CHECK(!FishingSpotApplies(true, true));
        CHECK(!FishingSpotApplies(false, false));
        CHECK(!FishingSpotApplies(false, true));
        std::cout << "  [PASS] direct water search binds pool bots only\n";
    }

    // -------------------------------------------------------------
    // (2) Search window: the donor fishingDistance (40 yd masterless)
    // and fishingDistanceFromMaster (10 yd mastered) values.
    // -------------------------------------------------------------
    {
        CHECK(FishingSearchRadius(false) == 40.0f);
        CHECK(FishingSearchRadius(true) == 10.0f);
        std::cout << "  [PASS] search radius matches donor windows\n";
    }

    // -------------------------------------------------------------
    // (3) Cast window: closer than 10 yd the bobber lands on the
    // bank, beyond 20 yd the cast cannot reach (donor MIN/MAX).
    // -------------------------------------------------------------
    {
        CHECK(!FishingCastInRange(9.9f));
        CHECK(FishingCastInRange(10.0f));
        CHECK(FishingCastInRange(15.0f));
        CHECK(FishingCastInRange(20.0f));
        CHECK(!FishingCastInRange(20.1f));
        std::cout << "  [PASS] cast accepts the 10-20 yd window only\n";
    }

    // -------------------------------------------------------------
    // (4) Depth: a puddle (surface at/below the lakebed, or under
    // half a yard deep) is not fishable (donor shallow check).
    // -------------------------------------------------------------
    {
        CHECK(!FishingWaterDeepEnough(0.0f, 0.0f));
        CHECK(!FishingWaterDeepEnough(-1.0f, 0.0f));
        CHECK(!FishingWaterDeepEnough(0.4f, 0.0f));
        CHECK(FishingWaterDeepEnough(0.5f, 0.0f));
        CHECK(FishingWaterDeepEnough(2.0f, -3.0f));
        std::cout << "  [PASS] shallow water is rejected\n";
    }

    // -------------------------------------------------------------
    // (5) Skill gate mirrors the travel fish errand
    // (GatherTravelDestination::IsPossible): no base skill means no
    // fishable zone, otherwise the bot gets the same -5 head start.
    // -------------------------------------------------------------
    {
        CHECK(!FishingZoneSkillOk(0, 100));
        CHECK(!FishingZoneSkillOk(5, 4));
        CHECK(FishingZoneSkillOk(5, 5));
        CHECK(!FishingZoneSkillOk(10, 4));
        CHECK(FishingZoneSkillOk(10, 5));
        std::cout << "  [PASS] zone skill gate matches the travel errand\n";
    }

    // -------------------------------------------------------------
    // (6) The bot stands on dry land, never in the water.
    // -------------------------------------------------------------
    {
        CHECK(FishingStandDry(false, true));
        CHECK(!FishingStandDry(true, true));
        CHECK(!FishingStandDry(false, false));
        std::cout << "  [PASS] stand must be dry valid ground\n";
    }

    // -------------------------------------------------------------
    // (7) Side-activity budget (review): one session per hour, at most
    // 5 min / 5 casts. A fresh bot may start; a running session stays
    // startable; a spent session (time or casts) blocks until the hour
    // passes. Wrap-safe: uint32 ms arithmetic survives the ~49-day wrap.
    // -------------------------------------------------------------
    {
        std::uint32_t const hour = 3600000;
        std::uint32_t const fiveMin = 300000;
        CHECK(FishingSessionStartable(0, 0, 0, 1000));
        CHECK(FishingSessionAllowsSearch(0, 0, 0, 1000));
        // Running session: 2 casts in, 1 min elapsed.
        CHECK(!FishingSessionStartable(1000, 0, 2, 61000));
        CHECK(FishingSessionAllowsSearch(1000, 0, 2, 61000));
        CHECK(FishingSessionActive(1000, 2, 61000));
        // Time spent: 5 min elapsed.
        CHECK(FishingSessionExpired(1000, 2, 1000 + fiveMin));
        CHECK(!FishingSessionActive(1000, 2, 1000 + fiveMin));
        // Casts spent: 5 casts even in the first minute.
        CHECK(FishingSessionExpired(1000, 5, 61000));
        // Ended an hour ago: startable again.
        CHECK(FishingSessionStartable(0, 1000, 0, 1000 + hour));
        CHECK(!FishingSessionStartable(0, 1000, 0, 1000 + hour - 1));
        // Wrap: start 10 s before the uint32 wrap, now 10 s after.
        std::uint32_t const nearWrap = 0xFFFFFFFFu - 10000;
        CHECK(!FishingSessionExpired(nearWrap, 1, 10000));
        CHECK(FishingSessionExpired(nearWrap, 1, nearWrap + fiveMin));
        std::cout << "  [PASS] hourly session budget (5 casts / 5 min)\n";
    }

    // -------------------------------------------------------------
    // (8) Search throttle (review): at most one search per bot per
    // 15 s; shared per-cell water verdicts stay fresh 30 min.
    // -------------------------------------------------------------
    {
        CHECK(FishingSearchAllowed(0, 1000));
        CHECK(!FishingSearchAllowed(1000, 1000 + 14999));
        CHECK(FishingSearchAllowed(1000, 1000 + 15000));
        CHECK(FishingWaterCacheFresh(1000, 1000 + 1799999));
        CHECK(!FishingWaterCacheFresh(1000, 1000 + 1800000));
        CHECK(!FishingWaterCacheFresh(0, 1000));
        std::cout << "  [PASS] per-bot throttle and cell cache windows\n";
    }

    // -------------------------------------------------------------
    // (9) Cell keys (review): distinct map cells map to distinct keys.
    // -------------------------------------------------------------
    {
        CHECK(FishingCellKey(0, 1, 2) != FishingCellKey(0, 1, 3));
        CHECK(FishingCellKey(0, 1, 2) != FishingCellKey(1, 1, 2));
        CHECK(FishingCellKey(0, 1, 2) == FishingCellKey(0, 1, 2));
        std::cout << "  [PASS] cell keys separate map cells\n";
    }

    std::cout << "All fishing-spot policy tests passed.\n";
    return 0;
}
