#include "../ai/playerbot/ZoneMigratePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::ZoneMigrationExcludeZone;

int main()
{
    std::cout << "Starting TortoiseBots zone migration policy tests...\n";

    // Outgrown-zone leave: the search must not re-pick home. Night2 case:
    // Galwurth fired LeaveOutgrownZone 3x at level 10.59 in Durotar (zone
    // 14) and re-picked Durotar grind every time, never the Barrens.
    CHECK(ZoneMigrationExcludeZone(14, false) == 14);
    CHECK(ZoneMigrationExcludeZone(12, false) == 12);
    CHECK(ZoneMigrationExcludeZone(85, false) == 85);
    std::cout << "  [PASS] outgrown zone excluded from its own leave search\n";

    // Capital-idle leave: no zone is excluded. The bot leaves for services
    // (trainer/AH/bank), not away from a zone, and the service gate keeps
    // capital vendors for outleveled bots - excluding the capital's zone
    // would fight that. Night2 case: Zarortharur fired 4x idling in
    // Ironforge and kept picking Dun Morogh, correctly.
    CHECK(ZoneMigrationExcludeZone(1, true) == 0);
    CHECK(ZoneMigrationExcludeZone(14, true) == 0);
    std::cout << "  [PASS] capital-idle leave excludes nothing\n";

    // Unknown bot zone (id 0: unresolvable area) excludes nothing - fail
    // open, like today's behaviour.
    CHECK(ZoneMigrationExcludeZone(0, false) == 0);
    std::cout << "  [PASS] unknown zone fails open\n";

    std::cout << "TortoiseBots zone migration policy tests passed.\n";
    return 0;
}
