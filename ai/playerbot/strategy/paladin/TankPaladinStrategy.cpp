// Forward-ported from mod-playerbots Paladin/Strategy/TankPaladinStrategy.cpp
// Source: mod-playerbots@5397110
/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "TankPaladinStrategy.h"
#include "Playerbots.h"

TankPaladinStrategy::TankPaladinStrategy(PlayerbotAI* botAI) : GenericPaladinStrategy(botAI) {}

std::vector<NextAction> TankPaladinStrategy::getDefaultActions()
{
    return {
        NextAction("consecration", ACTION_DEFAULT + 0.6f),
        NextAction("judgement of wisdom", ACTION_DEFAULT + 0.4f),
        NextAction("melee", ACTION_DEFAULT)
    };
}

void TankPaladinStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    GenericPaladinStrategy::InitTriggers(triggers);

    triggers.push_back(
        new TriggerNode(
            "seal",
            {
                NextAction("seal of righteousness", ACTION_HIGH)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "low mana",
            {
                NextAction("seal of wisdom", ACTION_HIGH + 9)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "light aoe",
            {
                NextAction("consecration", ACTION_HIGH + 5)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "medium aoe",
            {
                NextAction("consecration", ACTION_HIGH + 7)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "lose aggro",
            {
                NextAction("judgement", ACTION_HIGH + 7)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "medium health",
            {
                NextAction("holy shield", ACTION_HIGH + 4)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "target critical health",
            {
                NextAction("hammer of wrath", ACTION_CRITICAL_HEAL)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "righteous fury",
            {
                NextAction("righteous fury", ACTION_HIGH + 8)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "enough mana",
            {
                NextAction("consecration", ACTION_HIGH + 4)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "not facing target",
            {
                NextAction("set facing", ACTION_NORMAL + 7)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "enemy out of melee",
            {
                NextAction("reach melee", ACTION_HIGH + 1)
            }
        )
    );
}
