#include "../ai/playerbot/VaelPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::kVaelRangedBiasThreshold;
using ai::kVaelRepulsionRange;
using ai::kVaelTailSweepRecoveryRange;
using ai::ShouldBiasFleeToVaelBoss;
using ai::ShouldClusterWithBaCarriers;
using ai::ShouldRecoverToVaelBoss;
using ai::ShouldVaelVictimHold;

int main()
{
    std::cout << "Starting TortoiseBots vael policy tests...\n";

    // Pinned geometry: 20y repulsion, 30y tail-sweep recovery, 25y ranged bias.
    CHECK(kVaelRepulsionRange == 20.0f);
    CHECK(kVaelTailSweepRecoveryRange == 30.0f);
    CHECK(kVaelRangedBiasThreshold == 25.0f);
    std::cout << "  [PASS] geometry pinned\n";

    // Victim holds while the boss lives; dead boss releases everyone.
    CHECK(ShouldVaelVictimHold(true, true));
    CHECK(!ShouldVaelVictimHold(true, false));
    CHECK(!ShouldVaelVictimHold(false, true));
    std::cout << "  [PASS] victim hold rule\n";

    // BA clustering: while the boss lives, other carriers are ignored as
    // repulsion sources; after the kill everyone repels everyone.
    CHECK(ShouldClusterWithBaCarriers(true, true));
    CHECK(!ShouldClusterWithBaCarriers(true, false));
    CHECK(!ShouldClusterWithBaCarriers(false, true));
    std::cout << "  [PASS] BA clustering rule\n";

    // Tail-sweep recovery: no push + live boss + past 30y walks back.
    CHECK(ShouldRecoverToVaelBoss(false, true, true));
    CHECK(!ShouldRecoverToVaelBoss(true, true, true));
    CHECK(!ShouldRecoverToVaelBoss(false, false, true));
    CHECK(!ShouldRecoverToVaelBoss(false, true, false));
    std::cout << "  [PASS] tail-sweep recovery rule\n";

    // Ranged bias: ranged past 25y with a live boss blends toward it.
    CHECK(ShouldBiasFleeToVaelBoss(true, true, true));
    CHECK(!ShouldBiasFleeToVaelBoss(false, true, true));
    CHECK(!ShouldBiasFleeToVaelBoss(true, false, true));
    CHECK(!ShouldBiasFleeToVaelBoss(true, true, false));
    std::cout << "  [PASS] ranged bias rule\n";

    std::cout << "All vael policy tests passed.\n";
    return 0;
}
