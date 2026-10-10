#include "../ai/playerbot/strategy/shaman/ShamanSituationalTotemPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::DiseaseCleansingShouldDrop;
using ai::EarthbindShouldDrop;
using ai::GroundingShouldDrop;
using ai::PoisonCleansingShouldDrop;
using ai::SituationalTotemManualOverride;
using ai::TremorShouldDrop;

int main()
{
    std::cout << "Starting shaman situational totem policy tests...\n";

    // Tremor: fires on party fear/charm, never into an occupied earth slot
    // or against a manual order. No confuse arm, no any-cast pre-drop:
    // tremor answers fear/charm only.
    CHECK(TremorShouldDrop(true, true, false) == true);
    CHECK(TremorShouldDrop(false, true, false) == false);
    CHECK(TremorShouldDrop(true, false, false) == false);
    CHECK(TremorShouldDrop(true, true, true) == false);
    std::cout << "  [PASS] tremor reacts to fear/charm, respects slot and manual orders\n";

    // Grounding: only a cast aimed at the party steals the air slot (AoE,
    // self-buffs and casts aimed elsewhere cannot be redirected).
    CHECK(GroundingShouldDrop(true, true, false) == true);
    CHECK(GroundingShouldDrop(false, true, false) == false);
    CHECK(GroundingShouldDrop(true, false, false) == false);
    CHECK(GroundingShouldDrop(true, true, true) == false);
    std::cout << "  [PASS] grounding reacts to party-aimed casts, respects slot and manual orders\n";

    // Poison twin: poison only; disease twin: disease only.
    CHECK(PoisonCleansingShouldDrop(true, true, false) == true);
    CHECK(PoisonCleansingShouldDrop(false, true, false) == false);
    CHECK(PoisonCleansingShouldDrop(true, false, false) == false);
    CHECK(PoisonCleansingShouldDrop(true, true, true) == false);
    CHECK(DiseaseCleansingShouldDrop(true, true, false) == true);
    CHECK(DiseaseCleansingShouldDrop(false, true, false) == false);
    CHECK(DiseaseCleansingShouldDrop(true, false, false) == false);
    CHECK(DiseaseCleansingShouldDrop(true, true, true) == false);
    std::cout << "  [PASS] cleansing twins react per debuff type, respect slot and manual orders\n";

    // Earthbind: fleeing target or snared member; slot and manual order gate.
    CHECK(EarthbindShouldDrop(true, true, false) == true);
    CHECK(EarthbindShouldDrop(false, true, false) == false);
    CHECK(EarthbindShouldDrop(true, false, false) == false);
    CHECK(EarthbindShouldDrop(true, true, true) == false);
    std::cout << "  [PASS] earthbind reacts to runners, respects slot and manual orders\n";

    // Manual override helper.
    CHECK(SituationalTotemManualOverride(true) == true);
    CHECK(SituationalTotemManualOverride(false) == false);
    std::cout << "  [PASS] manual orders veto automation\n";

    std::cout << "All shaman situational totem policy checks PASSED!\n";
    return 0;
}
