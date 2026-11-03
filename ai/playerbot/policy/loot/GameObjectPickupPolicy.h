#pragma once

// Automatic game-object pickup. Open world props anyone can take (festive
// mugs, feast food, goblets) hold only common or poor items; bots leave them
// unless one of their quests needs the object or an item in it. Gathering
// nodes, chests that ask for a key or a skill, and open chests that can hold
// something better than common (treasure, tomes) are picked up as before.
namespace ai
{
    struct GameObjectPickupFacts
    {
        bool isChest = false;            // GAMEOBJECT_TYPE_CHEST
        bool keyOrSkillLocked = false;   // lock asks for an item or a gathering/lockpick skill
        bool questObject = false;        // the object or one of its loot items is needed for a quest
        bool onlyCommonLoot = false;     // every possible item is common or poor quality
    };

    inline bool AllowGameObjectPickup(GameObjectPickupFacts const& facts)
    {
        if (!facts.isChest || facts.keyOrSkillLocked || facts.questObject)
            return true;
        return !facts.onlyCommonLoot;
    }
}
