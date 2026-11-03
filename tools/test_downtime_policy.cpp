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

// The retaken check compares live state against the kit the window SET, not
// against the pre-window snapshot it replaced. Simulates the real sequence:
// snapshot (follow on, free off) -> enter parks follow and grants free ->
// first tick must NOT read as retaken; an owner stay order must.
struct DowntimeMovementKit
{
    bool follow = false;
    bool wander = false;
    bool stay = false;
    bool guard = false;
    bool free = false;
};

static bool DowntimeMovementRetaken(DowntimeMovementKit const& expected, DowntimeMovementKit const& now)
{
    return now.follow != expected.follow || now.wander != expected.wander || now.stay != expected.stay ||
        now.guard != expected.guard || now.free != expected.free;
}

static void TestMovementModeRetaken()
{
    DowntimeMovementKit const before = { true, false, false, false, false };
    // The window parks follow and grants free; that is the expected kit.
    DowntimeMovementKit const errand = { false, false, false, false, true };
    // snapshot -> enter -> first tick: no owner order landed, not retaken.
    assert(!DowntimeMovementRetaken(errand, errand));
    // The old check (live vs pre-window snapshot) misfires here: parked
    // follow and granted free both differ from `before`.
    assert(DowntimeMovementRetaken(before, errand));
    // Owner orders end it: stay, guard, follow, wander, or a dropped free bit.
    assert(DowntimeMovementRetaken(errand, { false, false, true, false, true }));
    assert(DowntimeMovementRetaken(errand, { false, false, false, true, true }));
    assert(DowntimeMovementRetaken(errand, { true, false, false, false, true }));
    assert(DowntimeMovementRetaken(errand, { false, true, false, false, true }));
    assert(DowntimeMovementRetaken(errand, { false, false, false, false, false }));
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
