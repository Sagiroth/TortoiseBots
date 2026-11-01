// Standalone regression test for the profession specialization rule:
// pool bots at the skill gate earn the quest-chain specialization spells
// directly (bots never run the exclusive quest chains). Pins the pure pick
// rule production calls from runtime/ProfessionSpecializationPolicy.h:
//
// PlayerbotFactory::EnsureSpecializations (mode gate + guid-parity pick),
// PlayerbotFactory::LearnSpecializationRecipes (gated by the spec spells).
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_profession_specialization_policy.cpp -o /tmp/test_profession_spec
//   /tmp/test_profession_spec

#include "../runtime/ProfessionSpecializationPolicy.h"

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

// The skill gates mirror the quest RequiredSkillValue rows.
static void TestThresholdValues()
{
    CHECK(SPEC_REQ_ENGINEERING == 200);
    CHECK(SPEC_REQ_BLACKSMITHING == 200);
    CHECK(SPEC_REQ_LEATHERWORKING == 225);
    CHECK(QualifiesForSpecialization(199, SPEC_REQ_ENGINEERING) == false);
    CHECK(QualifiesForSpecialization(200, SPEC_REQ_ENGINEERING) == true);
    CHECK(QualifiesForSpecialization(300, SPEC_REQ_ENGINEERING) == true);
    CHECK(QualifiesForSpecialization(224, SPEC_REQ_LEATHERWORKING) == false);
    CHECK(QualifiesForSpecialization(225, SPEC_REQ_LEATHERWORKING) == true);
}

// The table covers every vanilla 1.12 specialization branch that exists in
// this core's data: engineering (Goblin/Gnomish), blacksmithing
// (Armorsmith/Weaponsmith + Axe/Hammer/Swordsmith), leatherworking
// (Dragonscale/Elemental/Tribal). Spell ids verified against spell_template.
static void TestGroupContents()
{
    CHECK(kSpecializationGroupCount == 5);
    CHECK(kSpecializationGroups[0].skillId == SPEC_SKILL_ENGINEERING);
    CHECK(kSpecializationGroups[0].spellCount == 2);
    CHECK(kSpecializationGroups[0].spells[0] == SPELL_GOBLIN_ENGINEER);
    CHECK(kSpecializationGroups[0].spells[1] == SPELL_GNOMISH_ENGINEER);
    CHECK(kSpecializationGroups[1].skillId == SPEC_SKILL_BLACKSMITHING);
    CHECK(kSpecializationGroups[1].spellCount == 2);
    CHECK(kSpecializationGroups[1].spells[0] == SPELL_ARMORSMITH);
    CHECK(kSpecializationGroups[1].spells[1] == SPELL_WEAPONSMITH);
    CHECK(kSpecializationGroups[2].skillId == SPEC_SKILL_BLACKSMITHING);
    CHECK(kSpecializationGroups[2].spellCount == 3);
    CHECK(kSpecializationGroups[2].spells[0] == SPELL_MASTER_SWORDSMITH);
    CHECK(kSpecializationGroups[2].spells[1] == SPELL_MASTER_HAMMERSMITH);
    CHECK(kSpecializationGroups[2].spells[2] == SPELL_MASTER_AXESMITH);
    CHECK(kSpecializationGroups[3].skillId == SPEC_SKILL_LEATHERWORKING);
    CHECK(kSpecializationGroups[3].spellCount == 3);
    CHECK(kSpecializationGroups[3].spells[0] == SPELL_DRAGONSCALE_LW);
    CHECK(kSpecializationGroups[3].spells[1] == SPELL_ELEMENTAL_LW);
    CHECK(kSpecializationGroups[3].spells[2] == SPELL_TRIBAL_LW);
    // Turtle-custom Jewelcrafting (skill 755): Gemology / Goldsmith, gated at
    // 225 like the Mastering quests (41275-41278). No tailoring, alchemy or
    // enchanting specialization spells exist in the data.
    CHECK(kSpecializationGroups[4].skillId == SPEC_SKILL_JEWELCRAFTING);
    CHECK(kSpecializationGroups[4].requiredValue == SPEC_REQ_JEWELCRAFTING);
    CHECK(SPEC_REQ_JEWELCRAFTING == 225);
    CHECK(kSpecializationGroups[4].spellCount == 2);
    CHECK(kSpecializationGroups[4].spells[0] == SPELL_GEMOLOGY);
    CHECK(kSpecializationGroups[4].spells[1] == SPELL_GOLDSMITH);
    CHECK(SPELL_GEMOLOGY == 57551);
    CHECK(SPELL_GOLDSMITH == 57553);
    // Weapon masters extend Weaponsmith only.
    CHECK(kSpecializationGroups[2].requiresSpell == SPELL_WEAPONSMITH);
    CHECK(kSpecializationGroups[0].requiresSpell == 0);
    CHECK(kSpecializationGroups[1].requiresSpell == 0);
    CHECK(kSpecializationGroups[3].requiresSpell == 0);
    CHECK(kSpecializationGroups[4].requiresSpell == 0);
}

// Mode 1 pick is guid parity: deterministic per bot, never flips.
static void TestOnePerBotPickIsStable()
{
    CHECK(PickSpecializationIndex(0, 2) == 0);
    CHECK(PickSpecializationIndex(1, 2) == 1);
    CHECK(PickSpecializationIndex(2, 2) == 0);
    CHECK(PickSpecializationIndex(1001, 2) == 1);
    CHECK(PickSpecializationIndex(1000, 2) == 0);
    // Same guid always lands the same branch, whatever the branch count.
    for (uint32_t guid = 0; guid < 64; ++guid)
    {
        CHECK(PickSpecializationIndex(guid, 2) == guid % 2);
        CHECK(PickSpecializationIndex(guid, 3) == guid % 3);
    }
    CHECK(PickSpecializationIndex(7, 0) == 0);
}

// Mode enum matches the config values.
static void TestModeValues()
{
    CHECK(int(SpecializationMode::Off) == 0);
    CHECK(int(SpecializationMode::OnePerBot) == 1);
    CHECK(int(SpecializationMode::All) == 2);
}

int main()
{
    TestThresholdValues();
    TestGroupContents();
    TestOnePerBotPickIsStable();
    TestModeValues();
    std::printf("profession specialization policy: %d checks passed\n", checks);
    return 0;
}
