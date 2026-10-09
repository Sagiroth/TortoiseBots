#include "../ai/playerbot/BgInvitePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::BgInvitePending;
using ai::BgInviteSlotCount;

int main()
{
    std::cout << "Starting TortoiseBots bg-invite polling tests...\n";

    // The rule covers the core's three queue slots.
    CHECK(BgInviteSlotCount() == 3);
    std::cout << "  [PASS] slot count matches PLAYER_MAX_BATTLEGROUND_QUEUES\n";

    // No invite on any slot: quiet, so queued bots do not re-request
    // battlefield status every tick while merely waiting.
    CHECK(!BgInvitePending(false));
    CHECK(!BgInvitePending(false, false, false));
    std::cout << "  [PASS] queued-but-not-invited stays silent\n";

    // An invite on any slot fires: the status re-request re-emits the
    // invite and the normal "bg status" port path runs.
    CHECK(BgInvitePending(true));
    CHECK(BgInvitePending(false, true));
    CHECK(BgInvitePending(false, false, true));
    CHECK(BgInvitePending(true, true, true));
    std::cout << "  [PASS] invite on any slot fires\n";

    std::cout << "All TortoiseBots bg-invite polling tests passed.\n";
    return 0;
}
