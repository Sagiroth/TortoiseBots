#include "../runtime/DowntimePolicy.h"

#include <cassert>
#include <iostream>
#include <vector>

using namespace TortoiseBots;

static void TestStart()
{
    // Own alt, alive: allowed.
    assert(CheckDowntimeStart(true, true, false, false, false) == DowntimeStartRefusal::None);
    // Dead, hired, pool and autonomous are refused.
    assert(CheckDowntimeStart(false, true, false, false, false) == DowntimeStartRefusal::Dead);
    assert(CheckDowntimeStart(true, true, true, false, false) == DowntimeStartRefusal::NotOwnedAlt);
    assert(CheckDowntimeStart(true, false, false, true, false) == DowntimeStartRefusal::NotOwnedAlt);
    assert(CheckDowntimeStart(true, true, false, false, true) == DowntimeStartRefusal::Autonomous);
}

static void TestErrandOrder()
{
    std::vector<DowntimeErrand> none;
    // Owner's order: trainer first, then junk, then repair.
    assert(NextDowntimeErrand(none, true, true, true) == DowntimeErrand::Trainer);
    assert(NextDowntimeErrand(none, false, true, true) == DowntimeErrand::Vendor);
    assert(NextDowntimeErrand(none, false, false, true) == DowntimeErrand::Repair);

    // A tried errand is never repeated in the same window (a failed walk must
    // not loop), later ones still run.
    std::vector<DowntimeErrand> trainerTried = { DowntimeErrand::Trainer };
    assert(NextDowntimeErrand(trainerTried, true, true, true) == DowntimeErrand::Vendor);
    assert(NextDowntimeErrand(trainerTried, true, false, false) == DowntimeErrand::None);

    std::vector<DowntimeErrand> allTried = { DowntimeErrand::Trainer, DowntimeErrand::Vendor,
                                             DowntimeErrand::Repair };
    assert(NextDowntimeErrand(allTried, true, true, true) == DowntimeErrand::None);

    // Nothing needed: None even with nothing tried.
    assert(NextDowntimeErrand(none, false, false, false) == DowntimeErrand::None);
}

static void TestErrandDone()
{
    assert(DowntimeErrandDone(DowntimeErrand::Trainer, false, true, true));
    assert(!DowntimeErrandDone(DowntimeErrand::Trainer, true, false, false));
    assert(DowntimeErrandDone(DowntimeErrand::Vendor, true, false, true));
    assert(!DowntimeErrandDone(DowntimeErrand::Vendor, false, true, false));
    assert(DowntimeErrandDone(DowntimeErrand::Repair, true, true, false));
    assert(!DowntimeErrandDone(DowntimeErrand::Repair, false, false, true));
    assert(DowntimeErrandDone(DowntimeErrand::None, true, true, true));
}

// The junk predicate probe and sale share: a bag grey with a price, never
// soulbound, never equipped. Fed plain values so the header stays core-free.
static bool DowntimeJunkProbe(uint32_t quality, bool hasSellPrice, bool soulbound, bool equipped)
{
    if (quality != 0 /* ITEM_QUALITY_POOR */ || !hasSellPrice)
        return false;
    return !soulbound && !equipped;
}

static void TestJunkPredicate()
{
    // One normal grey: junk.
    assert(DowntimeJunkProbe(0, true, false, false));
    // Bound or worn stock is never junk, so probe and sale agree.
    assert(!DowntimeJunkProbe(0, true, true, false));
    assert(!DowntimeJunkProbe(0, true, false, true));
    assert(!DowntimeJunkProbe(0, true, true, true));
    // Whites, unsellable and free items are not junk.
    assert(!DowntimeJunkProbe(1, true, false, false));
    assert(!DowntimeJunkProbe(0, false, false, false));
}

// Any owner movement-mode delta ends the window (follow/wander/stay/guard and
// the free bit), so stay/guard orders cannot strand the bot. Fed plain values
// like the snapshot above.
struct DowntimeMovementKit
{
    bool follow = false;
    bool wander = false;
    bool stay = false;
    bool guard = false;
    bool free = false;
};

static bool DowntimeMovementRetaken(DowntimeMovementKit const& before, DowntimeMovementKit const& now)
{
    return now.follow != before.follow || now.wander != before.wander || now.stay != before.stay ||
        now.guard != before.guard || now.free != before.free;
}

static void TestMovementModeRetaken()
{
    DowntimeMovementKit const out = { false, false, false, false, true };
    // Unchanged kit: the window continues.
    assert(!DowntimeMovementRetaken(out, out));
    // Follow, stay, guard and wander orders all end it.
    assert(DowntimeMovementRetaken(out, { true, false, false, false, true }));
    assert(DowntimeMovementRetaken(out, { false, true, false, false, true }));
    assert(DowntimeMovementRetaken(out, { false, false, true, false, true }));
    assert(DowntimeMovementRetaken(out, { false, false, false, true, true }));
    // A dropped free bit (another path touching the mode) ends it too.
    assert(DowntimeMovementRetaken(out, { false, false, false, false, false }));
}

static void TestWindowCap()
{
    time_t const start = 1000000;
    assert(!DowntimeWindowExpired(start, start + kDowntimeMaxSeconds - 1));
    assert(DowntimeWindowExpired(start, start + kDowntimeMaxSeconds));
    assert(DowntimeWindowExpired(start, start + kDowntimeMaxSeconds * 10));
}

int main()
{
    TestStart();
    TestErrandOrder();
    TestErrandDone();
    TestJunkPredicate();
    TestMovementModeRetaken();
    TestWindowCap();
    std::cout << "downtime policy tests passed\n";
    return 0;
}
