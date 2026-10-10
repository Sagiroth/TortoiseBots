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
using ai::RaidNameMatches;

int main()
{
    std::cout << "Starting TortoiseBots raid-framework policy tests...\n";

    // Name matching is case-insensitive FULL equality like the donor:
    // partial qualifiers never match (no twin ambiguity).
    CHECK(RaidNameMatches("Loatheb", "loatheb"));
    CHECK(RaidNameMatches("Anub'Rekhan", "anub'rekhan"));
    CHECK(RaidNameMatches("Ossirian the Unscarred", "ossirian the unscarred"));
    CHECK(!RaidNameMatches("Anub'Rekhan", "rekhan"));
    CHECK(!RaidNameMatches("Loatheb", "loathe"));
    CHECK(!RaidNameMatches("Loatheb", ""));
    CHECK(!RaidNameMatches("", "loatheb"));
    CHECK(!RaidNameMatches("Gluth", "loatheb"));
    std::cout << "  [PASS] name matching is case-insensitive full equality\n";

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
