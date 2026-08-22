// Forward-ported from mod-playerbots Base/Strategy/AttackEnemyPlayersStrategy.cpp
// Source: mod-playerbots@5397110, Shyalya@1f9497e
/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "AttackEnemyPlayersStrategy.h"
#include "Playerbots.h"

void AttackEnemyPlayersStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("enemy player near",
                                       { NextAction("attack enemy player", 55.0f) }));
}
