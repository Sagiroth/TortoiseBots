#pragma once

#include "runtime/GuildOrderEnchantPolicy.h"

#include <cstdint>
#include <string>
#include <vector>
namespace ai
{

struct GuildOrderEnchantMaterial
{
    uint32_t itemId = 0;
    uint32_t count = 0;
};

// Recipe item teaching the spell: first lowest-entry recipe-class item whose
// use spell learns it. Trained-from-trainer rows have no item and stay 0.
// Shared with the Professions missing-recipe line.
uint32_t GuildOrderRecipeItemFor(uint32_t spellId);

// Lowest required skill among trade-skill trainer rows teaching the spell.
// Shared with the Professions missing-recipe line.
bool GuildOrderTaughtByAnyTrainer(uint32_t spellId, uint32_t& reqSkill);

// Required skill to learn the spell, from the same sources as the Professions
// missing-recipe line: the lowest trainer requirement teaching it, and the
// recipe item's RequiredSkillRank; the lower wins when both exist, 0 when
// neither names one. recipeItem is GuildOrderRecipeItemFor(spellId) (0 when
// trained from a trainer).
uint32_t GuildOrderEnchantReqSkill(uint32_t spellId, uint32_t recipeItem);

// Read-only catalogue of permanent item enchants, built once at startup from
// the server's own spell and skill-line data (see runtime/GuildOrderEnchantPolicy.h
// for the slot and matching rules). W3 wires no command or TBM output; the
// search tasks read this catalogue.
void BuildGuildOrderEnchantIndex();
bool GuildOrderEnchantsReady();

// All catalogue entries, sorted by required skill ascending then name.
std::vector<TortoiseBots::GuildOrders::EnchantEntry> GuildOrderEnchants();

// One entry's materials: reagents in spell order, then the recipe tools
// (runed rods) the bot always carries.
std::vector<GuildOrderEnchantMaterial> GuildOrderEnchantReagents(uint32_t spellId);
std::vector<uint32_t> GuildOrderEnchantTools(uint32_t spellId);

} // namespace ai
