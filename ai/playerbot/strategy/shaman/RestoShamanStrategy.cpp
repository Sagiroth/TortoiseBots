// Forward-ported from mod-playerbots Shaman/Strategy/RestoShamanStrategy.cpp
// Source: mod-playerbots@5397110
/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "RestoShamanStrategy.h"
#include "Playerbots.h"

RestoShamanStrategy::RestoShamanStrategy(PlayerbotAI* botAI) : GenericShamanStrategy(botAI)
{
    // No custom ActionNodeFactory needed
}

// ===== Trigger Initialization ===
void RestoShamanStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    GenericShamanStrategy::InitTriggers(triggers);

    // Totem Triggers
    triggers.push_back(new TriggerNode("call of the elements", { NextAction("call of the elements", 60.0f) }));
    triggers.push_back(new TriggerNode("low health", { NextAction("stoneclaw totem", 40.0f) }));
    triggers.push_back(new TriggerNode("medium mana", { NextAction("mana tide totem", ACTION_HIGH + 5) }));
    triggers.push_back(new TriggerNode("water shield", { NextAction("water shield", ACTION_HIGH) }));

    // Healing Triggers
    triggers.push_back(new TriggerNode("group heal setting", { NextAction("chain heal on party", 26.0f) }));

    triggers.push_back(new TriggerNode("party member critical health", { NextAction("healing wave on party", 24.0f),
                                                                         NextAction("lesser healing wave on party", 23.0f) }));

    triggers.push_back(new TriggerNode("party member low health", { NextAction("healing wave on party", 18.0f),
                                                                    NextAction("lesser healing wave on party", 17.0f) }));

    triggers.push_back(new TriggerNode("party member medium health", { NextAction("healing wave on party", 15.0f),
                                                                       NextAction("lesser healing wave on party", 14.0f) }));

    triggers.push_back(new TriggerNode("party member almost full health", { NextAction("lesser healing wave on party", 11.0f) }));

    triggers.push_back(new TriggerNode("earth shield on party tank", { NextAction("earth shield on party tank", ACTION_HIGH) }));

    // Range/Mana Triggers
    triggers.push_back(new TriggerNode("enemy too close for spell", { NextAction("flee", ACTION_MOVE + 9) }));
    triggers.push_back(new TriggerNode("party member to heal out of spell range", { NextAction("reach party member to heal", ACTION_CRITICAL_HEAL + 1) }));
}

void ShamanHealerDpsStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("healer should attack", { NextAction("flame shock", ACTION_DEFAULT + 0.2f),
                                                                 NextAction("lightning bolt", ACTION_DEFAULT) }));

    triggers.push_back( new TriggerNode("medium aoe and healer should attack", { NextAction("chain lightning", ACTION_DEFAULT + 0.3f) }));
}
