#include "playerbot/PlayerbotAIConfig.h"
#include "Policies/SingletonImp.h"

INSTANTIATE_SINGLETON_1(PlayerbotAIConfig)

PlayerbotAIConfig::PlayerbotAIConfig() : enabled(true)
{
    globalCoolDown = 500;
    reactDelay = 100;
    iterationsPerTick = 100;
    sightDistance = 75.0f;
    reactDistance = 30.0f;
    contactDistance = 0.5f;
    targetPosRecalcDistance = 0.5f;
    criticalHealth = 20;
    lowHealth = 50;
    mediumHealth = 70;
    logValuesPerTick = false;
    logInGroupOnly = false;
}

bool PlayerbotAIConfig::CanLogAction(PlayerbotAI*, std::string, bool, std::string) { return false; }
bool PlayerbotAIConfig::IsFreeAltBot(uint32) { return false; }
