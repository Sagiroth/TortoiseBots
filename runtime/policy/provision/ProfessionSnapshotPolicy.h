#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Profession snapshot for `.bot professions` (TBM:PROF/RECIPE/RECIPE_MISSING
// lines). Pure filter + line-format rules over plain caller-supplied rows,
// unit-tested on their own (tools/test_profession_snapshot_policy.cpp). The
// handler in commands/Professions.cpp feeds this from the core stores; only
// plain values cross the boundary, so this header stays core-free.
//
// Colour steps mirror the core's craft skill-up roll
// (Player::UpdateCraftSkill in the core checkout, same shape as
// ShouldCraftSpellValue::SpellGivesSkillUp). Each step is the skill where
// that colour starts: orange at req_skill_value (learnable), yellow at
// min_value, green at the midpoint, grey at max_value, where the skill-up
// chance hits zero.

namespace TortoiseBots
{
namespace ProfessionSnapshotPolicy
{

// Skill ids and category ids are SharedDefines.h values, kept numeric so this
// header stays core-free.
inline constexpr uint32_t kCategoryProfession = 11;
inline constexpr uint32_t kCategorySecondary = 9;

inline constexpr uint32_t kSkillFishing = 356;
inline constexpr uint32_t kSkillCooking = 185;
inline constexpr uint32_t kSkillFirstAid = 129;

inline constexpr uint32_t kEffectCreateItem = 24;
inline constexpr uint32_t kEffectLearnSpell = 36;
inline constexpr uint32_t kEffectSkillStep = 44;
inline constexpr uint32_t kEffectEnchantItem = 53;
inline constexpr uint32_t kEffectEnchantItemTemporary = 54;
struct ProfessionRow
{
    uint32_t skillId = 0;
    uint32_t value = 0;
    uint32_t max = 0;
};

struct AbilityRow
{
    uint32_t skillId = 0;
    uint32_t spellId = 0;
    uint32_t reqSkillValue = 0;
    uint32_t minValue = 0;
    uint32_t maxValue = 0;
    // Caller-resolved spell facts (handler reads them off the SpellEntry).
    bool known = false;
    bool passive = false;
    bool isSkillStep = false;
    uint32_t effect0 = 0;
    uint32_t createdItemId = 0;
    bool createsItem = false;
    bool isEnchant = false;
    bool learnOnGetSkill = false;
};

inline bool IsProfessionCategory(uint32_t categoryId)
{
    return categoryId == kCategoryProfession || categoryId == kCategorySecondary;
}

// Known-recipe filter: the spell is on one of the bot's profession lines,
// is no rank/learn/profession-grant spell itself, and is a real recipe the
// panel can show (made item or enchant).
inline bool IsKnownRecipe(AbilityRow const& ability)
{
    if (!ability.known || ability.passive)
        return false;
    if (ability.learnOnGetSkill)
        return false;
    if (ability.isSkillStep)
        return false;
    if (ability.effect0 == kEffectLearnSpell || ability.effect0 == kEffectSkillStep)
        return false;
    // The Fishing line carries the Fishing cast itself (neither craft nor
    // enchant); only the line's crafts (lures, cooked fish via Cooking) show.
    if (ability.skillId == kSkillFishing && !ability.createsItem && !ability.isEnchant)
        return false;
    return ability.createsItem || ability.isEnchant;
}

inline void SkillSteps(uint32_t reqSkillValue, uint32_t minValue, uint32_t maxValue,
    uint32_t& orange, uint32_t& yellow, uint32_t& green, uint32_t& grey)
{
    orange = reqSkillValue;
    yellow = minValue;
    green = (minValue + maxValue) / 2;
    grey = maxValue;
}

inline std::string FormatProfession(ProfessionRow const& row)
{
    char line[64];
    std::snprintf(line, sizeof(line), "TBM:PROF|%u|%u|%u",
        row.skillId, row.value, row.max);
    return line;
}

inline std::string FormatRecipe(AbilityRow const& ability, std::string const& order)
{
    uint32_t orange = 0, yellow = 0, green = 0, grey = 0;
    SkillSteps(ability.reqSkillValue, ability.minValue, ability.maxValue, orange, yellow, green, grey);
    char line[112];
    std::snprintf(line, sizeof(line), "TBM:RECIPE|%u|%u|%u|%u|%u|%u|%u|%s",
        ability.skillId, ability.spellId, ability.createdItemId,
        orange, yellow, green, grey, order.c_str());
    return line;
}

struct MissingRow
{
    uint32_t skillId = 0;
    uint32_t spellId = 0;
    uint32_t createdItemId = 0;
    uint32_t reqSkill = 0;
    uint32_t recipeItemId = 0;
};

// Missing-recipe filter: a profession recipe the bot does not know yet, on a
// profession it holds, learnable at its current rank (required skill at or
// below the rank cap), and obtainable (trainer-taught or sold as a recipe
// item). Recipes with neither source never reach a bot, so they stay hidden.
inline bool IsMissingRecipe(AbilityRow const& ability, uint32_t skillMax,
    bool taughtByTrainer, uint32_t recipeItemId)
{
    if (ability.known)
        return false;
    if (ability.passive || ability.learnOnGetSkill || ability.isSkillStep)
        return false;
    if (ability.effect0 == kEffectLearnSpell || ability.effect0 == kEffectSkillStep)
        return false;
    if (ability.skillId == kSkillFishing && !ability.createsItem && !ability.isEnchant)
        return false;
    if (!ability.createsItem && !ability.isEnchant)
        return false;
    if (ability.reqSkillValue > skillMax)
        return false;
    return taughtByTrainer || recipeItemId != 0;
}

inline std::string FormatMissing(MissingRow const& row)
{
    char line[96];
    std::snprintf(line, sizeof(line), "TBM:RECIPE_MISSING|%u|%u|%u|%u|%u",
        row.skillId, row.spellId, row.createdItemId, row.reqSkill, row.recipeItemId);
    return line;
}

inline std::string FormatEnd(std::string const& bot, size_t recipeCount, size_t missingCount)
{
    char line[128];
    std::snprintf(line, sizeof(line), "TBM:PROF_END|%s|%u|%u",
        bot.c_str(), uint32_t(recipeCount), uint32_t(missingCount));
    return line;
}

inline std::string FormatBegin(std::string const& bot)
{
    return "TBM:PROF_BEGIN|" + bot;
}

} // namespace ProfessionSnapshotPolicy
} // namespace TortoiseBots
