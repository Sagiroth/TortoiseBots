// Forward-ported from mod-playerbots Shaman/Strategy/GenericShamanStrategy.cpp
// Source: mod-playerbots@5397110
/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "GenericShamanStrategy.h"
#include "AiFactory.h"
#include "Playerbots.h"
#include "Strategy.h"

class GenericShamanStrategyActionNodeFactory : public NamedObjectFactory<ActionNode>
{
public:
    GenericShamanStrategyActionNodeFactory()
    {
        creators["flametongue totem"] = &flametongue_totem;
        creators["magma totem"] = &magma_totem;
        creators["strength of earth totem"] = &strength_of_earth_totem;
        creators["cleansing totem"] = &cleansing_totem;
        creators["windfury totem"] = &windfury_totem;
    }

private:
    // Passthrough totems are set up so lower level shamans will still cast totems.
    // Magma Totem -> Searing Totem
    // Strength of Earth Totem -> Stoneskin Totem
    // Cleansing Totem -> Mana Spring Totem
    static ActionNode* flametongue_totem([[maybe_unused]] PlayerbotAI* botAI)
    {
        return new ActionNode("flametongue totem",
                              /*P*/ {},
                              /*A*/ { NextAction("searing totem") },
                              /*C*/ {});
    }
    static ActionNode* magma_totem([[maybe_unused]] PlayerbotAI* botAI)
    {
        return new ActionNode("magma totem",
                              /*P*/ {},
                              /*A*/ { NextAction("searing totem") },
                              /*C*/ {});
    }
    static ActionNode* strength_of_earth_totem([[maybe_unused]] PlayerbotAI* botAI)
    {
        return new ActionNode("strength of earth totem",
                              /*P*/ {},
                              /*A*/ { NextAction("stoneskin totem") },
                              /*C*/ {});
    }
    static ActionNode* cleansing_totem([[maybe_unused]] PlayerbotAI* botAI)
    {
        return new ActionNode("cleansing totem",
                              /*P*/ {},
                              /*A*/ { NextAction("mana spring totem") },
                              /*C*/ {});
    }
    static ActionNode* windfury_totem([[maybe_unused]] PlayerbotAI* botAI)
    {
        return new ActionNode("windfury totem",
                              /*P*/ {},
                              /*A*/ { NextAction("grounding totem") },
                              /*C*/ {});
    }
};

GenericShamanStrategy::GenericShamanStrategy(PlayerbotAI* botAI) : CombatStrategy(botAI)
{
    actionNodeFactories.Add(new GenericShamanStrategyActionNodeFactory());
}

void GenericShamanStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    CombatStrategy::InitTriggers(triggers);

    triggers.push_back(new TriggerNode("purge", { NextAction("purge", ACTION_DISPEL), }));
    triggers.push_back(new TriggerNode("new pet", { NextAction("set pet stance", 65.0f), }));
}

void ShamanCureStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
}

void ShamanBoostStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("bloodlust", { NextAction("bloodlust", 30.0f), }));

}

void ShamanAoeStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{

    Player* bot = botAI->GetBot();
    int tab = AiFactory::GetPlayerSpecTab(bot);

    if (tab == SHAMAN_TAB_ELEMENTAL)
    {
        triggers.push_back(new TriggerNode("medium aoe",{ NextAction("fire nova", 23.0f), }));
        triggers.push_back(new TriggerNode("chain lightning no cd", { NextAction("chain lightning", 5.6f), }));
    }
    else if (tab == SHAMAN_TAB_ENHANCEMENT)
    {
        triggers.push_back(new TriggerNode("medium aoe",{ NextAction("fire nova", 23.0f), }));
        triggers.push_back(new TriggerNode("enemy within melee", { NextAction("fire nova", 5.1f), }));
    }
    // Resto AoE handled by "Healer DPS" Strategy
}
