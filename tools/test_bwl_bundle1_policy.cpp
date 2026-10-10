#include "../ai/playerbot/BwlBundle1Policy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::kBroodlordEntry;
using ai::kBroodlordRangeDistance;
using ai::kNefarianEntry;
using ai::kVaelEntry;
using ai::kWildMagicSpellId;
using ai::ShouldLeaveBroodlordRange;
using ai::ShouldNefarianIceBlock;
using ai::ShouldOffTankFlank;

int main()
{
    std::cout << "Starting TortoiseBots bwl-bundle1 policy tests...\n";

    // Pinned ids: Broodlord 12017, Vael 13020, Nefarian 11583,
    // Wild Magic 23410, 18y step-out.
    CHECK(kBroodlordEntry == 12017);
    CHECK(kVaelEntry == 13020);
    CHECK(kNefarianEntry == 11583);
    CHECK(kWildMagicSpellId == 23410);
    CHECK(kBroodlordRangeDistance == 18.0f);
    std::cout << "  [PASS] ids and 18y range pinned\n";

    // Broodlord: ranged non-victim inside 18y leaves; victims hold (no
    // kiting through the room), melee holds, ranged outside holds.
    CHECK(ShouldLeaveBroodlordRange(true, false, true));
    CHECK(!ShouldLeaveBroodlordRange(true, true, true));
    CHECK(!ShouldLeaveBroodlordRange(false, false, true));
    CHECK(!ShouldLeaveBroodlordRange(true, false, false));
    std::cout << "  [PASS] broodlord moves ranged non-victims only\n";

    // Drake off-tank flank: tanks that are not the victim flank.
    CHECK(ShouldOffTankFlank(true, false));
    CHECK(!ShouldOffTankFlank(true, true));
    CHECK(!ShouldOffTankFlank(false, false));
    std::cout << "  [PASS] off-tank flank rule\n";

    // Nefarian: mage + Wild Magic means Ice Block.
    CHECK(ShouldNefarianIceBlock(true, true));
    CHECK(!ShouldNefarianIceBlock(false, true));
    CHECK(!ShouldNefarianIceBlock(true, false));
    std::cout << "  [PASS] nefarian wild magic ice block rule\n";

    std::cout << "All bwl-bundle1 policy tests passed.\n";
    return 0;
}
