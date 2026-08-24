// Forward-ported from mod-playerbots Druid/Strategy/RestoDruidStrategy.cpp
// Source: mod-playerbots@5397110
/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "RestoDruidStrategy.h"
#include "Playerbots.h"

RestoDruidStrategy::RestoDruidStrategy(PlayerbotAI* botAI) : GenericDruidStrategy(botAI)
{
    // No custom ActionNodeFactory needed
}

void RestoDruidStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    GenericDruidStrategy::InitTriggers(triggers);

    triggers.push_back(new TriggerNode("no healer dps strategy",
                                       { NextAction("tree form", 5.0f) }));

    triggers.push_back(new TriggerNode(
        "party member to heal out of spell range",
        { NextAction("reach party member to heal", 39.0f) }));

    triggers.push_back(
        new TriggerNode("party member critical health",
                        {
                            NextAction("tree form",              34.1f),
                            NextAction("swiftmend on party",     34.0f),
                            NextAction("regrowth on party",      32.0f),
                            NextAction("healing touch on party", 31.0f),
                        }));

    triggers.push_back(
        new TriggerNode("party member critical health",
                        { NextAction("nature's swiftness", 58.0f) }));

    triggers.push_back(new TriggerNode(
        "nature's swiftness active",
        { NextAction("healing touch on party", 55.0f) }));

    triggers.push_back(new TriggerNode("clearcasting",
        { NextAction("rejuvenation on party", 13.0f) }));

    // LOW
    triggers.push_back(
        new TriggerNode("party member low health",
                        {
                            NextAction("tree form",              21.5f),
                            NextAction("swiftmend on party",     21.4f),
                            NextAction("regrowth on party",      21.2f),
                            NextAction("healing touch on party", 21.1f),
                        }));

    // MEDIUM
    triggers.push_back(
        new TriggerNode("party member medium health",
                        {
                            NextAction("tree form",              20.5f),
                            NextAction("swiftmend on party",     20.4f),
                            NextAction("regrowth on party",      20.2f),
                            NextAction("healing touch on party", 20.1f),
                        }));

    // ALMOST FULL
    triggers.push_back(
        new TriggerNode("party member almost full health",
                        {
                            NextAction("rejuvenation on party", 10.2f),
                            NextAction("regrowth on party",      10.1f),
                        }));

    triggers.push_back(
        new TriggerNode("medium mana", { NextAction("innervate", 25.0f) }));

    triggers.push_back(new TriggerNode("enemy too close for spell",
                                       { NextAction("flee", 39.0f) }));
}

void DruidTranquilityStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("medium group heal setting",
                                       { NextAction("tree form", 30.6f), NextAction("tranquility", 30.5f) }));
}

void DruidBlanketStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "rejuvenation blanket",
        { NextAction("tree form", 6.1f), NextAction("rejuvenation blanket", 6.0f) }));
}
