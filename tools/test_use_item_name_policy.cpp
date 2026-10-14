#include "../ai/playerbot/strategy/actions/UseItemNamePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::UseNameMatchesObject;

int main()
{
    std::cout << "Starting TortoiseBots use-command object name match tests...\n";

    // The named object is a match, whether the typed text starts its name or sits inside it.
    CHECK(UseNameMatchesObject("Keystone", "Keystone") == true);
    CHECK(UseNameMatchesObject("Ironaya's Seal", "Seal") == true);
    std::cout << "  [PASS] object whose name contains the typed text is a match\n";

    // A nearby object with another name is not a match, and neither is one with no name.
    CHECK(UseNameMatchesObject("Torch", "Keystone") == false);
    CHECK(UseNameMatchesObject("", "Keystone") == false);
    std::cout << "  [PASS] unrelated or nameless object is not a match\n";

    std::cout << "All use-command object name match checks PASSED!\n";
    return 0;
}
