#include "../ai/playerbot/policy/loot/LootViewRulePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::PersonalCopyVisible;

int main()
{
    std::cout << "Starting TortoiseBots loot-view rule policy tests...\n";

    // The rule the module applied before: Loot::hasItemFor, which ignores the
    // round-robin reservation. It is the failing case of test 1, kept here so
    // the old and the new answer are both visible.
    auto OldRule = [](bool freeforall, bool turnAssigned, bool viewerHoldsTurn, bool turnHolderHasCopyInList)
    {
        (void)freeforall; (void)turnAssigned; (void)viewerHoldsTurn; (void)turnHolderHasCopyInList;
        return true;
    };

    // 1. The P3 case (#1307): both members are on the quest, the other member
    // holds the turn, and the core hides this viewer's non-FFA quest copy until
    // that member loots or releases. hasItemFor still says "lootable", so the
    // bot walked to the corpse and opened an empty window.
    CHECK(PersonalCopyVisible(/*ffa*/false, /*turn*/true, /*viewerTurn*/false, /*holderCopy*/true) == false);
    CHECK(OldRule(false, true, false, true) == true);
    std::cout << "  [PASS] a reserved quest copy is hidden from the other member\n";

    // 2. The turn holder has no copy in the same list (the P1 case: the
    // commander is not on the quest), so nothing is reserved.
    CHECK(PersonalCopyVisible(false, /*turn*/true, /*viewerTurn*/false, /*holderCopy*/false) == true);
    std::cout << "  [PASS] no reservation when the turn holder has no copy\n";

    // 3. The turn was released (roundRobinPlayer = 0): every copy shows again.
    CHECK(PersonalCopyVisible(false, /*turn*/false, /*viewerTurn*/false, /*holderCopy*/true) == true);
    std::cout << "  [PASS] a released turn shows the copy\n";

    // 4. The viewer is the turn holder: its own copy is shown.
    CHECK(PersonalCopyVisible(false, /*turn*/true, /*viewerTurn*/true, /*holderCopy*/true) == true);
    std::cout << "  [PASS] the turn holder sees its own copy\n";

    // 5. A free-for-all copy is always shown, whoever holds the turn.
    CHECK(PersonalCopyVisible(/*ffa*/true, /*turn*/true, /*viewerTurn*/false, /*holderCopy*/true) == true);
    std::cout << "  [PASS] a free-for-all copy ignores the turn\n";

    std::cout << "All loot-view rule policy tests passed.\n";
    return 0;
}
