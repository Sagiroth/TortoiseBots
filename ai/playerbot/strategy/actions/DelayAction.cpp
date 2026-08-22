// Forward-ported from mod-playerbots DelayAction.cpp - modern donor
// Source: mod-playerbots@5397110, Shyalya@1f9497e Tortoise 1.18.1 baseline
/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "DelayAction.h"
#include "Event.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"

bool DelayAction::Execute(Event /*event*/)
{
    const uint32 delay = PlayerbotAIConfig::instance().passiveDelay + PlayerbotAIConfig::instance().globalCoolDown;

    botAI->SetNextCheckDelay(delay);

    return true;
}

bool DelayAction::isUseful()
{
    return !botAI->AllowActivity(ALL_ACTIVITY);
}
