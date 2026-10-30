#include "../ai/playerbot/policy/loot/LootSlotRightPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::BotMayTakeLootSlot;

int main()
{
    std::cout << "Starting TortoiseBots loot-slot right policy tests...\n";

    // The rule StoreLootAction applied before: everything blocked or not in a
    // shared slot was skipped. It is the failing case of test 1, kept here so
    // the old and the new answer are both visible.
    auto OldRule = [](bool allowedForPlayer, bool blocked, bool sharedSlotAllowed)
    {
        return allowedForPlayer && !(blocked || !sharedSlotAllowed);
    };

    // 1. The Fel Moss case (#1307): a bot's own quest item arrives with
    // is_blocked set (the core marks every quest item blocked once it enters a
    // player's quest list) and no shared slot type, and must be taken anyway.
    CHECK(BotMayTakeLootSlot(true, true, false, /*blocked*/true, /*shared*/false) == true);
    CHECK(OldRule(true, /*blocked*/true, /*shared*/false) == false);
    std::cout << "  [PASS] a personal quest item is taken although the core marks it blocked\n";

    // 2. A shared item that is still blocked (a group roll pending) is not
    // this bot's yet.
    CHECK(BotMayTakeLootSlot(true, false, false, /*blocked*/true, /*shared*/true) == false);
    std::cout << "  [PASS] a shared item under a roll stays\n";

    // 3. A shared item with no block is taken only when the bot has a shared
    // slot type for it.
    CHECK(BotMayTakeLootSlot(true, false, false, /*blocked*/false, /*shared*/true) == true);
    CHECK(BotMayTakeLootSlot(true, false, false, /*blocked*/false, /*shared*/false) == false);
    std::cout << "  [PASS] a shared item follows the shared slot type\n";

    // 4. The bot's own party-loot copy: GetSlotTypeForSharedLoot answers MAX
    // for every free-for-all item, which must not decide its own copy.
    CHECK(BotMayTakeLootSlot(true, false, true, /*blocked*/false, /*shared*/false) == true);
    std::cout << "  [PASS] the bot's own free-for-all copy is taken\n";

    // 5. No right for this player at all: never, whatever else is true.
    CHECK(BotMayTakeLootSlot(false, true, false, /*blocked*/false, /*shared*/true) == false);
    std::cout << "  [PASS] a slot the player may not see is skipped\n";

    std::cout << "All loot-slot right policy tests passed.\n";
    return 0;
}
