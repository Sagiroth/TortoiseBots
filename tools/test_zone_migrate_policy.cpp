#include "../ai/playerbot/ZoneMigratePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::OUTGROWN_GATHER_NEXT_TIER_SKILL;
using ai::OutgrownZoneRefusesPoint;
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
    // Ironforge and kept picking Dun Morogh, correctly. The capital itself
    // stays searchable on purpose; Grind points inside it are refused by the
    // SetBestTarget "capital" gate in ChooseTravelTargetAction instead, so a
    // capital-idle leave (e.g. Orgrimmar) can no longer land back inside the
    // same capital.
    CHECK(ZoneMigrationExcludeZone(1, true) == 0);
    CHECK(ZoneMigrationExcludeZone(14, true) == 0);
    std::cout << "  [PASS] capital-idle leave excludes nothing\n";

    // Unknown bot zone (id 0: unresolvable area) excludes nothing - fail
    // open, like today's behaviour.
    CHECK(ZoneMigrationExcludeZone(0, false) == 0);
    std::cout << "  [PASS] unknown zone fails open\n";

    // Ordinary Grind/Gather floor (task E): same +5 shape as the leave
    // rule, pool bots 11+ only. Dun Morogh (zone level 7) refuses a level
    // 13 pool bot and keeps a level 11 one (11 < 7 + 5 + 1, i.e. fits);
    // levels 1-10 never refuse, so starter behaviour is unchanged, and
    // owned/hired bots keep player control.
    CHECK(OutgrownZoneRefusesPoint(7, 13, true));
    CHECK(OutgrownZoneRefusesPoint(7, 16, true));
    CHECK(!OutgrownZoneRefusesPoint(7, 12, true));
    CHECK(!OutgrownZoneRefusesPoint(7, 11, true));
    CHECK(!OutgrownZoneRefusesPoint(7, 10, true));
    CHECK(!OutgrownZoneRefusesPoint(7, 5, true));
    CHECK(!OutgrownZoneRefusesPoint(7, 13, false));
    CHECK(!OutgrownZoneRefusesPoint(13, 13, true));
    CHECK(!OutgrownZoneRefusesPoint(14, 13, true));
    CHECK(OutgrownZoneRefusesPoint(6, 12, true));
    CHECK(!OutgrownZoneRefusesPoint(6, 11, true));
    std::cout << "  [PASS] outgrown zone floor refuses pool 11+ only\n";

    // Unknown/unvalidated zone levels fail open: a point whose zone level
    // never resolved must stay walkable, like today.
    CHECK(!OutgrownZoneRefusesPoint(0, 16, true));
    CHECK(!OutgrownZoneRefusesPoint(-1, 16, true));
    CHECK(!OutgrownZoneRefusesPoint(-2, 16, true));
    std::cout << "  [PASS] unknown zone level fails open\n";

    // Gather next-tier skill guard: starter Copper/Earthroot (req 1) stay
    // while the bot cannot gather next-zone Tin/Briarthorn (req 65).
    CHECK(OUTGROWN_GATHER_NEXT_TIER_SKILL == 65);
    std::cout << "  [PASS] gather next-tier skill threshold\n";

    std::cout << "TortoiseBots zone migration policy tests passed.\n";
    return 0;
}
