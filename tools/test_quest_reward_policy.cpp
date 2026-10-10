#include "../ai/playerbot/QuestRewardPolicy.h"

#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::QuestRewardTiebreak;

int main()
{
    std::cout << "Starting TortoiseBots quest-reward tiebreak tests...\n";

    // Empty set: defined, yields 0.
    CHECK(QuestRewardTiebreak({}) == 0);
    std::cout << "  [PASS] empty set yields 0\n";

    // Single candidate always wins.
    CHECK(QuestRewardTiebreak({{3, 100}}) == 3);
    std::cout << "  [PASS] single candidate wins\n";

    // Highest weight wins regardless of order.
    CHECK(QuestRewardTiebreak({{0, 100}, {1, 500}, {2, 200}}) == 1);
    CHECK(QuestRewardTiebreak({{2, 200}, {0, 100}, {1, 500}}) == 1);
    CHECK(QuestRewardTiebreak({{1, 500}, {2, 500}, {0, 100}}) == 1);
    std::cout << "  [PASS] highest weight wins\n";

    // Exact tie keeps the earlier (vendor-order) candidate.
    CHECK(QuestRewardTiebreak({{4, 300}, {7, 300}}) == 4);
    CHECK(QuestRewardTiebreak({{7, 300}, {4, 300}}) == 7);
    std::cout << "  [PASS] exact tie keeps vendor order\n";

    // Zero-weight candidates still pick the first, never a phantom.
    CHECK(QuestRewardTiebreak({{5, 0}, {6, 0}}) == 5);
    std::cout << "  [PASS] all-zero picks first\n";

    std::cout << "All TortoiseBots quest-reward tiebreak tests passed.\n";
    return 0;
}
