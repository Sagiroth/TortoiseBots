// Standalone regression test for the warlock curse default + conflict rule
// (runtime/WarlockCursePolicy.h): destruction opens Curse of the Elements,
// affliction/demonology stay on Curse of Agony, and no default fires while
// any warlock curse sits on the target (vanilla holds one curse per mob).
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_warlock_curse_policy.cpp -o /tmp/test_warlock_curse
//   /tmp/test_warlock_curse

#include "../runtime/WarlockCursePolicy.h"

#include <cstdio>
#include <cstdlib>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static WarlockCurseInputs Curse(WarlockSpec spec, bool knows, bool hasCurse)
{
    WarlockCurseInputs inputs;
    inputs.spec = spec;
    inputs.knowsDefaultCurse = knows;
    inputs.targetHasAnyCurse = hasCurse;
    return inputs;
}

// Destruction with CoE trained and a clean target: Elements.
static void TestDestroOpensElements()
{
    CHECK(DecideWarlockCurse(Curse(WarlockSpec::Destruction, true, false)) ==
        WarlockCurseDecision::CurseOfTheElements);
}

// Affliction / demonology with CoA trained and a clean target: Agony.
static void TestAffliDemoOpenAgony()
{
    CHECK(DecideWarlockCurse(Curse(WarlockSpec::Affliction, true, false)) ==
        WarlockCurseDecision::CurseOfAgony);
    CHECK(DecideWarlockCurse(Curse(WarlockSpec::Demonology, true, false)) ==
        WarlockCurseDecision::CurseOfAgony);
}

// Any curse on the target (mine or another warlock's): no default fires,
// so a second warlock never overwrites the first's curse.
static void TestConflictLeavesAlone()
{
    CHECK(DecideWarlockCurse(Curse(WarlockSpec::Destruction, true, true)) ==
        WarlockCurseDecision::LeaveAlone);
    CHECK(DecideWarlockCurse(Curse(WarlockSpec::Affliction, true, true)) ==
        WarlockCurseDecision::LeaveAlone);
    CHECK(DecideWarlockCurse(Curse(WarlockSpec::Demonology, true, true)) ==
        WarlockCurseDecision::LeaveAlone);
}

// Default not trained yet (low level): stay quiet.
static void TestUntrainedLeftAlone()
{
    CHECK(DecideWarlockCurse(Curse(WarlockSpec::Destruction, false, false)) ==
        WarlockCurseDecision::LeaveAlone);
    CHECK(DecideWarlockCurse(Curse(WarlockSpec::Affliction, false, false)) ==
        WarlockCurseDecision::LeaveAlone);
}

int main()
{
    std::printf("Starting warlock curse policy tests...\n");
    TestDestroOpensElements();
    std::printf("  [PASS] destro opens elements\n");
    TestAffliDemoOpenAgony();
    std::printf("  [PASS] affli/demo open agony\n");
    TestConflictLeavesAlone();
    std::printf("  [PASS] conflict leaves alone\n");
    TestUntrainedLeftAlone();
    std::printf("  [PASS] untrained left alone\n");
    std::printf("All warlock curse policy checks PASSED (%d assertions)!\n", checks);
    return 0;
}
