#include "../ai/playerbot/CastCancelPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::CastCancelPct;

int main()
{
    std::cout << "Starting TortoiseBots cast-cancel policy tests...\n";

    // CastCancel_ProgressIsElapsedOverTotal: the core's m_timer starts at the total and counts to 0, so the progress
    // is (total - remaining) / total. A fresh cast reads 0, a half cast 50, a finished one 100.
    CHECK(CastCancelPct(1500, 1500) == 0);
    CHECK(CastCancelPct(1500, 750) == 50);
    CHECK(CastCancelPct(1500, 0) == 100);
    CHECK(CastCancelPct(2500, 2400) == 4);
    CHECK(CastCancelPct(1000, 1) == 99);
    std::cout << "  [PASS] the progress is the elapsed share of the cast\n";

    // A remaining above the total is a fresh cast (a pushback or a re-prepared timer), never a negative progress; an
    // instant spell has no total and no bar to lose, so it reads -1 instead of a made-up 0.
    CHECK(CastCancelPct(1500, 2000) == 0);
    CHECK(CastCancelPct(1500, 0xFFFFFFFFu) == 0);
    CHECK(CastCancelPct(0, 0) == -1);
    CHECK(CastCancelPct(-1, 10) == -1);
    std::cout << "  [PASS] an unknown total reads -1 and a fresh cast 0\n";

    std::cout << "TortoiseBots cast-cancel policy tests passed.\n";
    return 0;
}
