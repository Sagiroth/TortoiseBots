// Standalone regression test for the sliced travel pick
// (ai/playerbot/TravelPickSlicePolicy.h): the resume machinery behind the
// per-visit travel-pick deadline. Exercises the rules that must not regress —
// cursor advance walks partitions in order without skipping or repeating,
// validity restarts the scan when the bot moved cells or the list refreshed,
// the deadline never fires when the budget is 0 and fires once elapsed, and
// a resumed scan replays the same candidate order.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_travel_pick_slice.cpp -o /tmp/test_travel_pick_slice
//   /tmp/test_travel_pick_slice

#include "../ai/playerbot/TravelPickSlicePolicy.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <thread>
#include <vector>

using namespace ai;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

using FakePartitions = std::map<uint32_t, std::vector<int>>;

static void TestAdvanceWalksInOrder()
{
    FakePartitions partitions = {{100, {1, 2, 3}}, {250, {4}}, {500, {5, 6}}};
    TravelPickCursor cursor;
    cursor.partitionKey = 100;
    cursor.pointIndex = 0;
    std::vector<int> visited;
    visited.push_back(partitions[cursor.partitionKey][cursor.pointIndex]);
    while (TravelPickAdvance(cursor, partitions))
        visited.push_back(partitions[cursor.partitionKey][cursor.pointIndex]);
    // Every candidate exactly once, in partition then list order.
    CHECK((visited == std::vector<int>{1, 2, 3, 4, 5, 6}));
}

static void TestAdvanceSkipsEmptyAndEnds()
{
    FakePartitions partitions = {{100, {1}}};
    TravelPickCursor cursor;
    cursor.partitionKey = 100;
    cursor.pointIndex = 0;
    CHECK(!TravelPickAdvance(cursor, partitions));
    // Unknown partition key: exhausted, never invented.
    cursor.partitionKey = 999;
    CHECK(!TravelPickAdvance(cursor, partitions));
}

static void TestValidity()
{
    // Same cell, same list: resume.
    CHECK(TravelPickScanValid(0, 5, 7, 3, 100, 0, 5, 7, 3, 100));
    // Bot moved cells: restart.
    CHECK(!TravelPickScanValid(0, 5, 7, 3, 100, 0, 6, 7, 3, 100));
    CHECK(!TravelPickScanValid(0, 5, 7, 3, 100, 1, 5, 7, 3, 100));
    // List refreshed (size or head changed): restart.
    CHECK(!TravelPickScanValid(0, 5, 7, 3, 100, 0, 5, 7, 4, 100));
    CHECK(!TravelPickScanValid(0, 5, 7, 3, 100, 0, 5, 7, 3, 250));
}

static void TestDeadline()
{
    auto const start = std::chrono::steady_clock::now();
    // Zero budget disables the deadline (synchronous legacy behavior).
    CHECK(!TravelPickOverBudget(start, 0));
    // A huge budget never fires immediately.
    CHECK(!TravelPickOverBudget(start, 100000000ULL));
    // A tiny budget fires after a short sleep.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(TravelPickOverBudget(start, 1000));
}

static void TestResumeReplaysOrder()
{
    // Simulate a two-visit pick: visit 1 evaluates the first three
    // candidates then yields; visit 2 resumes at the cursor and must see
    // candidates 4-6 in order, never re-evaluating 1-3.
    FakePartitions partitions = {{100, {1, 2, 3}}, {250, {4, 5, 6}}};
    TravelPickCursor cursor;
    cursor.partitionKey = 100;
    cursor.pointIndex = 0;
    cursor.started = true;
    cursor.distanceChecked = true;
    std::vector<int> visit1;
    for (int i = 0; i < 3; ++i)
    {
        visit1.push_back(partitions[cursor.partitionKey][cursor.pointIndex]);
        CHECK(TravelPickAdvance(cursor, partitions));
    }
    CHECK((visit1 == std::vector<int>{1, 2, 3}));
    std::vector<int> visit2;
    visit2.push_back(partitions[cursor.partitionKey][cursor.pointIndex]);
    while (TravelPickAdvance(cursor, partitions))
        visit2.push_back(partitions[cursor.partitionKey][cursor.pointIndex]);
    CHECK((visit2 == std::vector<int>{4, 5, 6}));
}

int main()
{
    TestAdvanceWalksInOrder();
    TestAdvanceSkipsEmptyAndEnds();
    TestValidity();
    TestDeadline();
    TestResumeReplaysOrder();
    std::printf("travel pick slice: all %d checks passed\n", checks);
    return 0;
}
