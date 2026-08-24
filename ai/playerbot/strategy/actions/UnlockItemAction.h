// Vanilla/Turtle lockpicking action.

#ifndef PLAYERBOTS_UNLOCKITEMACTION_H
#define PLAYERBOTS_UNLOCKITEMACTION_H

#include "Action.h"

class PlayerbotAI;
class Item;
class Player;

class UnlockItemAction : public Action
{
public:
    UnlockItemAction(PlayerbotAI* botAI) : Action(botAI, "unlock item") { }

    bool Execute(Event& event) override;

private:
    void UnlockItem(Item* item, Player* requester);
};

#endif
