// Forward-ported from mod-playerbots TameAction.h
/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_TAMEACTION_H
#define PLAYERBOTS_TAMEACTION_H

#include "Action.h"
#include <string>

class PlayerbotAI;
class Player;
class Creature;

class TameAction : public Action
{
public:
    TameAction(PlayerbotAI* botAI) : Action(botAI, "tame") {}

    bool Execute(Event& event) override;

private:
    Creature* FindTarget(std::string const& mode, std::string const& value, Player* requester);
    bool RenamePet(std::string const& name, Player* requester);
    bool AbandonPet(Player* requester);
};

#endif
