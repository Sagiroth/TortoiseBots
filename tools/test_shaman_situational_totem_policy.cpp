#include "../ai/playerbot/strategy/shaman/ShamanSituationalTotemPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::CleansingShouldDrop;
using ai::EarthbindShouldDrop;
using ai::GroundingShouldDrop;
using ai::SituationalTotemManualOverride;
using ai::TremorOutranksEarthbind;
using ai::TremorShouldDrop;

int main()
{
    std::cout << "Starting shaman situational totem policy tests...\n";

    // Tremor: fires on party fear or a fear-casting target, solo or grouped,
    // but never into an occupied earth slot or against a manual order.
    CHECK(TremorShouldDrop(true, false, true, false) == true);
    CHECK(TremorShouldDrop(false, true, true, false) == true);
    CHECK(TremorShouldDrop(false, false, true, false) == false);
    CHECK(TremorShouldDrop(true, true, true, false) == true);
    CHECK(TremorShouldDrop(true, false, false, false) == false);
    CHECK(TremorShouldDrop(true, false, true, true) == false);
    CHECK(TremorShouldDrop(false, true, false, true) == false);
    std::cout << "  [PASS] tremor reacts to fear, respects slot and manual orders\n";

    // Grounding: only a live cast steals the air slot.
    CHECK(GroundingShouldDrop(true, true, false) == true);
    CHECK(GroundingShouldDrop(false, true, false) == false);
    CHECK(GroundingShouldDrop(true, false, false) == false);
    CHECK(GroundingShouldDrop(true, true, true) == false);
    std::cout << "  [PASS] grounding reacts to casting, respects slot and manual orders\n";

    // Cleansing: either debuff type triggers; slot and manual order gate.
    CHECK(CleansingShouldDrop(true, false, true, false) == true);
    CHECK(CleansingShouldDrop(false, true, true, false) == true);
    CHECK(CleansingShouldDrop(true, true, true, false) == true);
    CHECK(CleansingShouldDrop(false, false, true, false) == false);
    CHECK(CleansingShouldDrop(true, false, false, false) == false);
    CHECK(CleansingShouldDrop(false, true, true, true) == false);
    std::cout << "  [PASS] cleansing reacts to poison/disease, respects slot and manual orders\n";

    // Earthbind: fleeing target only; slot and manual order gate.
    CHECK(EarthbindShouldDrop(true, true, false) == true);
    CHECK(EarthbindShouldDrop(false, true, false) == false);
    CHECK(EarthbindShouldDrop(true, false, false) == false);
    CHECK(EarthbindShouldDrop(true, true, true) == false);
    std::cout << "  [PASS] earthbind reacts to runners, respects slot and manual orders\n";

    // Manual override helper + earth arbitration: tremor beats earthbind.
    CHECK(SituationalTotemManualOverride(true) == true);
    CHECK(SituationalTotemManualOverride(false) == false);
    CHECK(TremorOutranksEarthbind() == true);
    std::cout << "  [PASS] manual orders veto automation, tremor outranks earthbind\n";

    std::cout << "All shaman situational totem policy checks PASSED!\n";
    return 0;
}
