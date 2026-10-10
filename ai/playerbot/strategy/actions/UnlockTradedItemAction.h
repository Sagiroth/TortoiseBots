// Vanilla/Tortoise trade lockpicking action.

#ifndef PLAYERBOTS_UNLOCKTRADEDITEMACTION_H
#define PLAYERBOTS_UNLOCKTRADEDITEMACTION_H

#include "Action.h"

class PlayerbotAI;
class Item;
class Player;

class UnlockTradedItemAction : public Action
{
public:
    UnlockTradedItemAction(PlayerbotAI* botAI) : Action(botAI, "unlock traded item") {}

    bool Execute(Event& event) override;
    // Automatic path gate (AG-5): only a rogue with a locked box in the
    // trader's do-not-trade slot runs on window updates. The fine checks
    // (skill, spell, level) stay in Execute, which still tells when asked.
    bool isUseful() override;

private:
    bool CanUnlockItem(Item* item);
    bool UnlockItem(Item* item, Player* requester);
};

#endif
