// Forward-ported from mod-playerbots WipeAction.cpp - modern donor
// Source: mod-playerbots@5397110, Shyalya@1f9497e Tortoise 1.18.1 baseline
/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "WipeAction.h"
#include "PlayerbotAI.h"

bool WipeAction::Execute(Event event)
{
    Player* const owner = event.GetOwner();
    Player* const master = this->botAI->GetMaster();

    if (owner != nullptr && master != nullptr && master->GetGUID() != owner->GetGUID())
        return false;

    bot->Kill(bot, bot);

    return true;
}
