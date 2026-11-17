#pragma once

#include <cstdint>

// Pure policy for skinning-loot telemetry (issue #403): a skinned corpse must
// log its GatherLoot row under SKILL_SKINNING so the dashboard skinning
// counter works.
//
// Core contract (read-only core, tortoise-wow):
// - src/game/Spells/SpellEffects.cpp EffectSkinning opens the window with
//   SendLoot(guid, LOOT_SKINNING).
// - src/game/Objects/Player.cpp SendLoot stores that type server-side
//   (loot->loot_type = loot_type) and THEN rewrites it for the 1.12 client,
//   which has no skinning window (LOOT_SKINNING -> LOOT_PICKPOCKETING).
// So the wire loot_type in SMSG_LOOT_RESPONSE is LOOT_PICKPOCKETING for both
// a skin and a real pickpocket, while the server-side Loot keeps LOOT_SKINNING.
// StoreLootAction must key off the server-side type (LootAccess::lootType()),
// never the wire byte.
// - src/game/Spells/SpellEffects.cpp EffectOpenLock (Herb Gathering, Mining,
//   any opening spell) ALSO calls SendLoot(guid, LOOT_SKINNING), so an herb or
//   ore window carries server type 6 too. A game-object window therefore takes
//   its skill from the object's lock, never from the loot type.
// The constants below mirror the core enums so this rule
// is unit-testable without core headers; LootAction.cpp static_asserts them
// against LOOT_SKINNING / SKILL_SKINNING so drift fails the build.

namespace ai
{
    uint32_t const kSkinningLootType = 6;   // LOOT_SKINNING (LootMgr.h)
    uint32_t const kPickpocketLootType = 2; // LOOT_PICKPOCKETING (LootMgr.h)
    uint32_t const kHerbalismSkillId = 182; // SKILL_HERBALISM (SharedDefines.h)
    uint32_t const kMiningSkillId = 186;    // SKILL_MINING (SharedDefines.h)
    uint32_t const kSkinningSkillId = 393;  // SKILL_SKINNING (SharedDefines.h)
    uint32_t const kNoGatherSkill = 0;      // SKILL_NONE

    // Gathering skill for one stored loot window. internalLootType is the
    // server-side Loot::loot_type, NOT the wire byte. A game-object window
    // takes its skill from the object's lock (nodeGatherSkill, SKILL_NONE when
    // the lock asks for no skill) whatever its type: an opened node arrives as
    // LOOT_SKINNING, and checking the type first logged every herb and ore as
    // a skin. Only a creature window with the skinning type is a skin.
    inline uint32_t DecideGatherSkillForLoot(uint32_t internalLootType, bool targetIsGameObject, uint32_t nodeGatherSkill)
    {
        if (targetIsGameObject)
            return nodeGatherSkill;
        return internalLootType == kSkinningLootType ? kSkinningSkillId : kNoGatherSkill;
    }

    // True when the server-side loot type marks a skinning window. Used to
    // bypass the ordinary IsLootAllowed filter for skins, exactly as the old
    // (dead) wire-type check intended.
    inline bool IsSkinningLoot(uint32_t internalLootType)
    {
        return internalLootType == kSkinningLootType;
    }

    // True when a loot window may skip the bot's IsLootAllowed filter.
    // Only a skinned creature or a Herbalism/Mining node may bypass it;
    // chests, lockpicked boxes and quest objects also open with LOOT_SKINNING
    // via EffectOpenLock and must go through the filter like a corpse.
    inline bool ShouldSkipLootFilter(uint32_t internalLootType, bool targetIsGameObject, uint32_t gatherSkill)
    {
        if (!targetIsGameObject)
            return internalLootType == kSkinningLootType;
        return gatherSkill == kHerbalismSkillId || gatherSkill == kMiningSkillId;
    }
}
