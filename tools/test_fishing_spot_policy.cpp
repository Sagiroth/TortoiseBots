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
using ai::FishingSearchRadius;
using ai::FishingSpotApplies;
using ai::FishingStandDry;
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

    std::cout << "All fishing-spot policy tests passed.\n";
    return 0;
}
