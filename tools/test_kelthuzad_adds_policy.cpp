#include "../ai/playerbot/KelthuzadAddsPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::KtPickTarget;

int main()
{
    std::cout << "Starting TortoiseBots Kel'Thuzad-adds policy tests...\n";

    // Ranged: soldier > weaver > abom > KT.
    CHECK(KtPickTarget(true, false, true, true, true, false, true) == 16427);
    CHECK(KtPickTarget(true, false, false, true, true, false, true) == 16429);
    CHECK(KtPickTarget(true, false, false, false, true, false, true) == 16428);
    CHECK(KtPickTarget(true, false, false, false, false, false, true) == 15990);
    CHECK(KtPickTarget(true, false, false, false, false, true, false) == 0);
    // Tanks: abom > guardian > KT.
    CHECK(KtPickTarget(false, true, true, true, true, true, true) == 16428);
    CHECK(KtPickTarget(false, true, true, true, false, true, true) == 16441);
    CHECK(KtPickTarget(false, true, true, true, false, false, true) == 15990);
    // Melee: abom > KT.
    CHECK(KtPickTarget(false, false, true, true, true, true, true) == 16428);
    CHECK(KtPickTarget(false, false, true, true, false, true, true) == 15990);
    CHECK(KtPickTarget(false, false, false, false, false, false, false) == 0);
    std::cout << "  [PASS] role-split add priorities match donor order\n";

    std::cout << "All Kel'Thuzad-adds policy tests passed.\n";
    return 0;
}
