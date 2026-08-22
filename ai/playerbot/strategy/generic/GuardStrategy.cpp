// Forward-ported from mod-playerbots Base/Strategy/GuardStrategy.cpp
// Source: mod-playerbots@5397110, Shyalya@1f9497e
/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "GuardStrategy.h"

std::vector<NextAction> GuardStrategy::getDefaultActions()
{
    return {
        NextAction("guard", 4.0f)
    };
}

void GuardStrategy::InitTriggers(std::vector<TriggerNode*>& /*triggers*/) {}
