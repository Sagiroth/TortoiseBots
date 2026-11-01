// Standalone check for the raid rule in runtime/policy/class/HunterFeignDeathPolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_hunter_raid_fd_policy.cpp -o /tmp/test_hunter_raid_fd && /tmp/test_hunter_raid_fd
#include "../runtime/policy/class/HunterFeignDeathPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace TortoiseBots;

int main()
{
    std::cout << "Starting TortoiseBots hunter raid feign death policy tests...\n";

    // Outside a raid group the old rule stands: threat climbing on a mob the
    // tank holds ({}) may still feign.
    CHECK(HunterFeignDeathRaidAllowed(false, {}, 60));
    CHECK(HunterFeignDeathRaidAllowed(false, {3}, 60));
    // Raid group: needs a mob on the hunter or near-max threat, and the
    // old evade veto still holds (a mob with only the hunter on its
    // threat list evades if feigned).
    CHECK(HunterFeignDeathRaidAllowed(true, {2}, 60));
    CHECK(!HunterFeignDeathRaidAllowed(true, {0}, 90));
    CHECK(HunterFeignDeathRaidAllowed(true, {}, 95));
    CHECK(!HunterFeignDeathRaidAllowed(true, {}, 60));
    CHECK(!HunterFeignDeathRaidAllowed(true, {}, 89));
    CHECK(HunterFeignDeathRaidAllowed(true, {1}, 60));
    std::cout << "  [PASS] raid feigns only on attackers or near-max threat\n";

    std::cout << "All TortoiseBots hunter raid feign death policy tests passed.\n";
    return 0;
}
