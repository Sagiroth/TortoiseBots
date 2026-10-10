#include "../ai/playerbot/LoathebSporesPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::ShouldKillLoathebSpore;

int main()
{
    std::cout << "Starting TortoiseBots Loatheb-spores policy tests...\n";

    CHECK(ShouldKillLoathebSpore(true, 0.5f));
    CHECK(ShouldKillLoathebSpore(true, 1.0f));
    CHECK(!ShouldKillLoathebSpore(true, 1.1f));
    CHECK(!ShouldKillLoathebSpore(true, 10.0f));
    CHECK(!ShouldKillLoathebSpore(false, 0.5f));
    std::cout << "  [PASS] spore kill only within 1yd\n";

    std::cout << "All Loatheb-spores policy tests passed.\n";
    return 0;
}
