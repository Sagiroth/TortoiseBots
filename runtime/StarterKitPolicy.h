#pragma once

#include <cstdint>

// Starter-kit policy for issue #401: which bags, soul bag and ammo container a
// bot owns, and which container fits a ranged weapon. Pure decision rules over
// durable facts (class, weapon subclass, owned counts) so they stay unit
// tested on their own (tools/test_starter_kit_policy.cpp); the level->entry
// ladders themselves stay data-driven in RandomItemMgr (vendor-joined Budgets,
// never raid drops).
//
// Item ids are server data from tw_world.item_template, pinned here (not
// guessed): 3914 Journeyman's Backpack (14 slots, req 0, any class), 22243
// Small Soul Pouch (12 slots, warlock-only via allowable_class 256).
// Subclass numbers mirror ItemPrototype.h (ItemSubclassWeapon /
// ItemSubclassQuiver): BOW 2, GUN 3, THROWN 16, CROSSBOW 18,
// container QUIVER 2, AMMO_POUCH 3. Class numbers mirror SharedDefines.h:
// HUNTER 3, WARLOCK 9.

namespace TortoiseBots
{

inline constexpr uint32_t STARTER_PLAIN_BAG_ENTRY = 3914;
inline constexpr uint32_t STARTER_PLAIN_BAG_COUNT = 3;
inline constexpr uint32_t STARTER_SOUL_BAG_ENTRY = 22243;

inline constexpr uint32_t WEAPON_SUBCLASS_BOW = 2;
inline constexpr uint32_t WEAPON_SUBCLASS_GUN = 3;
inline constexpr uint32_t WEAPON_SUBCLASS_THROWN = 16;
inline constexpr uint32_t WEAPON_SUBCLASS_CROSSBOW = 18;

inline constexpr uint32_t CONTAINER_SUBCLASS_QUIVER = 2;
inline constexpr uint32_t CONTAINER_SUBCLASS_AMMO_POUCH = 3;

inline constexpr uint32_t CLASS_HUNTER_ID = 3;
inline constexpr uint32_t CLASS_WARLOCK_ID = 9;

// Ammo container the ranged weapon needs: gun -> pouch, bow/crossbow ->
// quiver, anything else (thrown, melee, wand, none) -> none.
inline uint32_t ContainerSubclassForWeapon(uint32_t weaponSubClass)
{
    if (weaponSubClass == WEAPON_SUBCLASS_GUN)
        return CONTAINER_SUBCLASS_AMMO_POUCH;
    if (weaponSubClass == WEAPON_SUBCLASS_BOW || weaponSubClass == WEAPON_SUBCLASS_CROSSBOW)
        return CONTAINER_SUBCLASS_QUIVER;
    return 0;
}

// Soul bag entry for the class, 0 when the class needs none.
inline uint32_t SoulBagEntryForClass(uint32_t classId)
{
    return classId == CLASS_WARLOCK_ID ? STARTER_SOUL_BAG_ENTRY : 0;
}

// A bot with fewer plain bags than the starter set still needs seeding.
inline bool NeedsStarterBags(uint32_t plainBagCount)
{
    return plainBagCount < STARTER_PLAIN_BAG_COUNT;
}

// Plain-bag cap: four bag slots, STARTER_PLAIN_BAG_COUNT plain bags wanted.
// Hunters keep one slot free for the quiver, warlocks one for the soul
// pouch, so they cap one lower. Production (InitStarterBags) and this test
// share the rule.
inline uint32_t MaxPlainBagsForClass(uint32_t classId)
{
    if (classId == CLASS_HUNTER_ID || classId == CLASS_WARLOCK_ID)
        return STARTER_PLAIN_BAG_COUNT - 1;
    return STARTER_PLAIN_BAG_COUNT;
}

} // namespace TortoiseBots
