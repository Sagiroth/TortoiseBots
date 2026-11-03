// Standalone test for the profession snapshot policy
// (runtime/policy/provision/ProfessionSnapshotPolicy.h): the known/missing
// recipe filters and the exact TBM:PROF/RECIPE/RECIPE_MISSING wire format.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_profession_snapshot_policy.cpp -o /tmp/test_profession_snapshot_policy
//   /tmp/test_profession_snapshot_policy

#include "../runtime/policy/provision/ProfessionSnapshotPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace TortoiseBots::ProfessionSnapshotPolicy;

static AbilityRow KnownRecipe(uint32_t skillId = 164, uint32_t spellId = 2661)
{
    AbilityRow row;
    row.skillId = skillId;
    row.spellId = spellId;
    row.known = true;
    row.effect0 = kEffectCreateItem;
    row.createdItemId = 2856;
    row.createsItem = true;
    row.minValue = 75;
    row.maxValue = 105;
    return row;
}

int main()
{
    // Profession lines only: primary (11) and secondary (9) categories pass.
    CHECK(IsProfessionCategory(11));
    CHECK(IsProfessionCategory(9));
    CHECK(!IsProfessionCategory(7));
    CHECK(!IsProfessionCategory(6));
    CHECK(!IsProfessionCategory(0));

    // A known Blacksmithing recipe passes the filter.
    CHECK(IsKnownRecipe(KnownRecipe()));

    // Class skills never reach this filter (no profession ability row). The
    // Fishing cast itself is neither craft nor enchant, so it falls out.
    AbilityRow cast = KnownRecipe(kSkillFishing, 13181);
    cast.createsItem = false;
    CHECK(!IsKnownRecipe(cast));

    // Smelting stays: Smelt Copper is a real Mining recipe with skill steps.
    AbilityRow smelt = KnownRecipe(186, 2656);
    smelt.minValue = 1;
    smelt.maxValue = 55;
    CHECK(IsKnownRecipe(smelt));

    // Rank/learn spells excluded: the Apprentice teaching spell (SKILL_STEP),
    // a LEARN_SPELL row, and skill-granted rows (learnOnGetSkill).
    AbilityRow rank = KnownRecipe(164, 9787);
    rank.isSkillStep = true;
    CHECK(!IsKnownRecipe(rank));
    AbilityRow learn = KnownRecipe();
    learn.effect0 = kEffectLearnSpell;
    CHECK(!IsKnownRecipe(learn));
    AbilityRow granted = KnownRecipe();
    granted.learnOnGetSkill = true;
    CHECK(!IsKnownRecipe(granted));

    // Passives and unknown spells excluded.
    AbilityRow passive = KnownRecipe();
    passive.passive = true;
    CHECK(!IsKnownRecipe(passive));
    AbilityRow unknown = KnownRecipe();
    unknown.known = false;
    CHECK(!IsKnownRecipe(unknown));

    // Enchants pass with item id 0 (no made item).
    AbilityRow enchant = KnownRecipe(333, 7418);
    enchant.createdItemId = 0;
    enchant.createsItem = false;
    enchant.isEnchant = true;
    CHECK(IsKnownRecipe(enchant));

    // A fishing-line row with neither craft nor enchant is not a recipe.
    AbilityRow survey = KnownRecipe(kSkillFishing, 7732);
    survey.createdItemId = 0;
    survey.createsItem = false;
    CHECK(!IsKnownRecipe(survey));

    // Exact line format.
    ProfessionRow prof{164, 237, 300};
    CHECK(FormatProfession(prof) == "TBM:PROF|164|237|300");
    CHECK(FormatBegin("Dudette") == "TBM:PROF_BEGIN|Dudette");
    AbilityRow recipe = KnownRecipe();
    CHECK(FormatRecipe(recipe) == "TBM:RECIPE|164|2661|2856|75|90|105|105");
    MissingRow missing{164, 2663, 2859, 100, 2881};
    CHECK(FormatMissing(missing) == "TBM:RECIPE_MISSING|164|2663|2859|100|2881");
    CHECK(FormatEnd("Dudette", 3, 2) == "TBM:PROF_END|Dudette|3|2");

    // Missing filter: unknown, learnable at the current rank, trainer-taught.
    AbilityRow missingRecipe = KnownRecipe();
    missingRecipe.known = false;
    missingRecipe.reqSkillValue = 100;
    CHECK(IsMissingRecipe(missingRecipe, 150, true, 0));

    // Known excluded.
    AbilityRow known = KnownRecipe();
    known.reqSkillValue = 100;
    CHECK(!IsMissingRecipe(known, 150, true, 0));

    // Required skill above the rank cap excluded.
    AbilityRow tooHard = KnownRecipe();
    tooHard.known = false;
    tooHard.reqSkillValue = 200;
    CHECK(!IsMissingRecipe(tooHard, 150, true, 0));

    // Unobtainable excluded: neither trainer nor recipe item.
    AbilityRow noSource = KnownRecipe();
    noSource.known = false;
    noSource.reqSkillValue = 100;
    CHECK(!IsMissingRecipe(noSource, 150, false, 0));

    // Recipe item alone is enough (trainer-only quest recipes have no item).
    AbilityRow itemOnly = KnownRecipe();
    itemOnly.known = false;
    itemOnly.reqSkillValue = 100;
    CHECK(IsMissingRecipe(itemOnly, 150, false, 2881));

    // Same exclusions as known: rank spells, gathering with no output, passives.
    AbilityRow missingRank = KnownRecipe(164, 9787);
    missingRank.known = false;
    missingRank.isSkillStep = true;
    missingRank.reqSkillValue = 1;
    CHECK(!IsMissingRecipe(missingRank, 300, true, 0));
    AbilityRow missingGather = KnownRecipe(356, 13181);
    missingGather.known = false;
    missingGather.createsItem = false;
    missingGather.reqSkillValue = 1;
    CHECK(!IsMissingRecipe(missingGather, 300, true, 0));
    AbilityRow missingPassive = KnownRecipe();
    missingPassive.known = false;
    missingPassive.passive = true;
    missingPassive.reqSkillValue = 1;
    CHECK(!IsMissingRecipe(missingPassive, 300, true, 0));

    std::cout << "profession snapshot policy ok\n";
    return 0;
}
