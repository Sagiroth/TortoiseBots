#include "../ai/playerbot/policy/loot/QuestSourceItemPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::QuestSourceItemStillNeeded;

int main()
{
    std::cout << "Starting TortoiseBots quest-source item policy tests...\n";

    // The rule IsNeededForQuest applied before: only quest->ReqItemId matched, so a
    // quest's SOURCE item (ReqSourceId, e.g. the Gnarlpine Necklace for Ferocitas
    // the Dream Eater) was never needed. It is the failing case of test 1, kept
    // here so the old and the new answer are both visible.
    auto OldRule = [](uint32_t, uint32_t, uint32_t, uint32_t)
    {
        return false;
    };

    // 1. A unique source item (MaxCount 1) with none held is needed; the old rule
    // said no.
    CHECK(QuestSourceItemStillNeeded(/*held*/0, /*maxCount*/1, /*reqSourceCount*/0, /*stackable*/1) == true);
    CHECK(OldRule(0, 1, 0, 1) == false);
    std::cout << "  [PASS] a unique source item with none held is needed\n";

    // 2. The same unique item once held is not needed again.
    CHECK(QuestSourceItemStillNeeded(/*held*/1, /*maxCount*/1, /*reqSourceCount*/0, /*stackable*/1) == false);
    std::cout << "  [PASS] a unique source item already held is not needed\n";

    // 3. A counted source item (ReqSourceCount 3) with 2 held is still needed.
    CHECK(QuestSourceItemStillNeeded(/*held*/2, /*maxCount*/0, /*reqSourceCount*/3, /*stackable*/5) == true);
    std::cout << "  [PASS] a counted source item below its count is needed\n";

    // 4. The counted source item at its count is not needed.
    CHECK(QuestSourceItemStillNeeded(/*held*/3, /*maxCount*/0, /*reqSourceCount*/3, /*stackable*/5) == false);
    std::cout << "  [PASS] a counted source item at its count is not needed\n";

    // 5. With neither MaxCount nor ReqSourceCount set, need holds below one stack.
    CHECK(QuestSourceItemStillNeeded(/*held*/4, /*maxCount*/0, /*reqSourceCount*/0, /*stackable*/5) == true);
    CHECK(QuestSourceItemStillNeeded(/*held*/5, /*maxCount*/0, /*reqSourceCount*/0, /*stackable*/5) == false);
    std::cout << "  [PASS] an uncounted source item is needed below one stack\n";

    std::cout << "All quest-source item policy tests passed.\n";
    return 0;
}
