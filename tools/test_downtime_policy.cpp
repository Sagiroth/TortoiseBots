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
    TestWindowCap();
    std::cout << "downtime policy tests passed\n";
    return 0;
}
