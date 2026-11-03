// The ready check's pure facts (task R4m): which auras count as the raid's
// buffs and as consumables, which spells are key cooldowns, and which one
// spell per chain is sent. Plain data and the standard library only.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra -Werror=return-type tools/test_ready_check_policy.cpp -o /tmp/ready_check
//   /tmp/ready_check

#include "../runtime/ReadyCheckPolicy.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace TortoiseBots::ReadyCheck;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static AuraFact Aura(uint32_t type, int32_t misc, uint32_t family, uint32_t mask = 0, bool positive = true,
    bool fromPlayer = true)
{
    AuraFact f;
    f.auraType = type;
    f.misc = misc;
    f.family = family;
    f.elixirMask = mask;
    f.positive = positive;
    f.fromPlayer = fromPlayer;
    return f;
}

static SpellCd Cd(uint32_t spell, uint32_t chainFirst, uint32_t recovery, uint32_t category = 0, bool passive = false,
    uint32_t family = 6)
{
    SpellCd c;
    c.spell = spell;
    c.chainFirst = chainFirst;
    c.recoveryMs = recovery;
    c.categoryRecoveryMs = category;
    c.passive = passive;
    c.family = family;
    return c;
}

// The numbers mirror the core: an aura or stat index that drifts there makes
// the ready check grade the wrong buff, so they are pinned.
static void TestConstants()
{
    CHECK(kAuraModStat == 29);          // Spells/SpellAuraDefines.h:167
    CHECK(kAuraModPercentStat == 80);   // :218
    CHECK(kAuraModPowerRegen == 85);    // :223
    CHECK(kAuraModAttackPower == 99);   // :237
    CHECK(kStatAll == -1);
    CHECK(kStatStamina == 2);           // SharedDefines.h:123
    CHECK(kStatIntellect == 3);         // :124
    CHECK(kStatSpirit == 4);            // :125
    CHECK(kPowerMana == 0);             // :132
    CHECK(kFamilyGeneric == 0);         // Database/DBCEnums.h:162
    CHECK(kFamilyPotion == 13);         // :175
    CHECK(kElixirFlaskMask == 0x03);    // Spells/SpellMgr.h:104
    CHECK(kElixirWellFed == 0x10);      // :105
    CHECK(kKeyCooldownMs == 120000);
    CHECK(kReadyRefreshMs == 10000);
    CHECK(kKeySpellsRefreshMs == 60000);
    CHECK(kMaxKeySpells == 8);
}

static void TestRaidBuffs()
{
    // A Fortitude-like aura: MOD_STAT with stamina, cast by a player.
    CHECK(BuffBits({Aura(kAuraModStat, kStatStamina, 6)}) == 1);
    // A consumable (family generic) is not a raid buff even with the same effect.
    CHECK(BuffBits({Aura(kAuraModStat, kStatStamina, kFamilyGeneric)}) == 0);
    // Gift of the Wild-like: all stats, both the flat and the percent aura.
    CHECK(BuffBits({Aura(kAuraModStat, kStatAll, 7)}) == 8);
    CHECK(BuffBits({Aura(kAuraModPercentStat, kStatAll, 7)}) == 8);
    // Arcane Intellect-like and Divine Spirit-like.
    CHECK(BuffBits({Aura(kAuraModStat, kStatIntellect, 3)}) == 2);
    CHECK(BuffBits({Aura(kAuraModStat, kStatSpirit, 6)}) == 4);
    // Strength and agility are not graded: no bit.
    CHECK(BuffBits({Aura(kAuraModStat, 0, 6)}) == 0);
    CHECK(BuffBits({Aura(kAuraModStat, 1, 6)}) == 0);
    // Power Word: Fortitude-like percent stamina still counts as stamina.
    CHECK(BuffBits({Aura(kAuraModPercentStat, kStatStamina, 6)}) == 1);
    // Attack power (Battle Shout-like) and mana regeneration (Blessing of Wisdom-like).
    CHECK(BuffBits({Aura(kAuraModAttackPower, 0, 4)}) == 16);
    CHECK(BuffBits({Aura(kAuraModPowerRegen, kPowerMana, 10)}) == 32);
    CHECK(BuffBits({Aura(kAuraModPowerRegen, 1, 10)}) == 0); // not mana: no bit
    // A debuff and another unit's buff are not the raid's.
    CHECK(BuffBits({Aura(kAuraModStat, kStatStamina, 6, 0, false)}) == 0);
    CHECK(BuffBits({Aura(kAuraModStat, kStatStamina, 6, 0, true, false)}) == 0);
    // Potion-family auras never count, whatever their effect.
    CHECK(BuffBits({Aura(kAuraModStat, kStatAll, kFamilyPotion)}) == 0);
    // Everything at once.
    CHECK(BuffBits({Aura(kAuraModStat, kStatStamina, 6), Aura(kAuraModStat, kStatIntellect, 3),
              Aura(kAuraModPowerRegen, kPowerMana, 10), Aura(kAuraModAttackPower, 0, 4)}) == (1 | 2 | 16 | 32));
}

static void TestConsumables()
{
    CHECK(ConsumableBits({Aura(kAuraModStat, kStatStamina, kFamilyPotion, kElixirFlaskMask)}) == 1); // flask
    CHECK(ConsumableBits({Aura(kAuraModStat, kStatStamina, kFamilyPotion, 1)}) == 2);               // elixir
    CHECK(ConsumableBits({Aura(kAuraModStat, kStatStamina, kFamilyPotion, kElixirWellFed)}) == 4);  // well fed
    CHECK(ConsumableBits({Aura(kAuraModStat, kStatStamina, kFamilyPotion, 0)}) == 2);               // potion family, no mask
    CHECK(ConsumableBits({Aura(kAuraModStat, kStatStamina, 6, 0)}) == 0);                           // a plain buff
    CHECK(ConsumableBits({Aura(kAuraModStat, kStatStamina, kFamilyPotion, kElixirFlaskMask)}) == 1); // not also an elixir
    // A debuff with a potion mask is not consumed.
    CHECK(ConsumableBits({Aura(kAuraModStat, kStatStamina, kFamilyPotion, 1, false)}) == 0);
    // Flask and food together.
    CHECK(ConsumableBits({Aura(kAuraModStat, kStatStamina, kFamilyPotion, kElixirFlaskMask),
              Aura(kAuraModStat, kStatStamina, kFamilyPotion, kElixirWellFed)}) == (1 | 4));
}

static void TestKeyCooldown()
{
    CHECK(!IsKeyCooldown(Cd(100, 100, 119999)));
    CHECK(IsKeyCooldown(Cd(100, 100, 120000)));
    CHECK(IsKeyCooldown(Cd(100, 100, 0, 120000)));       // the category recovery counts too
    CHECK(IsKeyCooldown(Cd(100, 100, 120000, 0)));
    CHECK(!IsKeyCooldown(Cd(100, 100, 300000, 0, true))); // a passive spell is never used
    CHECK(!IsKeyCooldown(Cd(100, 100, 300000, 0, false, kFamilyGeneric))); // no family: not a class cooldown
}

static void TestKeySpells()
{
    // Two ranks of one chain (the higher id wins), six more chains, one with a
    // category recovery: nine inputs, one per chain.
    std::vector<SpellCd> spells = {
        Cd(100, 100, 120000),
        Cd(101, 100, 130000),            // same chain: replaces 100
        Cd(200, 200, 500000),
        Cd(300, 300, 400000, 450000),    // effective 450 s via the category
        Cd(400, 400, 120000),
        Cd(500, 500, 300000),
        Cd(600, 600, 200000),
        Cd(700, 700, 180000),
        Cd(800, 800, 150000),
    };
    std::vector<uint32_t> const keys = KeySpells(spells);
    CHECK(keys.size() == 8);
    CHECK(keys.size() <= kMaxKeySpells);
    CHECK((keys == std::vector<uint32_t>{200, 300, 500, 600, 700, 800, 101, 400}));
    for (uint32_t spell : keys)
        CHECK(spell != 100); // the lower rank of chain 100 is not sent
    // Short, passive and generic spells are dropped before the cap.
    std::vector<SpellCd> mixed = {
        Cd(900, 900, 60000),             // under two minutes
        Cd(901, 901, 300000, 0, true),   // passive
        Cd(902, 902, 300000, 0, false, kFamilyGeneric),
        Cd(903, 903, 300000),
    };
    CHECK((KeySpells(mixed) == std::vector<uint32_t>{903}));
    // Ten key chains: the eight longest.
    std::vector<SpellCd> many;
    for (uint32_t i = 0; i < 10; ++i)
        many.push_back(Cd(1000 + i, 1000 + i, 120000 + i * 10000));
    std::vector<uint32_t> const capped = KeySpells(many);
    CHECK(capped.size() == 8);
    CHECK(capped.front() == 1009 && capped.back() == 1002);
}

int main()
{
    TestConstants();
    TestRaidBuffs();
    TestConsumables();
    TestKeyCooldown();
    TestKeySpells();
    std::printf("test_ready_check_policy: %d checks passed\n", checks);
    return EXIT_SUCCESS;
}
