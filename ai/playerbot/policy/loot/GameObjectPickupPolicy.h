#pragma once

// Automatic game-object pickup. Bots open a world object only when it is
// needed for one of their quests, a gathering node they can work, or a chest
// that asks for a key or a skill (locked treasure, lockboxes). Open world
// props that anyone can take (festive mugs, feast food, tomes) stay where
// they are unless a quest needs them or a player ordered the pickup.
namespace ai
{
    struct GameObjectPickupFacts
    {
        bool isChest = false;            // GAMEOBJECT_TYPE_CHEST
        bool keyOrSkillLocked = false;   // lock asks for an item or a lockpick/treasure skill
        bool gatheringNode = false;      // herb / ore / fishing lock
        bool questObject = false;        // the object or one of its loot items is needed for a quest
        bool orderedByPlayer = false;    // explicit player order ("add all loot")
    };

    inline bool AllowGameObjectPickup(GameObjectPickupFacts const& facts)
    {
        if (facts.orderedByPlayer || !facts.isChest)
            return true;
        return facts.questObject || facts.gatheringNode || facts.keyOrSkillLocked;
    }
}
