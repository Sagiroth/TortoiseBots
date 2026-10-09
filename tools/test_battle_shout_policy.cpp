#include "../ai/playerbot/BattleShoutPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::BattleShoutAttackPower;
using ai::BlessingOfMightAttackPower;
using ai::ShouldBattleShout;

int main()
{
    std::cout << "Starting TortoiseBots battle-shout policy tests...\n";

    // Tables match spell_template EffectBasePoints+1 (verified 1.18.1).
    CHECK(BattleShoutAttackPower(6673) == 15);
    CHECK(BattleShoutAttackPower(5242) == 35);
    CHECK(BattleShoutAttackPower(6192) == 55);
    CHECK(BattleShoutAttackPower(11549) == 85);
    CHECK(BattleShoutAttackPower(11550) == 130);
    CHECK(BattleShoutAttackPower(11551) == 185);
    CHECK(BattleShoutAttackPower(25289) == 232);
    CHECK(BlessingOfMightAttackPower(19740) == 20);
    CHECK(BlessingOfMightAttackPower(19834) == 35);
    CHECK(BlessingOfMightAttackPower(19835) == 55);
    CHECK(BlessingOfMightAttackPower(19836) == 85);
    CHECK(BlessingOfMightAttackPower(19837) == 115);
    CHECK(BlessingOfMightAttackPower(19838) == 155);
    CHECK(BlessingOfMightAttackPower(25782) == 155);
    CHECK(BlessingOfMightAttackPower(25916) == 185);
    CHECK(BattleShoutAttackPower(0) == 0);
    CHECK(BlessingOfMightAttackPower(999999) == 0);
    std::cout << "  [PASS] AP tables cover every rank\n";

    // No might up: always shout.
    CHECK(ShouldBattleShout(15, false, 0));
    CHECK(ShouldBattleShout(0, false, 0));
    std::cout << "  [PASS] no might means shout\n";

    // Might at/above shout AP suppresses (equal still skips: zero gain).
    CHECK(!ShouldBattleShout(130, true, 155));
    CHECK(!ShouldBattleShout(155, true, 155));
    CHECK(!ShouldBattleShout(35, true, 35));
    std::cout << "  [PASS] equal-or-stronger might suppresses\n";

    // Stronger shout still fires (late-rank shout vs early might).
    CHECK(ShouldBattleShout(185, true, 155));
    CHECK(ShouldBattleShout(232, true, 185));
    CHECK(ShouldBattleShout(35, true, 20));
    std::cout << "  [PASS] stronger shout still fires\n";

    // Unknown aura id maps to 0 AP: never suppresses a known shout.
    CHECK(ShouldBattleShout(15, true, BlessingOfMightAttackPower(12345)));
    std::cout << "  [PASS] unknown aura never suppresses\n";

    std::cout << "All battle-shout policy tests passed.\n";
    return 0;
}
