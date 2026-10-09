#pragma once

#include <cstdint>

// Auto-tools kit policy (owner request): bots auto-carry the items and skills
// their behaviours need. Pure decision rules over durable facts (class id,
// level, owned counts) so they stay unit tested on their own
// (tools/test_auto_tools_kit_policy.cpp); the mint itself lives in the
// per-tick cheat block (PlayerbotAI.cpp) and the seed paths
// (PlayerbotFactory).
//
// Ids are server data from tw_world, pinned here (not guessed):
// 5060 Thieves' Tools (req 15, Pick Lock 1804 totem1=5060), 19183 Hourglass
// Sand (stack 200, casts 23645), 17333 Aqual Quintessence / 22754 Eternal
// Quintessence (MC rune douse), 13457 Greater Fire Protection Potion
// (req 48, MC/Ragnaros), 5140 Flash Powder (Vanish 1856/1857 reagent1),
// 5530 Blinding Powder (Blind reagent), 6265 Soul Shard (warlock keep 5).
// Map ids: 409 Molten Core, 469 Blackwing Lair (map_template).

namespace TortoiseBots
{

inline constexpr uint32_t THIEVES_TOOLS_ITEM_ID = 5060;
inline constexpr uint32_t THIEVES_TOOLS_REQ_LEVEL = 15;
inline constexpr uint32_t PICK_LOCK_SPELL_ID = 1804;
inline constexpr uint32_t LOCKPICK_SKILL_ID = 633;
inline constexpr uint32_t LOCKPICK_SKILL_CAP = 300;

inline constexpr uint32_t HOURGLASS_SAND_ITEM_ID = 19183;
inline constexpr uint32_t BRONZE_AFFLICTION_SPELL_ID = 23170;
inline constexpr uint32_t BWL_MAP_ID = 469;

inline constexpr uint32_t AQUAL_QUINTESSENCE_ITEM_ID = 17333;
inline constexpr uint32_t ETERNAL_QUINTESSENCE_ITEM_ID = 22754;
inline constexpr uint32_t MC_MAP_ID = 409;

inline constexpr uint32_t ROGUE_CLASS_ID = 4;
inline constexpr uint32_t CLASS_MASK_ROGUE = 8;

// Lockpicking is always max-for-level: 5 per level, capped at 300. Below the
// Thieves' Tools level the skill is moot (bots cannot hold the tools), so
// the tools gate mirrors the item's own req level.
inline uint32_t LockpickSkillForLevel(uint32_t level)
{
    uint32_t skill = level * 5;
    return skill > LOCKPICK_SKILL_CAP ? LOCKPICK_SKILL_CAP : skill;
}

inline bool RogueWantsLockpickSkill(uint32_t classId, uint32_t level)
{
    return classId == ROGUE_CLASS_ID && level >= THIEVES_TOOLS_REQ_LEVEL;
}

inline bool RogueWantsThievesTools(uint32_t classId, uint32_t level)
{
    return classId == ROGUE_CLASS_ID && level >= THIEVES_TOOLS_REQ_LEVEL;
}

// Hourglass Sand: mint one only while the bot itself carries the Bronze
// affliction (BWL map or Chromaggus-fight gate applied by the caller) and
// holds none. One is enough: the item stacks to 200 and the cleanse uses
// one charge-equivalent per cast.
inline bool ShouldEnsureHourglassSand(bool selfHasBronze, uint32_t sandCount)
{
    return selfHasBronze && sandCount == 0;
}

// MC rune douse: keep one Quintessence while inside Molten Core so the rune
// actions never silently skip. Eternal first (exalted upgrade), Aqual as
// the fallback the strategy already prefers in that order.
inline bool ShouldEnsureQuintessence(uint32_t mapId, uint32_t eternalCount, uint32_t aqualCount)
{
    return mapId == MC_MAP_ID && eternalCount == 0 && aqualCount == 0;
}

inline uint32_t QuintessenceEnsureId(uint32_t botLevel)
{
    // Eternal is the exalted-Hydraxian upgrade; bracketing by level keeps
    // lowbies on the quest item they can actually hold.
    return botLevel >= 60 ? ETERNAL_QUINTESSENCE_ITEM_ID : AQUAL_QUINTESSENCE_ITEM_ID;
}

}  // namespace TortoiseBots
