#include "../ai/playerbot/RaidFrameworkPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::IsClassicRaidMap;
using ai::NeglectThreatNeedsRefresh;
using ai::RaidNameMatches;

int main()
{
    std::cout << "Starting TortoiseBots raid-framework policy tests...\n";

    // Name matching is case-insensitive substring; empty never matches.
    CHECK(RaidNameMatches("Loatheb", "loatheb"));
    CHECK(RaidNameMatches("Anub'Rekhan", "anub'rekhan"));
    CHECK(RaidNameMatches("Anub'Rekhan", "rekhan"));
    CHECK(!RaidNameMatches("Loatheb", ""));
    CHECK(!RaidNameMatches("", "loatheb"));
    CHECK(!RaidNameMatches("Gluth", "loatheb"));
    std::cout << "  [PASS] name matching is case-insensitive substring\n";

    // Read-once flag: only a fresh Set per evaluation keeps suppression up.
    CHECK(NeglectThreatNeedsRefresh(true));
    CHECK(!NeglectThreatNeedsRefresh(false));
    std::cout << "  [PASS] neglect-threat needs a fresh set per evaluation\n";

    // Classic raid maps auto-enable their tactics rows.
    CHECK(IsClassicRaidMap(509));
    CHECK(IsClassicRaidMap(309));
    CHECK(IsClassicRaidMap(531));
    CHECK(IsClassicRaidMap(533));
    CHECK(!IsClassicRaidMap(469));
    CHECK(!IsClassicRaidMap(0));
    std::cout << "  [PASS] classic raid map ids recognized\n";

    std::cout << "All raid-framework policy tests passed.\n";
    return 0;
}
