#include "../ai/playerbot/GluthKitePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::IsGluthChowExecute;
using ai::ShouldGluthTauntSwap;

int main()
{
    std::cout << "Starting TortoiseBots Gluth-kite policy tests...\n";

    // Swap only for a tank on Gluth while another tank holds 5+ stacks.
    CHECK(ShouldGluthTauntSwap(true, true, true, 5));
    CHECK(ShouldGluthTauntSwap(true, true, true, 9));
    CHECK(!ShouldGluthTauntSwap(true, true, true, 4));
    CHECK(!ShouldGluthTauntSwap(false, true, true, 8));
    CHECK(!ShouldGluthTauntSwap(true, false, true, 8));
    CHECK(!ShouldGluthTauntSwap(true, true, false, 8));
    std::cout << "  [PASS] wound swap needs tank + Gluth + wounded other tank\n";

    CHECK(IsGluthChowExecute(5.0f));
    CHECK(IsGluthChowExecute(10.0f));
    CHECK(!IsGluthChowExecute(10.1f));
    CHECK(!IsGluthChowExecute(80.0f));
    std::cout << "  [PASS] chow execute threshold is 10%\n";

    std::cout << "All Gluth-kite policy tests passed.\n";
    return 0;
}
