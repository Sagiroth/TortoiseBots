#include "../runtime/LineagePolicy.h"

#include <cassert>
#include <iostream>

using namespace TortoiseBots;

static void TestWantedAndCeiling()
{
    assert(LineageLowBandWanted(900, 2) == 1800);
    assert(LineageLowBandWanted(900, 0) == 0);
    assert(LineagePoolCeiling(1000, 10) == 10000);
    // 0 is clamped to 1: the ceiling never drops below the online target.
    assert(LineagePoolCeiling(1000, 0) == 1000);
}

static void TestGate()
{
    // Low bands short, nothing created yet: roll.
    assert(CheckLineageRoll(800, 1800, 1014, 10000, 1000, 0, 10) == LineageRollGate::Roll);
    // Enough stock below 60.
    assert(CheckLineageRoll(1800, 1800, 2000, 10000, 1000, 0, 10) == LineageRollGate::Enough);
    // Ceiling wins over everything, even with a short low band.
    assert(CheckLineageRoll(10, 1800, 10000, 10000, 1000, 0, 10) == LineageRollGate::Ceiling);
    // 10 per hour = one alt every 360 s.
    assert(CheckLineageRoll(800, 1800, 1014, 10000, 1359, 1000, 10) == LineageRollGate::NotYet);
    assert(CheckLineageRoll(800, 1800, 1014, 10000, 1360, 1000, 10) == LineageRollGate::Roll);
    assert(CheckLineageRoll(800, 1800, 1014, 10000, 5000, 1000, 0) == LineageRollGate::RateZero);
}

static void TestParent()
{
    std::vector<LineageParentOption> none;
    assert(PickLineageParent(none, 3) == 0);

    std::vector<LineageParentOption> o = {{10, 3}, {11, 1}, {12, 2}, {13, 1}};
    // Only the smallest lines (size 1) are picked; the roll walks the ties.
    assert(PickLineageParent(o, 0) == 11);
    assert(PickLineageParent(o, 1) == 13);
    assert(PickLineageParent(o, 2) == 11);
}

static void TestClass()
{
    std::vector<uint8_t> const alliance = {1, 2, 4, 5, 8, 9, 11};
    // Warrior (1) and paladin (2) taken: the pick skips them.
    uint32_t taken = (1u << 0) | (1u << 1);
    for (uint32_t roll = 0; roll < 20; ++roll)
    {
        uint8_t cls = PickLineageClass(taken, alliance, roll);
        assert(cls != 1 && cls != 2);
    }
    // Every class taken: any valid class.
    uint32_t all = 0;
    for (uint8_t c : alliance)
        all |= 1u << (c - 1);
    assert(PickLineageClass(all, alliance, 0) == 1);
    assert(PickLineageClass(all, alliance, 8) == 2);
    std::vector<uint8_t> empty;
    assert(PickLineageClass(0, empty, 0) == 0);
}

int main()
{
    TestWantedAndCeiling();
    TestGate();
    TestParent();
    TestClass();
    std::cout << "lineage policy: all tests passed\n";
    return 0;
}
