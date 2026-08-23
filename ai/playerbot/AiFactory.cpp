#include "playerbot/playerbot.h"
#include "playerbot/AiFactory.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/ReactionEngine.h"
#include "playerbot/strategy/warrior/WarriorAiObjectContext.h"
#include "Log.h"
#include "Objects/Player.h"

AiObjectContext* AiFactory::createAiObjectContext(Player* player, PlayerbotAI* ai)
{
    if (player->GetClass() == CLASS_WARRIOR) return new ai::WarriorAiObjectContext(ai);
    return new AiObjectContext(ai);
}

std::map<uint32, int32> AiFactory::GetPlayerSpecTabs(Player const* bot)
{
    std::map<uint32, int32> tabs{{0, 0}, {1, 0}, {2, 0}};
    for (uint32 i = 0; i < sTalentStore.GetNumRows(); ++i)
    {
        TalentEntry const* talent = sTalentStore.LookupEntry(i);
        if (!talent) continue;
        TalentTabEntry const* tab = sTalentTabStore.LookupEntry(talent->TalentTab);
        if (!tab || !(bot->GetClassMask() & tab->ClassMask)) continue;
        for (int rank = MAX_TALENT_RANK - 1; rank >= 0; --rank)
            if (talent->RankID[rank] && bot->HasSpell(talent->RankID[rank])) { tabs[tab->tabpage] += rank + 1; break; }
    }
    return tabs;
}

int AiFactory::GetPlayerSpecTab(Player const* bot)
{
    std::map<uint32, int32> tabs = GetPlayerSpecTabs(bot);
    if (bot->GetLevel() < 10 || tabs[0] + tabs[1] + tabs[2] == 0) return 0;
    return std::max_element(tabs.begin(), tabs.end(), [](auto const& a, auto const& b) { return a.second < b.second; })->first;
}

BotRoles AiFactory::GetPlayerRoles(uint8 cls, uint8 tab)
{
    return cls == CLASS_WARRIOR && tab == 2 ? BOT_ROLE_TANK : BOT_ROLE_DPS;
}

BotRoles AiFactory::GetPlayerRoles(Player const* player) { return GetPlayerRoles(player->GetClass(), GetPlayerSpecTab(player)); }

namespace
{
char const* WarriorSpec(Player* player)
{
    int tab = AiFactory::GetPlayerSpecTab(player);
    if (tab == 2) return "protection";
    if (tab == 1 && player->GetLevel() >= 30) return "fury";
    return "arms";
}

void AddWarriorStrategies(Player* player, Engine* engine, bool follow, bool dead)
{
    char const* spec = WarriorSpec(player);
    engine->addStrategy(spec);
    if (dead) engine->addStrategy("dead");
    if (follow) engine->addStrategy("follow");
    sLog.outString("TortoiseBots AI: Warrior strategy registered spec=%s follow=%u dead=%u", spec, follow, dead);
}
}

void AiFactory::AddDefaultCombatStrategies(Player* player, PlayerbotAI*, Engine* engine)
{
    AddWarriorStrategies(player, engine, true, false);
    engine->addStrategies("dps assist", "close", nullptr);
}

Engine* AiFactory::createCombatEngine(Player* player, PlayerbotAI* ai, AiObjectContext* context)
{
    Engine* engine = new Engine(ai, context, BotState::BOT_STATE_COMBAT);
    AddDefaultCombatStrategies(player, ai, engine);
    return engine;
}

void AiFactory::AddDefaultNonCombatStrategies(Player* player, PlayerbotAI*, Engine* engine)
{
    AddWarriorStrategies(player, engine, true, false);
    engine->addStrategy("nc");
}

Engine* AiFactory::createNonCombatEngine(Player* player, PlayerbotAI* ai, AiObjectContext* context)
{
    Engine* engine = new Engine(ai, context, BotState::BOT_STATE_NON_COMBAT);
    AddDefaultNonCombatStrategies(player, ai, engine);
    return engine;
}

void AiFactory::AddDefaultDeadStrategies(Player* player, PlayerbotAI*, Engine* engine)
{
    AddWarriorStrategies(player, engine, true, true);
}

Engine* AiFactory::createDeadEngine(Player* player, PlayerbotAI* ai, AiObjectContext* context)
{
    Engine* engine = new Engine(ai, context, BotState::BOT_STATE_DEAD);
    AddDefaultDeadStrategies(player, ai, engine);
    return engine;
}

void AiFactory::AddDefaultReactionStrategies(Player* player, PlayerbotAI*, ReactionEngine* engine)
{
    AddWarriorStrategies(player, engine, true, false);
}

ReactionEngine* AiFactory::createReactionEngine(Player* player, PlayerbotAI* ai, AiObjectContext* context)
{
    ReactionEngine* engine = new ReactionEngine(ai, context, BotState::BOT_STATE_REACTION);
    AddDefaultReactionStrategies(player, ai, engine);
    return engine;
}
