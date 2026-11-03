// Standalone policy check for the BotGearService rework (raid-w1-r2 item 2):
// pre-existing bag items survive, created items may be destroyed, pins hold.
//
// Build: g++ -std=c++17 -Wall -Wextra -Werror=return-type tools/test_bot_gear_created_policy.cpp -o /tmp/t
#include "../runtime/policy/hire/BotGearPolicy.h"

#include <cstdio>
#include <cstdlib>
#include <set>

using namespace TortoiseBots::BotGear;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

// The service change: an item is destroyable only when this call created it
// and it ended up unused. Everything else (pre-existing bags, displaced worn
// pieces parked in the bags) is kept.
static bool MayDestroyCreated(bool createdByCall, bool unused)
{
    return CreatedItemMayBeDestroyed(createdByCall, unused);
}

// The worn-side promise: nothing worn may stay above the cap.
static bool WornAboveCapStays(uint32_t itemLevel, uint32_t cap)
{
    return WornAboveCap(itemLevel, cap);
}

int main()
{
    // Pre-existing bag item above the cap: kept even when createdByCall=false.
    CHECK(!MayDestroyCreated(false, true));
    CHECK(!MayDestroyCreated(false, false));
    // A created item that ended up equipped or bagged-but-live: kept.
    CHECK(!MayDestroyCreated(true, false));
    // A created item that ended up unused: the only destroyable case.
    CHECK(MayDestroyCreated(true, true));

    // PurgeBagItem still gates the candidate pool by class/slot/cap, but the
    // service now consults it only for created items (see wiring pin).
    CHECK(PurgeBagItem(ITEM_CLASS_ARMOR, INVTYPE_CHEST, 70, 63, 0));
    CHECK(!PurgeBagItem(ITEM_CLASS_ARMOR, INVTYPE_CHEST, 63, 63, 0));

    // Worn items above the cap must be unequipped to the bags, not destroyed.
    CHECK(WornAboveCapStays(70, 63));
    CHECK(!WornAboveCapStays(63, 63));
    CHECK(!WornAboveCapStays(90, 0));

    std::set<uint32_t> created;
    CHECK(!CreatedItemUnused((uint32_t)0, created, true));
    created.insert(1001);
    CHECK(CreatedItemUnused(1001, created, true));
    CHECK(!CreatedItemUnused(1001, created, false));
    CHECK(!CreatedItemUnused(2002, created, true));

    std::printf("bot gear created-item policy: %d checks passed\n", checks);
    return 0;
}
