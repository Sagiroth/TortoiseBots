#pragma once

#include <cstdint>
#include <vector>

// Pure rule for automatic game-object pickup (LootObjectStack.cpp: LootObject
// is only offered to the bot when IsLootPossible passes). Kept free of core
// and AI headers so tools/test_game_object_pickup_policy.cpp can pin it.
//
// A game object may be opened by the automatic sweep only when it is
// (a) needed for an active quest of the bot,
// (b) a gathering node the bot can work (herb, ore, fishing),
// (c) a chest opened with a key or a skill (locked chests, lockboxes), or
// (d) a "treasure" chest the bot would already open on purpose.
// Everything else - the OPEN_KNEELING / OPEN_ATTACKING single-item world
// props in cities (Festive Mug 180905 and its peers: Feast Boar 21145,
// Feast Fish 21146, Feast Goblet 21148, Dusty Tome 179547/179548) - is
// decorative clutter the sweep must walk past.
//
// The caller classifies the object from its template + lock and passes plain
// facts in; the DB facts behind the taxonomy:
// - Festive Mug: type 3 (CHEST), lock 93 (OPEN_KNEELING + OPEN_ATTACKING,
//   no picklock/treasure), one loot-table row (item 21174, chance 100),
//   no gameobject_questrelation row, no quest needs the item.
// - Real treasure chests use lock 57 / 599 (PICKLOCK + TREASURE) with
//   dozens-hundreds of loot rows (Battered Chest 2843: 113 rows, Dark
//   Coffer 160845: 169 rows). Dusty-tome "treasure" excepted from the row
//   count below: lock 93 but 10-12 rows of skill books.
// - Gathering nodes carry herb/mining/fishing locks (type LOCK_KEY_SKILL
//   with index 2/3/19): Silverleaf 1617 (lock 29), Mageroyal 1620 (lock 9).
// - Quest props that are type 3 use lock 43 (OPEN) and have a
//   gameobject_questrelation row or a quest needing the item (Furlbrow's
//   Wardrobe 290 -> quest 64, Berard's Journal 1594 -> quest 450).
// The single-item lock-93 check below is the backstop for the objects the
// caller could not resolve a template row for (the Festive Mug shape:
// unlocked-by-animation chest, one common consumable, no quest anywhere).

namespace ai
{
    // Game-object facts the caller reads off the template + lock + quest log.
    struct GameObjectPickupFacts
    {
        bool isChest = false;            // GAMEOBJECT_TYPE_CHEST
        bool keyOrSkillLocked = false;   // lock asks for an item or a lockpick/treasure skill
        bool gatheringNode = false;      // herb / ore / fishing lock the bot can work
        bool questObject = false;        // bot has an active quest for the object or its items
        bool singleCommonItem = false;   // one loot-table row, common quality, no quest link
        bool orderedByPlayer = false;    // explicit player/guild order ("add all loot", farm order)
    };

    // Automatic sweep verdict. An explicit player order always wins: the bot
    // must still pick up what it was told to, even a decorative mug.
    inline bool AllowGameObjectPickup(GameObjectPickupFacts const& facts)
    {
        if (facts.orderedByPlayer)
            return true;
        if (!facts.isChest)
            return true;
        if (facts.questObject)
            return true;
        if (facts.gatheringNode)
            return true;
        if (facts.keyOrSkillLocked)
            return true;
        if (facts.singleCommonItem)
            return false;
        return true;
    }

    // Backstop for callers that only resolved the loot-table rows: a chest
    // whose whole table is one common item with no quest link is the
    // decorative-clutter shape (Festive Mug: 1 row, item 21174 quality 1).
    // Multi-row tables (real treasure, tomes) and quest loot pass.
    inline bool LootTableLooksDecorative(std::vector<std::uint32_t> const& itemQualities,
        bool anyQuestLinked)
    {
        if (itemQualities.size() != 1)
            return false;
        if (anyQuestLinked)
            return false;
        return itemQualities[0] <= 1;
    }
}
