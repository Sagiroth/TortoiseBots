// Standalone check for runtime/RunGivenItemRegistry.h (item-leg-skip review round 2, Codex 1): the run's given item
// instances by GUID; the glue (commands/Commander.cpp GiveRunItems/TakeRunItems/cleanup) and the bot item paths
// (GiveItemAction, SellAction) call it (tools/test_run_item_need_wiring.py pins the calls).
//   g++ -std=c++17 -Wall -Wextra tools/test_run_given_item_registry.cpp -o /tmp/t && /tmp/t
#include "../runtime/RunGivenItemRegistry.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace TortoiseBots;

// A given instance is known by its own GUID only: another instance of the same entry (the bot's own copy) is not.
static void TestRegistryKnowsTheGivenInstanceOnly()
{
    RunGivenItems::Add(5001);
    CHECK(RunGivenItems::Has(5001));
    CHECK(!RunGivenItems::Has(5002)); // the bot's own copy of the same key
    CHECK(!RunGivenItems::Has(0));
    RunGivenItems::Add(0);
    CHECK(!RunGivenItems::Has(0));
    RunGivenItems::Erase(5001);
    CHECK(!RunGivenItems::Has(5001));
}

int main()
{
    TestRegistryKnowsTheGivenInstanceOnly();
    std::cout << "ok\n";
    return 0;
}
