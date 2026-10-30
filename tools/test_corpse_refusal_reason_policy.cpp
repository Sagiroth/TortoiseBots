#include "../ai/playerbot/policy/loot/CorpseRefusalReasonPolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::CorpseRefusalReason;

int main()
{
    std::cout << "Starting TortoiseBots corpse refusal reason policy tests...\n";

    std::string const notInRange = "not in reward range at the kill (no loot right, no quest copy)";

    // 1. The failing case: not on the looter list at the kill. The reason that
    // ignores presence would say "another member's turn" and hide the real cause.
    CHECK(CorpseRefusalReason(/*present*/false, /*rolled*/true, /*has*/false, /*otherTurn*/true,
                              /*reserved*/false) == notInRange);
    std::cout << "  [PASS] absence from the kill is reported before the turn\n";

    // 2. Presence comes before the drop roll as well.
    CHECK(CorpseRefusalReason(false, /*rolled*/false, /*has*/false, /*otherTurn*/true,
                              /*reserved*/false) == notInRange);
    std::cout << "  [PASS] absence from the kill is reported before an empty quest roll\n";

    // 3. Present at the kill, another member's turn, no visible copy for this bot.
    CHECK(CorpseRefusalReason(true, true, false, true, false) ==
        "another member's turn; no quest item for this bot (quest not INCOMPLETE, or the copy is taken)");
    std::cout << "  [PASS] another member's turn is reported when no copy is visible\n";

    // 4. Nothing quest-related rolled on the corpse at all.
    CHECK(CorpseRefusalReason(true, false, false, true, false) == "no quest item rolled");
    std::cout << "  [PASS] an empty quest roll is reported\n";

    // 5. The bot has a quest item in the list yet the corpse was refused.
    CHECK(CorpseRefusalReason(true, true, true, true, false) == "has own item but refused (unexpected)");
    std::cout << "  [PASS] an unexpected personal item is reported\n";

    // 6. The core keeps the copy for the member whose round-robin turn it is.
    CHECK(CorpseRefusalReason(true, true, false, true, true) ==
        "quest item reserved for the round-robin member until it loots or releases");
    std::cout << "  [PASS] the round-robin reservation is reported\n";

    // 7. No cause applies: present, a copy rolled, the bot's turn, nothing visible.
    CHECK(CorpseRefusalReason(true, true, false, false, false) == "no right for this bot");
    std::cout << "  [PASS] the fallback reason is reported\n";

    std::cout << "All corpse refusal reason policy tests passed.\n";
    return 0;
}
