#include "../ai/playerbot/policy/gear/FeralSpecPolicy.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::ResolveDruidFeralSpecName;

int main()
{
    std::cout << "Starting TortoiseBots feral spec policy tests...\n";

    // Below level 20, Cat Form does not exist: low-level ferals are always
    // bear tanks regardless of role flag.
    CHECK(std::strcmp(ResolveDruidFeralSpecName(1, false), "feraltank") == 0);
    CHECK(std::strcmp(ResolveDruidFeralSpecName(10, false), "feraltank") == 0);
    CHECK(std::strcmp(ResolveDruidFeralSpecName(19, false), "feraltank") == 0);
    CHECK(std::strcmp(ResolveDruidFeralSpecName(19, true), "feraltank") == 0);
    std::cout << "  [PASS] level <= 19 resolves to feraltank\n";

    // At level 20+, Cat Form is available: active role decides stably.
    CHECK(std::strcmp(ResolveDruidFeralSpecName(20, true), "feraltank") == 0);
    CHECK(std::strcmp(ResolveDruidFeralSpecName(20, false), "feraldps") == 0);
    CHECK(std::strcmp(ResolveDruidFeralSpecName(60, true), "feraltank") == 0);
    CHECK(std::strcmp(ResolveDruidFeralSpecName(60, false), "feraldps") == 0);
    std::cout << "  [PASS] level >= 20 resolves by tank role\n";

    std::cout << "All feral spec policy checks PASSED!\n";
    return 0;
}
