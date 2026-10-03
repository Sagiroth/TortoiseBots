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
// never the wire byte. The constants below mirror the core enums so this rule
// is unit-testable without core headers; LootAction.cpp static_asserts them
// against LOOT_SKINNING / SKILL_SKINNING so drift fails the build.

namespace ai
{
    uint32_t const kSkinningLootType = 6;   // LOOT_SKINNING (LootMgr.h)
    uint32_t const kPickpocketLootType = 2; // LOOT_PICKPOCKETING (LootMgr.h)
    uint32_t const kSkinningSkillId = 393;  // SKILL_SKINNING (SharedDefines.h)
    uint32_t const kNoGatherSkill = 0;      // SKILL_NONE

    // Gathering skill for one stored loot window. internalLootType is the
    // server-side Loot::loot_type, NOT the wire byte. A game-object window is
    // ordinary loot whose skill comes from the object's lock (nodeGatherSkill,
    // SKILL_NONE when the lock asks for no skill).
    inline uint32_t DecideGatherSkillForLoot(uint32_t internalLootType, bool targetIsGameObject, uint32_t nodeGatherSkill)
    {
        if (internalLootType == kSkinningLootType)
            return kSkinningSkillId;
        return targetIsGameObject ? nodeGatherSkill : kNoGatherSkill;
    }

    // True when the server-side loot type marks a skinning window. Used to
    // bypass the ordinary IsLootAllowed filter for skins, exactly as the old
    // (dead) wire-type check intended.
    inline bool IsSkinningLoot(uint32_t internalLootType)
    {
        return internalLootType == kSkinningLootType;
    }
}
