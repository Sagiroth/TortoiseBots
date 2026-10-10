#include "../ai/playerbot/ChromaggusSandPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::IsHourglassSandAction;
using ai::kBronzeAfflictionSpellId;
using ai::kChromaggusEntry;
using ai::kHourglassSandItemId;
using ai::kHourglassSandSpellId;
using ai::ShouldUseHourglassSand;

int main()
{
    std::cout << "Starting TortoiseBots chromaggus-sand policy tests...\n";

    // Pinned ids: Chromaggus 14020, Bronze 23170, Sand spell 23645,
    // Sand item 19183 (1.18.1 verified; classic-21171 guess was wrong).
    CHECK(kChromaggusEntry == 14020);
    CHECK(kBronzeAfflictionSpellId == 23170);
    CHECK(kHourglassSandSpellId == 23645);
    CHECK(kHourglassSandItemId == 19183);
    std::cout << "  [PASS] ids pinned\n";

    // Trigger: bronze on self means use the sand, nothing else matters.
    CHECK(ShouldUseHourglassSand(true));
    CHECK(!ShouldUseHourglassSand(false));
    std::cout << "  [PASS] bronze affliction triggers the cleanse\n";

    // Action-name guard matches only the sand action.
    CHECK(IsHourglassSandAction("use hourglass sand"));
    CHECK(!IsHourglassSandAction("raid bomb runout"));
    std::cout << "  [PASS] action name guard\n";

    std::cout << "All chromaggus-sand policy tests passed.\n";
    return 0;
}
