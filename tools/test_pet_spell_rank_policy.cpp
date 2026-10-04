// Standalone regression test for the pet spell-rank ladders
// (runtime/PetSpellRankPolicy.h): rank levels must match the server data
// (tw_world.spell_template baseLevel), every ladder must ascend, the shared
// check/teach helpers must name only top ranks (lower ranks are dropped by
// Pet::AddSpell, so naming them would pin isUseful true forever), and the
// teach-time autocast default must agree with the runtime sweep except for
// the two summon-suicide/manual-CC spells the sweep would leave on
// (Sacrifice, Seduction).
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_pet_spell_rank_policy.cpp -o /tmp/test_pet_ranks
//   /tmp/test_pet_spell_rank_policy

#include "../runtime/PetSpellRankPolicy.h"

#include <cstdio>
#include <cstdlib>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static void CheckAscending(PetRankLadder const& ladder, char const* name)
{
    CHECK(!ladder.empty());
    for (size_t i = 1; i < ladder.size(); ++i)
    {
        if (!(ladder[i].first > ladder[i - 1].first))
        {
            std::fprintf(stderr, "FAIL %s: level %u after %u\n", name, ladder[i].first,
                ladder[i - 1].first);
            std::exit(1);
        }
        ++checks;
    }
}

// Spot-check rank levels against tw_world.spell_template baseLevel.
static void TestRankLevels()
{
    CHECK(BiteLadder()[1] == PetRankLadder::value_type(8, 17255));
    CHECK(BiteLadder()[2] == PetRankLadder::value_type(16, 17256));
    CHECK(ClawLadder()[2] == PetRankLadder::value_type(16, 16829));
    CHECK(ClawLadder()[5] == PetRankLadder::value_type(40, 16832));
    CHECK(ClawLadder()[6] == PetRankLadder::value_type(48, 3010));
    CHECK(GrowlLadder()[1] == PetRankLadder::value_type(10, 14916));
    CHECK(DiveLadder()[1] == PetRankLadder::value_type(40, 23147));
    CHECK(DiveLadder()[2] == PetRankLadder::value_type(50, 23148));
    CHECK(ScreechLadder()[2] == PetRankLadder::value_type(48, 24578));
    CHECK(ScreechLadder()[3] == PetRankLadder::value_type(56, 24579));
    CHECK(FuriousHowlLadder()[3] == PetRankLadder::value_type(56, 24597));
    CHECK(ChargeLadder()[1] == PetRankLadder::value_type(12, 26177));
    CHECK(ThunderstompLadder()[3] == PetRankLadder::value_type(56, 51156));
    CHECK(ScorpidPoisonLadder()[1] == PetRankLadder::value_type(24, 24583));
    CHECK(LightningBreathLadder()[1] == PetRankLadder::value_type(12, 25008));
    CHECK(ShellShieldSpell() == 26064);
    CHECK(PoisonSpitLadder()[0] == PetRankLadder::value_type(15, 46271));
    CHECK(PollenBurstSpell() == 42051);
    CHECK(PackleaderSpell() == 36532);
}

// Every ladder ascends (teaching in order never downgrades).
static void TestAscending()
{
    CheckAscending(BiteLadder(), "bite");
    CheckAscending(ClawLadder(), "claw");
    CheckAscending(CowerLadder(), "cower");
    CheckAscending(GrowlLadder(), "growl");
    CheckAscending(DiveLadder(), "dive");
    CheckAscending(DashLadder(), "dash");
    CheckAscending(ScreechLadder(), "screech");
    CheckAscending(ProwlLadder(), "prowl");
    CheckAscending(FuriousHowlLadder(), "howl");
    CheckAscending(ChargeLadder(), "charge");
    CheckAscending(ScorpidPoisonLadder(), "scorpid");
    CheckAscending(LightningBreathLadder(), "breath");
    CheckAscending(ThunderstompLadder(), "thunderstomp");
    CheckAscending(BubbleBarrierLadder(), "bubble");
    CheckAscending(DeathRollLadder(), "deathroll");
    CheckAscending(SavageRendLadder(), "rend");
    CheckAscending(PoisonSpitLadder(), "poisonspit");
}

// Turtle custom families resolve to their own abilities, not Growl-only.
static void TestCustomFamilies()
{
    bool serpent = false, fox = false, moth = false;
    for (PetRankLadder const* ladder : HunterPetLadders(35))
        if (ladder == &PoisonSpitLadder())
            serpent = true;
    for (PetRankLadder const* ladder : HunterPetLadders(36))
        if (ladder == &DashLadder())
            fox = true;
    for (PetRankLadder const* ladder : HunterPetLadders(39))
        if (ladder == &DiveLadder())
            moth = true;
    CHECK(serpent);
    CHECK(fox);
    CHECK(moth);
    CHECK(HunterPetSingleSpells(39).size() == 1); // Pollen Burst
    // Unknown families still get damage, not silence.
    CHECK(HunterPetLadders(999).size() == 2);
}

// Warlock ladders resolve per demon.
static void TestWarlockLadders()
{
    CHECK(WarlockPetLadders(416).size() == 3);  // imp
    CHECK(WarlockPetLadders(417).size() == 2);  // felhunter
    CHECK(WarlockPetLadders(1860).size() == 4); // voidwalker
    CHECK(WarlockPetLadders(1863).size() == 2); // succubus
    CHECK(WarlockPetLadders(1234).empty());
    CHECK(WarlockPetSingleSpells(416).size() == 1); // Phase Shift
}

// Teach-time defaults: combat spells on; deliberate-cast + summon-suicide off.
static void TestAutocastDefaults()
{
    CHECK(ShouldPetSpellAutocastDefault(14916));  // Growl
    CHECK(ShouldPetSpellAutocastDefault(3716));   // Torment
    CHECK(ShouldPetSpellAutocastDefault(3110));  // Firebolt
    CHECK(ShouldPetSpellAutocastDefault(17255)); // Bite
    CHECK(!ShouldPetSpellAutocastDefault(1742)); // Cower
    CHECK(!ShouldPetSpellAutocastDefault(24450)); // Prowl
    CHECK(!ShouldPetSpellAutocastDefault(19244)); // Spell Lock
    CHECK(!ShouldPetSpellAutocastDefault(19505)); // Devour Magic
    CHECK(!ShouldPetSpellAutocastDefault(7812)); // Sacrifice (kills the pet)
    CHECK(!ShouldPetSpellAutocastDefault(19443)); // Sacrifice top rank
    CHECK(!ShouldPetSpellAutocastDefault(6358)); // Seduction (manual CC)
}

// The check and the teach path share one table: only the TOP rank each
// ladder allows may be named (lower ranks are dropped by AddSpell), and the
// same wanted-id helpers must drive both.
static void TestSharedHelpers()
{
    // Top rank only: a L20 wolf knows Torment-equivalent Growl-3, not ranks 1-2.
    CHECK(TopRankAtLevel(GrowlLadder(), 20) == 14917);
    CHECK(TopRankAtLevel(BiteLadder(), 20) == 17256);
    CHECK(TopRankAtLevel(BiteLadder(), 1) == 17253);
    // Wolf L20 wanted set: Bite-3 + Cower-2 + Growl-3 + armor-3 + stamina-3,
    // never the dropped lower ranks.
    {
        bool hasBite3 = false, hasGrowl3 = false, hasBite1 = false, hasGrowl1 = false;
        ForEachHunterWantedSpell(1, 20, [&](PetWantedSpell wanted)
        {
            if (wanted.spellId == 17256)
                hasBite3 = true;
            if (wanted.spellId == 14917)
                hasGrowl3 = true;
            if (wanted.spellId == 17253)
                hasBite1 = true;
            if (wanted.spellId == 2649)
                hasGrowl1 = true;
        });
        CHECK(hasBite3);
        CHECK(hasGrowl3);
        CHECK(!hasBite1);
        CHECK(!hasGrowl1);
    }
    // Voidwalker L20: Torment-2 (7809) only, never Torment-1 (3716).
    {
        bool hasT2 = false, hasT1 = false;
        ForEachWarlockWantedSpell(1860, 20, [&](PetWantedSpell wanted)
        {
            if (wanted.spellId == 7809)
                hasT2 = true;
            if (wanted.spellId == 3716)
                hasT1 = true;
        });
        CHECK(hasT2);
        CHECK(!hasT1);
    }
    // Passives ride the same helper (no second table to drift).
    CHECK(TopRankAtLevel(NaturalArmorLadder(), 20) == 24550);
    CHECK(TopRankAtLevel(GreatStaminaLadder(), 20) == 4189);
}

int main()
{
    std::printf("Starting pet spell rank policy tests...\n");
    TestRankLevels();
    std::printf("  [PASS] rank levels\n");
    TestAscending();
    std::printf("  [PASS] ladders ascend\n");
    TestCustomFamilies();
    std::printf("  [PASS] custom families\n");
    TestWarlockLadders();
    std::printf("  [PASS] warlock ladders\n");
    TestAutocastDefaults();
    std::printf("  [PASS] autocast defaults\n");
    TestSharedHelpers();
    std::printf("  [PASS] shared check/teach helpers\n");
    std::printf("All pet spell rank policy checks PASSED (%d assertions)!\n", checks);
    return 0;
}
