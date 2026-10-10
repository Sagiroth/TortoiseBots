#include "../ai/playerbot/McGarrShazzrahPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::IsGarrSuppressedAoeAction;
using ai::IsShazzrahMoveAction;
using ai::kGarrEntry;
using ai::kShazzrahEntry;
using ai::kShazzrahRangeDistance;
using ai::ShouldLeaveShazzrahRange;
using ai::ShouldSuppressGarrAoe;

int main()
{
    std::cout << "Starting TortoiseBots mc-garr-shazzrah policy tests...\n";

    // Pinned ids: Garr 12057, Shazzrah 12264, 26y step-out.
    CHECK(kGarrEntry == 12057);
    CHECK(kShazzrahEntry == 12264);
    CHECK(kShazzrahRangeDistance == 26.0f);
    std::cout << "  [PASS] ids and 26y range pinned\n";

    // Garr AoE set: the donor's explicit list by action name. Real AoE
    // (whirlwind, consecration, magma totem, traps, hurricane, mage AoE incl.
    // blizzard/cone of cold/blast wave, chain lightning, hunter shots,
    // warlock rain) is suppressed; single-target dots, curses
    // and heals — which our threat flags wrongly mark AOE — are NOT.
    CHECK(IsGarrSuppressedAoeAction("dps aoe"));
    CHECK(IsGarrSuppressedAoeAction("whirlwind"));
    CHECK(IsGarrSuppressedAoeAction("consecration"));
    CHECK(IsGarrSuppressedAoeAction("magma totem"));
    CHECK(IsGarrSuppressedAoeAction("explosive trap"));
    CHECK(IsGarrSuppressedAoeAction("hurricane"));
    CHECK(IsGarrSuppressedAoeAction("flamestrike"));
    CHECK(IsGarrSuppressedAoeAction("blizzard"));
    CHECK(IsGarrSuppressedAoeAction("cone of cold"));
    CHECK(IsGarrSuppressedAoeAction("blast wave"));
    CHECK(IsGarrSuppressedAoeAction("arcane explosion"));
    CHECK(IsGarrSuppressedAoeAction("chain lightning"));
    CHECK(IsGarrSuppressedAoeAction("multi-shot"));
    CHECK(IsGarrSuppressedAoeAction("volley"));
    CHECK(IsGarrSuppressedAoeAction("rain of fire"));
    CHECK(!IsGarrSuppressedAoeAction("corruption on attacker"));
    CHECK(!IsGarrSuppressedAoeAction("serpent sting"));
    CHECK(!IsGarrSuppressedAoeAction("heal party member"));
    CHECK(!IsGarrSuppressedAoeAction("flash heal"));
    CHECK(!IsGarrSuppressedAoeAction("dps assist"));
    CHECK(!IsGarrSuppressedAoeAction("tank assist"));
    std::cout << "  [PASS] garr aoe name set (real aoe only)\n";

    // Garr AoE-off: only DPS-bot AoE while Garr lives is suppressed.
    CHECK(ShouldSuppressGarrAoe(true, true, true));
    CHECK(!ShouldSuppressGarrAoe(false, true, true));
    CHECK(!ShouldSuppressGarrAoe(true, false, true));
    CHECK(!ShouldSuppressGarrAoe(true, true, false));
    CHECK(!ShouldSuppressGarrAoe(false, false, false));
    std::cout << "  [PASS] garr suppresses dps-bot aoe only\n";

    // Shazzrah: ranged inside 26y leaves; melee inside holds, ranged
    // outside holds.
    CHECK(ShouldLeaveShazzrahRange(true, true));
    CHECK(!ShouldLeaveShazzrahRange(false, true));
    CHECK(!ShouldLeaveShazzrahRange(true, false));
    CHECK(!ShouldLeaveShazzrahRange(false, false));
    std::cout << "  [PASS] shazzrah moves ranged inside 26y only\n";

    CHECK(IsShazzrahMoveAction("move away from shazzrah"));
    CHECK(!IsShazzrahMoveAction("move away from magmadar"));
    std::cout << "  [PASS] action name guard\n";

    std::cout << "All mc-garr-shazzrah policy tests passed.\n";
    return 0;
}
