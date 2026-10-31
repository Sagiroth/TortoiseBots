#include "../ai/playerbot/WarriorRageGatePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

int main()
{
    std::cout << "Starting TortoiseBots warrior rage gate policy tests...\n";

    // Disarm: 20 rage and a target that carries a weapon.
    CHECK(ShouldDisarm(true, 20));
    CHECK(ShouldDisarm(true, 100));
    CHECK(!ShouldDisarm(true, 19));   // below the rage cost
    CHECK(!ShouldDisarm(true, 0));
    CHECK(!ShouldDisarm(false, 100)); // weaponless mob: rage would be wasted
    CHECK(!ShouldDisarm(false, 0));
    std::cout << "  [PASS] disarm gates on rage and weapon\n";

    // Bloodrage: off cooldown, health at least medium, rage below 20.
    CHECK(ShouldBloodrage(true, true, 0));
    CHECK(ShouldBloodrage(true, true, 19));
    CHECK(!ShouldBloodrage(false, true, 0));  // 60 s cooldown running
    CHECK(!ShouldBloodrage(true, false, 0));  // too hurt to pay the health cost
    CHECK(!ShouldBloodrage(true, true, 20));  // already enough rage
    CHECK(!ShouldBloodrage(true, true, 50));
    std::cout << "  [PASS] bloodrage gates on cooldown, health and rage\n";

    // Battle Shout: only at 10+ rage; buff/might logic is BattleShoutPolicy.
    CHECK(ShouldBattleShout(10));
    CHECK(ShouldBattleShout(15));
    CHECK(!ShouldBattleShout(9));    // below the rage cost
    CHECK(!ShouldBattleShout(0));
    std::cout << "  [PASS] battle shout gates on rage\n";
    // Rend is a 10-rage builder that must not eat the main strike's bar:
    // with Mortal Strike/Bloodthirst learned, only re-apply Rend when the
    // 30-rage main strike stays affordable afterwards.
    CHECK(!ShouldRendForDps(true, 39));
    CHECK(ShouldRendForDps(true, 40));
    CHECK(ShouldRendForDps(true, 100));
    CHECK(!ShouldRendForDps(true, 0));
    // Without a main strike the old unconditional row stands.
    CHECK(ShouldRendForDps(false, 0));
    CHECK(ShouldRendForDps(false, 10));
    CHECK(WARRIOR_REND_RAGE_COST == 10);
    CHECK(WARRIOR_MAIN_STRIKE_RAGE_COST == 30);
    std::cout << "  [PASS] rend holds the main strike's rage pool\n";
    // Mocking Blow: the second taunt when Taunt is down and the mob left.
    CHECK(ShouldMockingBlow(true, true, false, true, true, 10));
    CHECK(ShouldMockingBlow(true, true, false, true, true, 100));
    CHECK(!ShouldMockingBlow(true, true, true, true, true, 10));  // taunt ready: taunt first
    CHECK(!ShouldMockingBlow(false, true, false, true, true, 10)); // unknown spell
    CHECK(!ShouldMockingBlow(true, false, false, true, true, 10)); // aggro held
    CHECK(!ShouldMockingBlow(true, true, false, false, true, 10)); // mocking on cooldown
    CHECK(!ShouldMockingBlow(true, true, false, true, false, 10)); // out of melee
    CHECK(!ShouldMockingBlow(true, true, false, true, true, 9));  // below the 10 rage cost
    CHECK(!ShouldMockingBlow(true, true, false, true, true, 0));
    std::cout << "  [PASS] mocking blow gates on taunt cooldown, aggro, melee and rage\n";

    // The rage paid in Battle Stance is the rage AFTER the swap: only what
    // Tactical Mastery keeps (5 per rank) survives the dance.
    CHECK(RageAfterStanceSwap(true, 30, 0) == 30);
    CHECK(RageAfterStanceSwap(false, 30, 0) == 0);   // no TM: nothing survives
    CHECK(RageAfterStanceSwap(false, 30, 1) == 5);
    CHECK(RageAfterStanceSwap(false, 30, 2) == 10);
    CHECK(RageAfterStanceSwap(false, 30, 3) == 15);
    CHECK(RageAfterStanceSwap(false, 30, 4) == 20);
    CHECK(RageAfterStanceSwap(false, 30, 5) == 25);
    CHECK(RageAfterStanceSwap(false, 4, 3) == 4);    // less than the cap: all kept
    CHECK(TacticalMasteryRank(false, false, false) == 0);
    CHECK(TacticalMasteryRank(true, false, false) == 1);
    CHECK(TacticalMasteryRank(false, true, false) == 2);
    CHECK(TacticalMasteryRank(false, false, true) == 3);
    CHECK(TacticalMasteryRank(true, true, true) == 3); // idempotent on train
    CHECK(TacticalMasteryRank(false, false, false, true, false) == 4);
    CHECK(TacticalMasteryRank(false, false, false, false, true) == 5);
    CHECK(TacticalMasteryRank(true, true, true, true, true) == 5);
    // End to end: rage 30 with no TM is 0 after the swap -> no Mocking Blow;
    // with TM 2 the 10 kept pay for it.
    CHECK(!ShouldMockingBlow(true, true, false, true, true, RageAfterStanceSwap(false, 30, 0)));
    CHECK(ShouldMockingBlow(true, true, false, true, true, RageAfterStanceSwap(false, 30, 2)));
    std::cout << "  [PASS] mocking blow gates on post-swap rage (Tactical Mastery)\n";

    // Vanilla cost constants are pinned so a tuning change cannot silently
    // desync the trigger gates from the spell data.
    CHECK(WARRIOR_DISARM_RAGE_COST == 20);
    CHECK(WARRIOR_BATTLE_SHOUT_RAGE_COST == 10);
    CHECK(WARRIOR_BLOODRAGE_RAGE_FLOOR == 20);
    CHECK(WARRIOR_MOCKING_BLOW_RAGE_COST == 10);
    // Talent.dbc talent 128, 5 ranks (12295 / 12676 / 12677 / 12678 / 12679).
    CHECK(WARRIOR_TACTICAL_MASTERY_RANK_1 == 12295);
    CHECK(WARRIOR_TACTICAL_MASTERY_RANK_2 == 12676);
    CHECK(WARRIOR_TACTICAL_MASTERY_RANK_3 == 12677);
    CHECK(WARRIOR_TACTICAL_MASTERY_RANK_4 == 12678);
    CHECK(WARRIOR_TACTICAL_MASTERY_RANK_5 == 12679);
    CHECK(WARRIOR_TACTICAL_MASTERY_KEPT_PER_RANK == 5);
    std::cout << "  [PASS] cost constants pinned\n";

    std::cout << "All TortoiseBots warrior rage gate policy tests passed.\n";
    return 0;
}
