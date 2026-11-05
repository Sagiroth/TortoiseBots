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

#include "../runtime/policy/pet/PetSpellRankPolicy.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

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
    CHECK(ShellShieldLadder()[0] == PetRankLadder::value_type(20, 26064));
    CHECK(PoisonSpitLadder()[0] == PetRankLadder::value_type(15, 46271));
    CHECK(PollenBurstLadder()[0] == PetRankLadder::value_type(52, 42051));
    CHECK(PackleaderLadder()[0] == PetRankLadder::value_type(20, 36532));
    CHECK(WebLadder()[0] == PetRankLadder::value_type(20, 36533));
    CHECK(GraceLadder()[0] == PetRankLadder::value_type(10, 46296));
    CHECK(NaturalArmorLadder().size() == 10);
    CHECK(NaturalArmorLadder()[9] == PetRankLadder::value_type(60, 24630));
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
    CheckAscending(NaturalArmorLadder(), "armor");
    CheckAscending(GreatStaminaLadder(), "stamina");
    CheckAscending(ArcaneResistanceLadder(), "arcane");
    CheckAscending(FireResistanceLadder(), "fire");
    CheckAscending(FrostResistanceLadder(), "frost");
    CheckAscending(NatureResistanceLadder(), "nature");
    CheckAscending(ShadowResistanceLadder(), "shadow");
}

static bool BuildHas(uint32_t family, PetRankLadder const& ladder)
{
    for (HunterPetBuildEntry const& entry : HunterPetBuild(family))
        if (entry.ladder == &ladder)
            return true;
    return false;
}

// Every family build fits the core's 4 active slots (Growl + 3), never
// pairs Bite with Claw, and the Turtle families get their own specials.
static void TestBuilds()
{
    for (uint32_t family = 0; family <= 40; ++family)
    {
        uint32_t actives = 0;
        for (HunterPetBuildEntry const& entry : HunterPetBuild(family))
            if (!entry.passive)
                ++actives;
        CHECK(actives + 1 <= kHunterPetActiveSlots);
        CHECK(!(BuildHas(family, BiteLadder()) && BuildHas(family, ClawLadder())));
        CHECK(!BuildHas(family, CowerLadder()));
    }
    CHECK(BuildHas(3, WebLadder()));            // spider
    CHECK(BuildHas(36, GraceLadder()));         // fox
    CHECK(BuildHas(35, PoisonSpitLadder()));    // serpent
    CHECK(BuildHas(39, PollenBurstLadder()));   // moth
    CHECK(BuildHas(37, FuriousHowlLadder()));   // plagued = wolf line
    CHECK(BuildHas(38, FuriousHowlLadder()));   // horse = wolf line
    CHECK(BuildHas(11, SavageRendLadder()));    // raptor
    CHECK(BuildHas(999, BiteLadder()));         // unknown family still bites
}

// SkillLineAbility training-point costs (client DBC) for the spells the
// plans below touch; anything else costs 0.
static int32_t Cost(uint32_t spellId)
{
    static std::pair<uint32_t, int32_t> const costs[] = {
        {17253, 1}, {17255, 4}, {17256, 7}, {17257, 10}, {17258, 13}, {17259, 17}, {17260, 21}, {17261, 25}, // Bite
        {16827, 1}, {16828, 4}, {16829, 7}, {16830, 10}, {16831, 13}, {16832, 17}, {3010, 21}, {3009, 25},   // Claw
        {24604, 10}, {24605, 15}, {24603, 20}, {24597, 25},  // Furious Howl
        {23099, 15}, {23109, 20}, {23110, 25},               // Dash
        {24450, 15}, {24452, 20}, {24453, 25},               // Prowl
        {4187, 5}, {4188, 10}, {4189, 15}, {4190, 25}, {4191, 50}, {4192, 75}, {4193, 100}, {4194, 125}, {5041, 150}, {5042, 185},
        {24545, 1}, {24549, 5}, {24550, 10}, {24551, 15}, {24552, 25}, {24553, 50}, {24554, 75}, {24555, 100}, {24629, 125}, {24630, 150},
        {24493, 5}, {24497, 15}, {24500, 45}, {24501, 90},
        {23992, 5}, {24439, 15}, {24444, 45}, {24445, 90},
        {24446, 5}, {24447, 15}, {24448, 45}, {24449, 90},
        {24492, 5}, {24502, 15}, {24503, 45}, {24504, 90},
        {24488, 5}, {24505, 15}, {24506, 45}, {24507, 90},
    };
    for (auto const& c : costs)
        if (c.first == spellId)
            return c.second;
    return 0;
}

static std::vector<PetWantedSpell> Plan(uint32_t family, uint32_t level, int32_t budget,
    std::vector<PetRankLadder const*> const& kept = {}, PetRankLadder const* heldLadder = nullptr, int heldRank = -1)
{
    std::vector<PetWantedSpell> plan;
    PlanHunterPetSpells(family, level, budget, kept, Cost,
        [&](PetRankLadder const& ladder) { return &ladder == heldLadder ? heldRank : -1; },
        [&](PetWantedSpell wanted) { plan.push_back(wanted); });
    return plan;
}

static bool Has(std::vector<PetWantedSpell> const& plan, uint32_t spellId)
{
    for (PetWantedSpell const& wanted : plan)
        if (wanted.spellId == spellId)
            return true;
    return false;
}

static int32_t Spent(std::vector<PetWantedSpell> const& plan)
{
    int32_t total = 0;
    for (PetWantedSpell const& wanted : plan)
        total += Cost(wanted.spellId);
    return total;
}

static uint32_t Actives(std::vector<PetWantedSpell> const& plan)
{
    uint32_t n = 0;
    for (PetWantedSpell const& wanted : plan)
        if (!wanted.passive)
            ++n;
    return n;
}

// The planner stays within the budget and the 4 slots, and spends the
// points in order: top-rank actives, stamina, armor, then resistances.
static void TestPlan()
{
    // Level-60 Best Friend wolf: 300 points, every point used.
    std::vector<PetWantedSpell> wolf = Plan(1, 60, HunterPetTrainingBudget(60, 6));
    CHECK(HunterPetTrainingBudget(60, 6) == 300);
    CHECK(Has(wolf, 14921));  // Growl 7
    CHECK(Has(wolf, 17261));  // Bite 8
    CHECK(Has(wolf, 24597));  // Furious Howl 4
    CHECK(Has(wolf, 23110));  // Dash 3
    CHECK(Has(wolf, 5042));   // Great Stamina 10
    CHECK(Has(wolf, 24552));  // Natural Armor 5 (25 of the 40 left)
    CHECK(!Has(wolf, 1742) && !Has(wolf, 16697)); // no Cower
    CHECK(Actives(wolf) == 4);
    CHECK(Spent(wolf) == 300);

    // Every family at every level: never over budget, never over 4 actives.
    for (uint32_t family = 0; family <= 40; ++family)
        for (uint32_t level = 1; level <= 60; ++level)
        {
            for (uint32_t loyalty = 1; loyalty <= 6; ++loyalty)
            {
                std::vector<PetWantedSpell> plan = Plan(family, level, HunterPetTrainingBudget(level, loyalty));
                CHECK(Spent(plan) <= HunterPetTrainingBudget(level, loyalty));
                CHECK(Actives(plan) <= kHunterPetActiveSlots);
            }
        }

    // Level 10: Bite 2 + Howl 1 + Stamina 1 + Armor 1 = 20 of 50; no rank
    // above the level (Dash needs 30, resistances 20).
    std::vector<PetWantedSpell> low = Plan(1, 10, 50);
    CHECK(Has(low, 17255) && Has(low, 24604) && Has(low, 4187) && Has(low, 24545));
    CHECK(!Has(low, 23099) && !Has(low, 24493));

    // A loyalty-1 pet has no points: Growl only, nothing that costs.
    std::vector<PetWantedSpell> rebel = Plan(1, 40, HunterPetTrainingBudget(40, 1));
    CHECK(Spent(rebel) == 0);
    CHECK(Has(rebel, 14919)); // Growl 5

    // An alt's own pick (Prowl on a wolf) is kept and ranked up, and the
    // build fills only the slots left: Growl + Prowl + Bite + Howl.
    std::vector<PetWantedSpell> alt = Plan(1, 60, 300, { &ProwlLadder() });
    CHECK(Has(alt, 24453));   // Prowl 3
    CHECK(Has(alt, 17261) && Has(alt, 24597));
    CHECK(!Has(alt, 23110));  // no slot left for Dash
    CHECK(Actives(alt) == 4);

    // A held rank is never planned lower, even when the points are short.
    std::vector<PetWantedSpell> held = Plan(1, 60, 10, {}, &BiteLadder(), 7);
    CHECK(Has(held, 17261));
}

// Warlock ladders resolve per demon.
static void TestWarlockLadders()
{
    CHECK(WarlockPetLadders(416).size() == 3);  // imp
    CHECK(WarlockPetLadders(417).size() == 3);  // felhunter
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
    // Spell Lock is a ladder too: a level-52 Felhunter wants rank 2 only, so
    // the check cannot stay true on rank 1 the core already replaced.
    {
        bool hasSL2 = false, hasSL1 = false;
        ForEachWarlockWantedSpell(417, 52, [&](PetWantedSpell wanted)
        {
            if (wanted.spellId == 19647)
                hasSL2 = true;
            if (wanted.spellId == 19244)
                hasSL1 = true;
        });
        CHECK(hasSL2);
        CHECK(!hasSL1);
    }
}

int main()
{
    std::printf("Starting pet spell rank policy tests...\n");
    TestRankLevels();
    std::printf("  [PASS] rank levels\n");
    TestAscending();
    std::printf("  [PASS] ladders ascend\n");
    TestBuilds();
    std::printf("  [PASS] family builds\n");
    TestPlan();
    std::printf("  [PASS] training-point plans\n");
    TestWarlockLadders();
    std::printf("  [PASS] warlock ladders\n");
    TestAutocastDefaults();
    std::printf("  [PASS] autocast defaults\n");
    TestSharedHelpers();
    std::printf("  [PASS] shared check/teach helpers\n");
    std::printf("All pet spell rank policy checks PASSED (%d assertions)!\n", checks);
    return 0;
}
