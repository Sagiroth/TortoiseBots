#include "../ai/playerbot/policy/raid/BwlBundle1Policy.h"

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
using ai::kCorruptedHealingSpellId;
using ai::kCorruptedInfernalEntry;
using ai::kCorruptedStoneskinTotemEntry;
using ai::kCorruptedHealingStreamTotemEntry;
using ai::kCorruptedWindfuryTotemEntry;
using ai::kNefarianClassCallAddRange;
using ai::NefarianClassCallAddRank;
using ai::ShouldNefarianKillClassCallAdd;
using ai::ShouldSwitchToClassCallAdd;
using ai::ShouldBlockCorruptedHeal;

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

    // Nefarian class calls: ids pinned to the core script and 1.18.1 data.
    CHECK(kCorruptedHealingSpellId == 23401);
    CHECK(kCorruptedInfernalEntry == 14668);
    CHECK(kCorruptedStoneskinTotemEntry == 14663);
    CHECK(kCorruptedHealingStreamTotemEntry == 14664);
    CHECK(kCorruptedWindfuryTotemEntry == 14666);
    CHECK(kNefarianClassCallAddRange == 30.0f);
    std::cout << "  [PASS] nefarian class-call ids pinned\n";

    // Kill order: totems over infernals; Fire Nova 14662 (despawns at once),
    // the dummy Corrupted Totem 14667, Nefarian and Bone Constructs are not
    // class-call adds.
    CHECK(NefarianClassCallAddRank(14663) == 2);
    CHECK(NefarianClassCallAddRank(14664) == 2);
    CHECK(NefarianClassCallAddRank(14666) == 2);
    CHECK(NefarianClassCallAddRank(14668) == 1);
    CHECK(NefarianClassCallAddRank(14662) == 0);
    CHECK(NefarianClassCallAddRank(14667) == 0);
    CHECK(NefarianClassCallAddRank(11583) == 0);
    CHECK(NefarianClassCallAddRank(14605) == 0);
    std::cout << "  [PASS] nefarian class-call add ranks\n";

    // Only damage dealers without an explicit order switch.
    CHECK(ShouldNefarianKillClassCallAdd(false, false, false));
    CHECK(!ShouldNefarianKillClassCallAdd(true, false, false));
    CHECK(!ShouldNefarianKillClassCallAdd(false, true, false));
    CHECK(!ShouldNefarianKillClassCallAdd(false, false, true));
    std::cout << "  [PASS] nefarian class-call add roles\n";

    // Switch up the ranks only: boss -> totem, infernal -> totem,
    // boss -> infernal; never totem -> totem or totem -> infernal.
    CHECK(ShouldSwitchToClassCallAdd(2, 0));
    CHECK(ShouldSwitchToClassCallAdd(2, 1));
    CHECK(ShouldSwitchToClassCallAdd(1, 0));
    CHECK(!ShouldSwitchToClassCallAdd(2, 2));
    CHECK(!ShouldSwitchToClassCallAdd(1, 2));
    CHECK(!ShouldSwitchToClassCallAdd(1, 1));
    CHECK(!ShouldSwitchToClassCallAdd(0, 0));
    std::cout << "  [PASS] nefarian class-call add switch rule\n";

    // Corrupted Healing: direct heals blocked only while the aura is up;
    // Renew / Power Word: Shield (no direct heal effect) stay.
    CHECK(ShouldBlockCorruptedHeal(true, true));
    CHECK(!ShouldBlockCorruptedHeal(true, false));
    CHECK(!ShouldBlockCorruptedHeal(false, true));
    std::cout << "  [PASS] corrupted healing heal block rule\n";

    std::cout << "All bwl-bundle1 policy tests passed.\n";
    return 0;
}
