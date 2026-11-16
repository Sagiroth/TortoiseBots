
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/playerbot.h"
#include "playerbot/AmmoStackPolicy.h"
#include "playerbot/BossTacticsMgr.h"
#include "playerbot/BossStandMgr.h"
#include "playerbot/DungeonPlanMgr.h"
#include "playerbot/CampMgr.h"
#include "../../runtime/BotNames.h"
#include "../../runtime/HireLevelPolicy.h"
#include "../../runtime/HireFeePolicy.h"
#include "../../runtime/HireRaidPolicy.h"
#include "../../runtime/policy/hire/BotGearPolicy.h"
#include "../../runtime/HireUnlockPolicy.h"
#include "../../runtime/RemoteCommanderView.h"
#include <algorithm>
#include "../../runtime/RunTrace.h"
#include "../../runtime/DecisionCaptureLimits.h"
#include "../../runtime/GossipOrderPolicy.h"
#include <cerrno>
#include <cstring>
#include <thread>
#include <zlib.h>
#include "BotLog.h"
#include "AccountMgr.h"
#include "RandomItemMgr.h"
#include "playerbot/PlayerbotHelpMgr.h"
#include "playerbot/strategy/ValueRefreshPolicy.h"
#include "playerbot/strategy/actions/CheatAction.h"
// Issue #265: the managed-account registry is the authority for random-pool
// account identity (prefix matching never authorizes ownership).
#include "../../runtime/RandomBotAccountRegistry.h"
#include "../../runtime/AltBotService.h"
// logEvent forwards the activity whitelist to the telemetry daemon.
#include "../../runtime/ObservabilityEmitter.h"
#include "../../runtime/CombatMonitorService.h"
// Faction-ratio / start-zone-weight parsing lives in the core-free
// StartZoneBalance.h so the standalone unit test can exercise it.
#include "../../runtime/StartZoneBalance.h"
#include "../../runtime/policy/ah/AhBotItemPoolPolicy.h"

#include "playerbot/TravelMgr.h"
#include "playerbot/GuildOrderSources.h"
#include "playerbot/GuildOrderEnchants.h"

#include <iostream>
#include <numeric>
#include <iomanip>
#include <set>
#include <fstream>
#include <sstream>

// Defined in Helpers.cpp.
std::string& trim(std::string& s);

std::vector<std::string> ConfigAccess::GetValues(const std::string& name) const
{
    // M10: enumerate real repeated keys (AiPlayerbot.WorldBuff, .2, .0.0.40)
    // through the native section/key walk. Dotted-family rule: the key must
    // equal the prefix or continue with '.'. First-match rule mirrors
    // single-key reads (GetValueHelper scans sections in order).
    // NOTE: returns KEY names, not values — the WorldBuff loader parses
    // faction/class/spec/levels out of the key and reads the spell list
    // with GetStringDefault(key) itself.
    std::vector<std::string> values;
    std::vector<std::string> sections;
    m_config.GetRootSections(sections);
    std::set<std::string> seen;
    for (std::string const& section : sections)
    {
        std::vector<std::string> keys;
        m_config.GetKeys(section.c_str(), keys);
        for (std::string const& key : keys)
        {
            if (key.compare(0, name.size(), name) != 0)
                continue;
            if (key.size() != name.size() && key[name.size()] != '.')
                continue;
            if (!seen.insert(key).second)
                continue;
            ACE_TString value;
            if (m_config.GetValueHelper(key.c_str(), value))
                values.emplace_back(key);
        }
    }
    return values;
}

INSTANTIATE_SINGLETON_1(PlayerbotAIConfig);

PlayerbotAIConfig::PlayerbotAIConfig()
: enabled(false)
{
}

template <class T>
void LoadList(std::string value, T &list)
{
    list.clear();
    std::vector<std::string> ids = split(value, ',');
    for (std::vector<std::string>::iterator i = ids.begin(); i != ids.end(); i++)
    {
        std::string string = *i;
        if (string.empty())
            continue;

        uint32 id = atoi(string.c_str());

        list.push_back(id);
    }
}

template <class T>
void LoadListString(std::string value, T& list)
{
    list.clear();
    std::vector<std::string> strings = split(value, ',');
    for (std::vector<std::string>::iterator i = strings.begin(); i != strings.end(); i++)
    {
        std::string string = *i;
        if (string.empty())
            continue;

        list.push_back(string);
    }
}

void PlayerbotAIConfig::LoadDungeonRunKeys()
{
    dungeonSelfClear = config.GetBoolDefault("AiPlayerbot.DungeonSelfClear", true);
    dungeonRunRaid = config.GetBoolDefault("AiPlayerbot.DungeonRunRaid", false);
    dungeonRunRaidBossRules = config.GetBoolDefault("AiPlayerbot.DungeonRunRaidBossRules", false);
    raidHealerSeatsByClass = config.GetBoolDefault("AiPlayerbot.RaidHealerSeatsByClass", false);
    raidTankHealerAssist = config.GetBoolDefault("AiPlayerbot.RaidTankHealerAssist", false);
    dungeonRunRaidHealerGuard = config.GetBoolDefault("AiPlayerbot.DungeonRunRaidHealerGuard", false);
    interruptIgnoresThreat = config.GetBoolDefault("AiPlayerbot.InterruptIgnoresThreat", false);
    interruptAllowStun = config.GetBoolDefault("AiPlayerbot.InterruptAllowStun", false);
    spellFacingPrecheck = config.GetBoolDefault("AiPlayerbot.SpellFacingPrecheck", false);
    fankrissSwapStacks = std::min(9, std::max(1, config.GetIntDefault("AiPlayerbot.FankrissSwapStacks", 5)));
    {
        int const patchwerkDelay = config.GetIntDefault("AiPlayerbot.PatchwerkOpeningDelay", 4);
        patchwerkOpeningDelay = patchwerkDelay <= 0 ? 0 : std::min(5, std::max(3, patchwerkDelay));
    }
    dungeonRunLinkedDeath = config.GetBoolDefault("AiPlayerbot.DungeonRunLinkedDeath", false);
    dungeonRunAuraTactics = config.GetBoolDefault("AiPlayerbot.DungeonRunAuraTactics", false);
    dungeonRunAuraGatedTargets = config.GetBoolDefault("AiPlayerbot.DungeonRunAuraGatedTargets", false);
    dungeonRunRespawnNest = config.GetBoolDefault("AiPlayerbot.DungeonRunRespawnNest", false);
    dungeonRunKillFirstGrid = config.GetBoolDefault("AiPlayerbot.DungeonRunKillFirstGrid", false);
    dungeonRunTotemFocus = config.GetBoolDefault("AiPlayerbot.DungeonRunTotemFocus", false);
    dungeonRunPackFocusSparesTacticUnits =
        config.GetBoolDefault("AiPlayerbot.DungeonRunPackFocusSparesTacticUnits", false);
    dungeonRunUseObject = config.GetBoolDefault("AiPlayerbot.DungeonRunUseObject", false);
    dungeonRunTankSwap = config.GetBoolDefault("AiPlayerbot.DungeonRunTankSwap", false);
    dungeonRunTankPost = config.GetBoolDefault("AiPlayerbot.DungeonRunTankPost", false);
    dungeonRunAddCrew = config.GetBoolDefault("AiPlayerbot.DungeonRunAddCrew", false);
    dungeonRunGuardLeavesAssignedTank =
        config.GetBoolDefault("AiPlayerbot.DungeonRunGuardLeavesAssignedTank", false);
    dungeonRunGuardClockHittable = config.GetBoolDefault("AiPlayerbot.DungeonRunGuardClockHittable", false);
    dungeonRunTankPostHoldSpot = config.GetBoolDefault("AiPlayerbot.DungeonRunTankPostHoldSpot", false);
    dungeonRunTankPostLeash = config.GetBoolDefault("AiPlayerbot.DungeonRunTankPostLeash", false);
    dungeonRunTankPostOwnsSubject =
        config.GetBoolDefault("AiPlayerbot.DungeonRunTankPostOwnsSubject", false);
    dungeonRunTankPostTaunts = config.GetBoolDefault("AiPlayerbot.DungeonRunTankPostTaunts", false);
    dungeonRunTankPostWalks = config.GetBoolDefault("AiPlayerbot.DungeonRunTankPostWalks", false);
    dungeonRunTankPostOwnsUnit = config.GetBoolDefault("AiPlayerbot.DungeonRunTankPostOwnsUnit", false);
    dungeonRunTankPostMain = config.GetBoolDefault("AiPlayerbot.DungeonRunTankPostMain", false);
    dungeonRunTankPostKeepsUnit = config.GetBoolDefault("AiPlayerbot.DungeonRunTankPostKeepsUnit", false);
    dungeonRunTankPostWaits = config.GetBoolDefault("AiPlayerbot.DungeonRunTankPostWaits", false);
    dungeonRunTankPostHealers = config.GetBoolDefault("AiPlayerbot.DungeonRunTankPostHealers", false);
    dungeonRunSeekAura = config.GetBoolDefault("AiPlayerbot.DungeonRunSeekAura", false);
    dungeonRunSeekPullCycle = config.GetBoolDefault("AiPlayerbot.DungeonRunSeekPullCycle", false);
    dungeonRunSeekSonMeet = config.GetBoolDefault("AiPlayerbot.DungeonRunSeekSonMeet", false);
    dungeonRunSeekTakeEdge = config.GetBoolDefault("AiPlayerbot.DungeonRunSeekTakeEdge", false);
    dungeonRunSeekMeetNoHeal = config.GetBoolDefault("AiPlayerbot.DungeonRunSeekMeetNoHeal", false);
    dungeonRunSeekWalkoutFocus = config.GetBoolDefault("AiPlayerbot.DungeonRunSeekWalkoutFocus", false);
    dungeonRunSeekWalkoutHoldCombat = config.GetBoolDefault("AiPlayerbot.DungeonRunSeekWalkoutHoldCombat", false);
    dungeonRunSeekVictimRelief = config.GetBoolDefault("AiPlayerbot.DungeonRunSeekVictimRelief", false);
    dungeonRunSeekWalkoutFaceCast = config.GetBoolDefault("AiPlayerbot.DungeonRunSeekWalkoutFaceCast", false);
    dungeonRunSeekWalkoutStandLog = config.GetBoolDefault("AiPlayerbot.DungeonRunSeekWalkoutStandLog", false);
    dungeonRunSeekWalkoutStandSight = config.GetBoolDefault("AiPlayerbot.DungeonRunSeekWalkoutStandSight", false);
    dungeonRunPlaceUnstick = config.GetBoolDefault("AiPlayerbot.DungeonRunPlaceUnstick", false);
    dungeonRunSeekNearestTake = config.GetBoolDefault("AiPlayerbot.DungeonRunSeekNearestTake", false);
    dungeonRunSeekNearestTakeReach = config.GetBoolDefault("AiPlayerbot.DungeonRunSeekNearestTakeReach", false);
    dungeonRunSeekMeetPrePlace = config.GetBoolDefault("AiPlayerbot.DungeonRunSeekMeetPrePlace", false);
    dungeonRunSeekMeetPrePlaceLate = config.GetBoolDefault("AiPlayerbot.DungeonRunSeekMeetPrePlaceLate", false);
    dungeonRunSeekShotWaitsReceiver = config.GetBoolDefault("AiPlayerbot.DungeonRunSeekShotWaitsReceiver", false);
    dungeonRunSeekMeetPrePlaceHold = config.GetBoolDefault("AiPlayerbot.DungeonRunSeekMeetPrePlaceHold", false);
    dungeonRunSeekMeetTakeReach = config.GetBoolDefault("AiPlayerbot.DungeonRunSeekMeetTakeReach", false);
    dungeonRunSeekTakenSonThreatCap = config.GetBoolDefault("AiPlayerbot.DungeonRunSeekTakenSonThreatCap", false);
    dungeonRunSeekSiphonClock = config.GetBoolDefault("AiPlayerbot.DungeonRunSeekSiphonClock", false);
    dungeonRunSeekCycleRoll = config.GetBoolDefault("AiPlayerbot.DungeonRunSeekCycleRoll", false);
    dungeonRunThreatResetWindow = config.GetBoolDefault("AiPlayerbot.DungeonRunThreatResetWindow", false);
    dungeonRunPullGate = config.GetBoolDefault("AiPlayerbot.DungeonRunPullGate", false);
    dungeonRunBossSpread = config.GetBoolDefault("AiPlayerbot.DungeonRunBossSpread", false);
    dungeonRunBossSpreadSlots = config.GetBoolDefault("AiPlayerbot.DungeonRunBossSpreadSlots", false);
    reactionHealCastGuardPct =
        std::min(100, std::max(0, config.GetIntDefault("AiPlayerbot.ReactionHealCastGuardPct", 0)));
    dungeonRunBossProtectionPotion = config.GetBoolDefault("AiPlayerbot.DungeonRunBossProtectionPotion", false);
    dungeonRunHealReserve = config.GetBoolDefault("AiPlayerbot.DungeonRunHealReserve", false);
    spreadRangedMinRange = config.GetBoolDefault("AiPlayerbot.SpreadRangedMinRange", false);
    dungeonRunAvoidCastAll = config.GetBoolDefault("AiPlayerbot.DungeonRunAvoidCastAll", false);
    dungeonRunBurnMana = config.GetBoolDefault("AiPlayerbot.DungeonRunBurnMana", false);
    bossStandOutranksBreathDodge = config.GetBoolDefault("AiPlayerbot.BossStandOutranksBreathDodge", false);
    bossStandEvents = config.GetBoolDefault("AiPlayerbot.BossStandEvents", false);
    bossStandWholeRaid = config.GetBoolDefault("AiPlayerbot.BossStandWholeRaid", false);
    bossTacticsKeepsEngagedBoss = config.GetBoolDefault("AiPlayerbot.BossTacticsKeepsEngagedBoss", false);
    dungeonRunRangedOnly = config.GetBoolDefault("AiPlayerbot.DungeonRunRangedOnly", false);
    rangedReachHoverTarget = config.GetBoolDefault("AiPlayerbot.RangedReachHoverTarget", false);
    airborneWandYieldsToNukes = config.GetBoolDefault("AiPlayerbot.AirborneWandYieldsToNukes", false);
    hoverCastHoldsSpread = config.GetBoolDefault("AiPlayerbot.HoverCastHoldsSpread", false);
    dungeonRunGateHold = config.GetBoolDefault("AiPlayerbot.DungeonRunGateHold", false);
    dungeonRunCharmYield = config.GetBoolDefault("AiPlayerbot.DungeonRunCharmYield", false);
    dungeonRunImmuneBossVictimEscape = config.GetBoolDefault("AiPlayerbot.DungeonRunImmuneBossVictimEscape", false);
    dungeonRunCarrierStrike = config.GetBoolDefault("AiPlayerbot.DungeonRunCarrierStrike", false);
    dungeonRunCarrierScan = config.GetBoolDefault("AiPlayerbot.DungeonRunCarrierScan", false);
    dungeonRunShadeHealerCover = config.GetBoolDefault("AiPlayerbot.DungeonRunShadeHealerCover", false);
    dungeonRunCarrierHold = config.GetBoolDefault("AiPlayerbot.DungeonRunCarrierHold", false);
    dungeonRunThreatSuccessorNotHolder = config.GetBoolDefault("AiPlayerbot.DungeonRunThreatSuccessorNotHolder", false);
    dungeonRunCureIgnoresThreat = config.GetBoolDefault("AiPlayerbot.DungeonRunCureIgnoresThreat", false);
    dungeonRunThreatSuccessorStrict = config.GetBoolDefault("AiPlayerbot.DungeonRunThreatSuccessorStrict", false);
    dungeonRunThreatSuccessorKeepsAway = config.GetBoolDefault("AiPlayerbot.DungeonRunThreatSuccessorKeepsAway", false);
    dungeonRunFactJudgeCharmedLead = config.GetBoolDefault("AiPlayerbot.DungeonRunFactJudgeCharmedLead", false);
    dungeonRunTauntGate = config.GetBoolDefault("AiPlayerbot.DungeonRunTauntGate", false);
    dungeonRunTauntAfterSkipsMain = config.GetBoolDefault("AiPlayerbot.DungeonRunTauntAfterSkipsMain", false);
    dungeonRunMaxMinutes = static_cast<uint32>(std::max(0, config.GetIntDefault("AiPlayerbot.DungeonRunMaxMinutes", 180)));
    dungeonRunAggroEstimate = config.GetBoolDefault("AiPlayerbot.DungeonRunAggroEstimate", false);
    dungeonRunAggroCeiling = static_cast<uint32>(std::max(1, config.GetIntDefault("AiPlayerbot.DungeonRunAggroCeiling", 6)));
    dungeonRunAggroWalkRing = config.GetBoolDefault("AiPlayerbot.DungeonRunAggroWalkRing", false);
    dungeonRunAggroRingYd = std::min(40.0f, std::max(5.0f,
        config.GetFloatDefault("AiPlayerbot.DungeonRunAggroRingYd", 20.0f)));
    // The margin's ceiling is kPackClearMarginMaxYd (30, kPatrolSeeYd in DungeonRunPolicy.h); the policy reads 0 as off.
    dungeonRunPackClearMarginYd = std::min(30.0f, std::max(0.0f,
        config.GetFloatDefault("AiPlayerbot.DungeonRunPackClearMarginYd", 10.0f)));
    dungeonRunPackClearRepulls = static_cast<uint32>(std::min(10, std::max(1,
        config.GetIntDefault("AiPlayerbot.DungeonRunPackClearRepulls", 3))));
    dungeonRunStragglerPull = config.GetBoolDefault("AiPlayerbot.DungeonRunStragglerPull", false);
    dungeonRunNearWipeStop = static_cast<uint32>(std::min(10, std::max(0,
        config.GetIntDefault("AiPlayerbot.DungeonRunNearWipeStop", 2))));
    dungeonRunNearWipeStopMinutes = static_cast<uint32>(std::min(60, std::max(1,
        config.GetIntDefault("AiPlayerbot.DungeonRunNearWipeStopMinutes", 10))));
    dungeonRunNearWipeStopParty = config.GetBoolDefault("AiPlayerbot.DungeonRunNearWipeStopParty", false);
    dungeonRunRezHold = config.GetBoolDefault("AiPlayerbot.DungeonRunRezHold", false);
    dungeonRunPullStand = config.GetBoolDefault("AiPlayerbot.DungeonRunPullStand", false);
    dungeonRunStandClearance = config.GetBoolDefault("AiPlayerbot.DungeonRunStandClearance", false);
    dungeonRunStandPullPreCc = config.GetBoolDefault("AiPlayerbot.DungeonRunStandPullPreCc", false);
    dungeonRunStandDiscipline = config.GetBoolDefault("AiPlayerbot.DungeonRunStandDiscipline", false);
    dungeonRunAutoCc = config.GetBoolDefault("AiPlayerbot.DungeonRunAutoCc", false);
    dungeonRunAutoCcAbove = static_cast<uint32>(std::min(40, std::max(2,
        config.GetIntDefault("AiPlayerbot.DungeonRunAutoCcAbove", 6))));
    dungeonRunAutoCcRisky = config.GetBoolDefault("AiPlayerbot.DungeonRunAutoCcRisky", false);
    dungeonRunWalkBackAfterWonFight = config.GetBoolDefault("AiPlayerbot.DungeonRunWalkBackAfterWonFight", false);
    dungeonRunRetreatAt2Dead = config.GetBoolDefault("AiPlayerbot.DungeonRunRetreatAt2Dead", false);
    dungeonRunWalkBackRescue = config.GetBoolDefault("AiPlayerbot.DungeonRunWalkBackRescue", true);
    dungeonRunDryWalk = config.GetBoolDefault("AiPlayerbot.DungeonRunDryWalk", false);
    runTeleportReturnYd = std::max(0.0f, config.GetFloatDefault("AiPlayerbot.RunTeleportReturnYd", 0.0f));
    dungeonReviveHoldYd = std::max(0.0f, config.GetFloatDefault("AiPlayerbot.DungeonReviveHoldYd", 0.0f));
    dungeonRunFightForward = config.GetBoolDefault("AiPlayerbot.DungeonRunFightForward", true);
    dungeonRunLightRest = config.GetBoolDefault("AiPlayerbot.DungeonRunLightRest", true);
    dungeonRunLootFfa = config.GetBoolDefault("AiPlayerbot.DungeonRunLootFfa", false);
    dungeonRunPlayerLed = config.GetBoolDefault("AiPlayerbot.DungeonRunPlayerLed", true);
    dungeonRunPlayerLedPullYd = std::min(100.0f, std::max(5.0f, config.GetFloatDefault("AiPlayerbot.DungeonRunPlayerLedPullYd", 40.0f)));
    dungeonRunPlayerLedAwayMinutes = static_cast<uint32>(std::min(60, std::max(1, config.GetIntDefault("AiPlayerbot.DungeonRunPlayerLedAwayMinutes", 10))));
    dungeonRunGiveItems = config.GetBoolDefault("AiPlayerbot.DungeonRunGiveItems", true);
    dungeonRunBotKeysForPlayers = config.GetBoolDefault("AiPlayerbot.DungeonRunBotKeysForPlayers", false);
    botLfgGroups = config.GetBoolDefault("AiPlayerbot.BotLfgGroups", false);
    botLfgFillAfterMinutes = static_cast<uint32>(std::min(60, std::max(1, config.GetIntDefault("AiPlayerbot.BotLfgFillAfterMinutes", 10))));
    botLfgGroupsPer100Bots = static_cast<uint32>(std::min(20, std::max(0, config.GetIntDefault("AiPlayerbot.BotLfgGroupsPer100Bots", 1))));
    botLfgMaxGroups = static_cast<uint32>(std::min(20, std::max(0, config.GetIntDefault("AiPlayerbot.BotLfgMaxGroups", 5))));
    botLfgOnlyWithPlayersOnline = config.GetBoolDefault("AiPlayerbot.BotLfgOnlyWithPlayersOnline", true);
    botLfgChat = config.GetBoolDefault("AiPlayerbot.BotLfgChat", true);
    if (botLfgGroups && !dungeonSelfClear)
        sLog.outString("TortoiseBots: AiPlayerbot.BotLfgGroups = 1 needs AiPlayerbot.DungeonSelfClear = 1: bot LFG groups stay dormant.");
    if (dungeonRunThreatResetWindow && !dungeonRunSeekAura)
        sLog.outString("TortoiseBots: AiPlayerbot.DungeonRunThreatResetWindow = 1 needs AiPlayerbot.DungeonRunSeekAura = 1: the reset clock is the seek fight's charm scan, so the window stays shut.");
}

void PlayerbotAIConfig::LoadObservabilityRaids()
{
    observabilityRaids = config.GetBoolDefault("AiPlayerbot.ObservabilityRaids", false);
    observabilityRaidsDetail = config.GetBoolDefault("AiPlayerbot.ObservabilityRaidsDetail", false);
}

bool PlayerbotAIConfig::ReloadDungeonRunKeys()
{
    if (!enabled)
        return false;
    if (!config.Reload())
    {
        sLog.outError("TortoiseBots: config reload: %s could not be read again; the reloaded keys keep their values.",
            config.GetFilename().c_str());
        return false;
    }
    LoadDungeonRunKeys();
    LoadObservabilityRaids();
    sLog.outString("TortoiseBots: config reload: run leader keys read again from %s (DungeonSelfClear %d, "
        "DungeonRunWalkBackAfterWonFight %d).", config.GetFilename().c_str(), dungeonSelfClear ? 1 : 0,
        dungeonRunWalkBackAfterWonFight ? 1 : 0);
    return true;
}

std::vector<std::pair<std::string, std::string>> PlayerbotAIConfig::FindConfigEntries(std::string const& filter)
{
    auto lower = [](std::string text)
    {
        for (char& c : text)
            c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        return text;
    };
    std::string const wanted = lower(filter);
    std::vector<std::string> keys;
    config.GetKeys("AiPlayerbotConf", keys);
    std::sort(keys.begin(), keys.end());
    std::vector<std::pair<std::string, std::string>> out;
    for (std::string const& key : keys)
    {
        std::string const name = lower(key);
        if (!wanted.empty() && name.find(wanted) == std::string::npos)
            continue;
        bool const secret = name.find("apikey") != std::string::npos || name.find("llmapi") != std::string::npos ||
            name.find("password") != std::string::npos || name.find("secret") != std::string::npos ||
            name.find("token") != std::string::npos;
        out.emplace_back(key, secret ? "***" : config.GetStringDefault(key.c_str(), ""));
    }
    return out;
}

bool PlayerbotAIConfig::Initialize()
{
    sLog.outString("Initializing AI Playerbot by ike3, based on the original Playerbot by blueboy");

    // The path used to be fixed at build time - SYSCONFDIR "aiplayerbot.conf" -
    // so a server run from anywhere other than the prefix it was configured with
    // could not find its bot settings, and the module switched itself off with a
    // message that names a file but not where it looked for it. Three places are
    // tried now, most explicit first: a path given in mangosd.conf, then next to
    // whichever mangosd.conf is actually in use, then the compiled-in default.
    std::string botConfigFile = sConfig.GetStringDefault("AiPlayerbot.ConfigFile", "");

    if (botConfigFile.empty())
    {
        std::string const mainConfig = sConfig.GetFilename();
        size_t const slash = mainConfig.find_last_of("/\\");
        if (slash != std::string::npos)
            botConfigFile = mainConfig.substr(0, slash + 1) + "aiplayerbot.conf";
    }

    if (botConfigFile.empty() || !config.SetSource(botConfigFile.c_str()))
    {
        if (!config.SetSource(_D_AIPLAYERBOT_CONFIG.c_str()))
        {
            sLog.outString("AI Playerbot is Disabled. No configuration file at %s%s%s.",
                botConfigFile.empty() ? "" : botConfigFile.c_str(),
                botConfigFile.empty() ? "" : " or ",
                _D_AIPLAYERBOT_CONFIG.c_str());
            return false;
        }
    }

    sLog.outString("Bot configuration read from %s.", config.GetFilename().c_str());

    enabled = config.GetBoolDefault("AiPlayerbot.Enabled", true);
    if (!enabled)
    {
        sLog.outString("AI Playerbot is Disabled in aiplayerbot.conf");
        return false;
    }

    ConfigAccess configA(config);

    BarGoLink::SetOutputState(config.GetBoolDefault("AiPlayerbot.ShowProgressBars", true));
    globalCoolDown = (uint32) config.GetIntDefault("AiPlayerbot.GlobalCooldown", 1500);
    maxWaitForMove = config.GetIntDefault("AiPlayerbot.MaxWaitForMove", 3000);
    expireActionTime = config.GetIntDefault("AiPlayerbot.ExpireActionTime", 5000);
    dispelAuraDuration = config.GetIntDefault("AiPlayerbot.DispelAuraDuration", 2000);
    reactDelay = (uint32) config.GetIntDefault("AiPlayerbot.ReactDelay", 100);
    passiveDelay = (uint32) config.GetIntDefault("AiPlayerbot.PassiveDelay", 10000);
    repeatDelay = (uint32) config.GetIntDefault("AiPlayerbot.RepeatDelay", 5000);
    errorRepeatDelay = (uint32) config.GetIntDefault("AiPlayerbot.ErrorRepeatDelay", 30000);
    rpgDelay = (uint32) config.GetIntDefault("AiPlayerbot.RpgDelay", 10000);
    sitDelay = (uint32) config.GetIntDefault("AiPlayerbot.SitDelay", 20000);
    returnDelay = (uint32) config.GetIntDefault("AiPlayerbot.ReturnDelay", 7000);
    lootDelay = (uint32)config.GetIntDefault("AiPlayerbot.LootDelayDelay", 1000);

    farDistance = config.GetFloatDefault("AiPlayerbot.FarDistance", 20.0f);
    sightDistance = config.GetFloatDefault("AiPlayerbot.SightDistance", 60.0f);
    spellDistance = config.GetFloatDefault("AiPlayerbot.SpellDistance", 26.0f);
    shootDistance = config.GetFloatDefault("AiPlayerbot.ShootDistance", 26.0f);
    // 125 was three times the reach of any heal in this Vanilla/Tortoise realm, and it fed
    // target selection, the out-of-range trigger and the approach action alike -
    // so a healer sixty yards away believed it was in position, never closed the
    // gap, and every cast failed.
    healDistance = config.GetFloatDefault("AiPlayerbot.HealDistance", 30.0f);
    healDistanceBg = config.GetFloatDefault("AiPlayerbot.HealDistanceBg", 25.0f);
    reactDistance = config.GetFloatDefault("AiPlayerbot.ReactDistance", 150.0f);
    grindDistance = config.GetFloatDefault("AiPlayerbot.GrindDistance", 75.0f);
    aggroDistance = config.GetFloatDefault("AiPlayerbot.AggroDistance", 22.0f);
    lootDistance = config.GetFloatDefault("AiPlayerbot.LootDistance", 25.0f);
    groupMemberLootDistance = config.GetFloatDefault("AiPlayerbot.GroupMemberLootDistance", 15.0f);
    gatheringDistance = config.GetFloatDefault("AiPlayerbot.GatheringDistance", 15.0f);
    groupMemberGatheringDistance = config.GetFloatDefault("AiPlayerbot.GroupMemberGatheringDistance", 10.0f);
    groupMemberGatheringDistanceWithActiveMaster = config.GetFloatDefault("AiPlayerbot.GroupMemberGatheringDistanceWithActiveMaster", 5.0f);
    fleeDistance = config.GetFloatDefault("AiPlayerbot.FleeDistance", 8.0f);
    tooCloseDistance = config.GetFloatDefault("AiPlayerbot.TooCloseDistance", 5.0f);
    meleeDistance = config.GetFloatDefault("AiPlayerbot.MeleeDistance", 1.5f);
    followDistance = config.GetFloatDefault("AiPlayerbot.FollowDistance", 1.5f);
    raidFollowDistance = config.GetFloatDefault("AiPlayerbot.RaidFollowDistance", 4.0f);
    wanderMinDistance = config.GetFloatDefault("AiPlayerbot.WanderMinDistance", 5.0f);
    wanderMaxDistance = config.GetFloatDefault("AiPlayerbot.WanderMaxDistance", 50.0f);
    whisperDistance = config.GetFloatDefault("AiPlayerbot.WhisperDistance", 6000.0f);
    contactDistance = config.GetFloatDefault("AiPlayerbot.ContactDistance", 0.5f);
    aoeRadius = config.GetFloatDefault("AiPlayerbot.AoeRadius", 5.0f);
    rpgDistance = config.GetFloatDefault("AiPlayerbot.RpgDistance", 200.0f);
    proximityDistance = config.GetFloatDefault("AiPlayerbot.ProximityDistance", 20.0f);
    walkDistance = config.GetFloatDefault("AiPlayerbot.WalkDistance", 5.0f);
    EatDrinkMinDistance = config.GetFloatDefault("AiPlayerbot.EatDrinkMinDistance", 5.0f);
    EatDrinkMaxDistance = config.GetFloatDefault("AiPlayerbot.EatDrinkMaxDistance", 1000.0f);

    criticalHealth = config.GetIntDefault("AiPlayerbot.CriticalHealth", 20);
    lowHealth = config.GetIntDefault("AiPlayerbot.LowHealth", 50);
    mediumHealth = config.GetIntDefault("AiPlayerbot.MediumHealth", 70);
    almostFullHealth = config.GetIntDefault("AiPlayerbot.AlmostFullHealth", 90);
    lowMana = config.GetIntDefault("AiPlayerbot.LowMana", 15);
    mediumMana = config.GetIntDefault("AiPlayerbot.MediumMana", 40);

    randomGearMaxLevel = config.GetIntDefault("AiPlayerbot.RandomGearMaxLevel", 100);
    randomGearMaxDiff = config.GetIntDefault("AiPlayerbot.RandomGearMaxDiff", 9);
    randomGearMaxItemLevelAboveLevel = std::max(0, config.GetIntDefault("AiPlayerbot.RandomGearMaxItemLevelAboveLevel", 1));
    hireGearMaxItemLevelAboveLevel = std::max(0, config.GetIntDefault("AiPlayerbot.HireGearMaxItemLevelAboveLevel", 5));
    randomGearMaxSourceTier = config.GetIntDefault("AiPlayerbot.RandomGearMaxSourceTier", 0);
    randomGearAllowReputation = config.GetBoolDefault("AiPlayerbot.RandomGearAllowReputation", false);
    randomGearAllowPvP = config.GetBoolDefault("AiPlayerbot.RandomGearAllowPvP", false);
    randomGearSeedEpicChance = config.GetFloatDefault("AiPlayerbot.RandomGearSeedEpicChance", 0.02f);
    randomGearUpgradeEnabled = config.GetBoolDefault("AiPlayerbot.RandomGearUpgradeEnabled", true);
    randomGearTabards = config.GetBoolDefault("AiPlayerbot.RandomGearTabards", false);
    randomGearTabardsChance = config.GetFloatDefault("AiPlayerbot.RandomGearTabardsChance", 0.1f);
    randomGearTabardsReplaceGuild = config.GetBoolDefault("AiPlayerbot.RandomGearTabardsReplaceGuild", false);
    {
        std::string gearBlacklist = config.GetStringDefault("AiPlayerbot.RandomGearBlacklist", "10,163,19972,58260,21524,21525,7");
        // Older templates shipped this line empty; kept in a server's config
        // it would silently drop the default bans. "0" blocks nothing.
        if (gearBlacklist.find_first_not_of(" \t\"") == std::string::npos)
            gearBlacklist = "10,163,19972,58260,21524,21525,7";
        LoadList<std::list<uint32> >(gearBlacklist, randomGearBlacklist);
    }
    LoadList<std::list<uint32> >(config.GetStringDefault("AiPlayerbot.RandomGearWhitelist", ""), randomGearWhitelist);
    randomGearProgression = config.GetBoolDefault("AiPlayerbot.RandomGearProgression", true);
    randomGearLoweringChance = config.GetFloatDefault("AiPlayerbot.RandomGearLoweringChance", 0.15f);
    rollBadItemsWithPlayer = config.GetBoolDefault("AiPlayerbot.RollBadItemsWithPlayer", false);
    generateItemCaches = config.GetBoolDefault("AiPlayerbot.GenerateItemCaches", true);
    usePotionChance = config.GetFloatDefault("AiPlayerbot.UsePotionChance", 1.0f);
    attackEmoteChance = config.GetFloatDefault("AiPlayerbot.AttackEmoteChance", 0.0f);

    jumpNoCombatChance = config.GetFloatDefault("AiPlayerbot.JumpNoCombatChance", 0.5f);
    jumpMeleeInCombatChance = config.GetFloatDefault("AiPlayerbot.JumpMeleeInCombatChance", 0.5f);
    jumpRandomChance = config.GetFloatDefault("AiPlayerbot.JumpRandomChance", 0.20f);
    jumpInPlaceChance = config.GetFloatDefault("AiPlayerbot.JumpInPlaceChance", 0.50f);
    jumpBackwardChance = config.GetFloatDefault("AiPlayerbot.JumpBackwardChance", 0.10f);
    jumpHeightLimit = config.GetFloatDefault("AiPlayerbot.JumpHeightLimit", 60.f);
    teleportLandingFall = config.GetBoolDefault("AiPlayerbot.TeleportLandingFall", false);
    jumpVSpeed = config.GetFloatDefault("AiPlayerbot.JumpVSpeed", 7.96f);
    jumpHSpeed = config.GetFloatDefault("AiPlayerbot.JumpHSpeed", 7.0f);
    jumpInBg = config.GetBoolDefault("AiPlayerbot.JumpInBg", false);
    jumpWithPlayer = config.GetBoolDefault("AiPlayerbot.JumpWithPlayer", false);
    forceRebuffOnReadyCheck = config.GetBoolDefault("AiPlayerbot.ForceRebuffOnReadyCheck", false);
    jumpFollow = config.GetBoolDefault("AiPlayerbot.JumpFollow", true);
    jumpChase = config.GetBoolDefault("AiPlayerbot.JumpChase", true);
    useKnockback = config.GetBoolDefault("AiPlayerbot.UseKnockback", true);
    LoadList<std::list<uint32> >(config.GetStringDefault("AiPlayerbot.BotAreaTriggers", ""), botAreaTriggerIds);

    iterationsPerTick = config.GetIntDefault("AiPlayerbot.IterationsPerTick", 100);

    // Module-side pool pass budget (PlayerbotAIConfig.h). The floor keeps a
    // typo from starving the pool to a single bot per tick; the ceiling keeps
    // the key from silently disabling itself.
    poolTickBudgetUs = (uint32)config.GetIntDefault("AiPlayerbot.PoolTickBudgetUs", 22000);
    if (poolTickBudgetUs > 0 && poolTickBudgetUs < 1000) poolTickBudgetUs = 1000;
    if (poolTickBudgetUs > 100000) poolTickBudgetUs = 100000;
    poolNearPlayerYd = std::max(0.0f, config.GetFloatDefault("AiPlayerbot.PoolNearPlayerYd", 0.0f));
    poolBudgetWhenTickOverMs = (uint32)config.GetIntDefault("AiPlayerbot.PoolBudgetWhenTickOverMs", 0);
    if (poolBudgetWhenTickOverMs > 10000) poolBudgetWhenTickOverMs = 10000;
    combatTickBudgetUs = (uint32)config.GetIntDefault("AiPlayerbot.CombatTickBudgetUs", 15000);
    if (combatTickBudgetUs > 0 && combatTickBudgetUs < 1000) combatTickBudgetUs = 1000;
    if (combatTickBudgetUs > 100000) combatTickBudgetUs = 100000;
    targetWorldTickMs = (uint32)config.GetIntDefault("AiPlayerbot.TargetWorldTickMs", 100);
    if (targetWorldTickMs > 1000) targetWorldTickMs = 1000;

    valueRefreshAtInterval = config.GetBoolDefault("AiPlayerbot.ValueRefreshAtInterval", false);
    ai::gValueRefreshAtInterval.store(valueRefreshAtInterval, std::memory_order_relaxed);

    // Issue #84: donor Shyalya defaults (base 250ms doubling to 2s cap,
    // 30s TTL, 64 entries). Zero base/max disables the backoff entirely.
    failedActionRetryBaseMs = uint32(std::max(0, std::min(2000, config.GetIntDefault("AiPlayerbot.FailedActionRetryBase", 250))));
    failedActionRetryMaxMs = uint32(std::max(0, std::min(10000, config.GetIntDefault("AiPlayerbot.FailedActionRetryMax", 2000))));
    if (failedActionRetryBaseMs && failedActionRetryMaxMs)
        failedActionRetryMaxMs = std::max(failedActionRetryBaseMs, failedActionRetryMaxMs);
    failedActionCacheTtlMs = uint32(std::max(1000, std::min(300000, config.GetIntDefault("AiPlayerbot.FailedActionCacheTtl", 30000))));
    failedActionCacheMaxEntries = uint32(std::max(1, std::min(256, config.GetIntDefault("AiPlayerbot.FailedActionCacheMaxEntries", 64))));

    LoadList<std::list<uint32> >(config.GetStringDefault("AiPlayerbot.RandomBotQuestItems", "6948,5175,5176,5177,5178,16309,12382,13704,11000,22754"), randomBotQuestItems);
    LoadList<std::list<uint32> >(config.GetStringDefault("AiPlayerbot.RandomBotSpellIds", ""), randomBotSpellIds);
	LoadList<std::list<uint32> >(config.GetStringDefault("AiPlayerbot.PvpProhibitedZoneIds", "2255,656,2361,2362,2363,976,35,2268,3425,392,541,1446,3828,3712,3738,3565,3539,3623,4152,3988,4658,4284,4418,4436,4275,4323"), pvpProhibitedZoneIds);
	LoadList<std::list<uint32> >(config.GetStringDefault("AiPlayerbot.ExtraTownGuardEntries", "5996,5997,5998,5999,6000,6001,6002,6003"), extraTownGuardEntries);
    randomBotForcePvp = config.GetBoolDefault("AiPlayerbot.RandomBotForcePvp", true);
    randomBotForcePvpPercent = std::min<uint32>(config.GetIntDefault("AiPlayerbot.RandomBotForcePvpPercent", 50), 100);
    worldPvpSeek = config.GetIntDefault("AiPlayerbot.WorldPvpSeek", 2);
    worldPvpSeekBotPercent = config.GetIntDefault("AiPlayerbot.WorldPvpSeekBotPercent", 25);
    worldPvpSeekRerollMinutes = config.GetIntDefault("AiPlayerbot.WorldPvpSeekRerollMinutes", 30);


    LoadList<std::list<uint32> >(config.GetStringDefault("AiPlayerbot.ImmuneSpellIds", "19428"), immuneSpellIds);

    randomBotAutologin = config.GetBoolDefault("AiPlayerbot.RandomBotAutologin", true);
    minRandomBots = config.GetIntDefault("AiPlayerbot.MinRandomBots", 500);
    maxRandomBots = config.GetIntDefault("AiPlayerbot.MaxRandomBots", 500);
    randomBotUpdateInterval = config.GetIntDefault("AiPlayerbot.RandomBotUpdateInterval", 500);
    randomBotTimedLogout = config.GetBoolDefault("AiPlayerbot.RandomBotTimedLogout", true);
    levelLadder = config.GetBoolDefault("AiPlayerbot.LevelLadder", true);
    levelLadderBandSize = config.GetIntDefault("AiPlayerbot.LevelLadderBandSize", 5);
    if (levelLadderBandSize < 1) levelLadderBandSize = 1;
    if (levelLadderBandSize > 59) levelLadderBandSize = 59;
    levelLadderMaxLevelSharePct = config.GetIntDefault("AiPlayerbot.LevelLadderMaxLevelShare", 10);
    if (levelLadderMaxLevelSharePct > 100) levelLadderMaxLevelSharePct = 100;
    levelLadderLogMinutes = config.GetIntDefault("AiPlayerbot.LevelLadderLogMinutes", 5);
    maxRandomBotInWorldTime = config.GetIntDefault("AiPlayerbot.MaxRandomBotInWorldTime", 6 * 3600);
    randomBotStallRelogMinutes = config.GetIntDefault("AiPlayerbot.RandomBotStallRelogMinutes", 45);
    minRandomBotRandomizeTime = config.GetIntDefault("AiPlayerbot.MinRandomBotRandomizeTime", 6 * 3600);
    maxRandomBotRandomizeTime = config.GetIntDefault("AiPlayerbot.MaxRandomBotRandomizeTime",
        config.GetIntDefault("AiPlayerbot.MaxRandomRandomizeTime", 24 * 3600));
    minRandomBotChangeStrategyTime = config.GetIntDefault("AiPlayerbot.MinRandomBotChangeStrategyTime", 1800);
    maxRandomBotChangeStrategyTime = config.GetIntDefault("AiPlayerbot.MaxRandomBotChangeStrategyTime", 2 * 3600);

    // Comma separated character names matched using the database's stored
    // `characters.name` collation (not the teleport facade's normalized
    // comparison). A pinned bot is kept logged in and is exempt from timed
    // logout (native login teleport is skipped separately via the facade's
    // normalized name match, best-effort) but still gated by
    // RandomBotLoginWithPlayer=1, so its run can be followed from one level
    // to the next when the pool is active.
    {
        std::string names = config.GetStringDefault("AiPlayerbot.PinnedBots", "");
        std::stringstream ss(names);
        std::string name;
        while (std::getline(ss, name, ','))
        {
            size_t b = name.find_first_not_of(" 	");
            size_t e = name.find_last_not_of(" 	");
            if (b != std::string::npos)
                pinnedBotNames.push_back(name.substr(b, e - b + 1));
        }
    }
    enableMinimalMove = config.GetBoolDefault("AiPlayerbot.EnableMinimalMove", true);

    transportTeleportType = config.GetIntDefault("AiPlayerbot.TransportTeleportType", 2);
    randomBotsMaxLoginsPerInterval = config.GetIntDefault("AiPlayerbot.RandomBotsMaxLoginsPerInterval", 30);
    maxRandomBotsPriceChangeInterval = config.GetIntDefault("AiPlayerbot.MaxRandomBotsPriceChangeInterval", 48 * 3600);
    //Auction house settings
    shouldQueryAHListingsOutsideOfAH = config.GetBoolDefault("AiPlayerbot.ShouldQueryAHListingsOutsideOfAH", true);
    LoadList<std::list<uint32> >(config.GetStringDefault("AiPlayerbot.AhOverVendorItemIds", ""), ahOverVendorItemIds);
    LoadList<std::list<uint32> >(config.GetStringDefault("AiPlayerbot.VendorOverAHItemIds", ""), vendorOverAHItemIds);
    LoadList<std::list<uint32> >(config.GetStringDefault("AiPlayerbot.AhSellBlacklist", "18256"), ahSellBlacklist);
    botCheckAllAuctionListings = config.GetBoolDefault("AiPlayerbot.BotCheckAllAuctionListings", false);
    botsSaveEpics = config.GetBoolDefault("AiPlayerbot.BotsSaveEpics", true);
    auctionPriceRefreshInterval = (uint32)config.GetIntDefault("AiPlayerbot.AuctionPriceRefreshInterval", 60);
    if (auctionPriceRefreshInterval < 5) auctionPriceRefreshInterval = 5;
    if (auctionPriceRefreshInterval > 3600) auctionPriceRefreshInterval = 3600;
    // Default-off bounded AH market population. Interval is seconds, batch is
    // max auctions per tick (hard capped at 5 in service). No AH scan or DB
    // query per tick; uses legitimate inventory + native HandleAuctionSellItem.
    ahMarketEnabled = config.GetBoolDefault("AiPlayerbot.AhMarketEnabled", true);
    ahMarketInterval = (uint32)config.GetIntDefault("AiPlayerbot.AhMarketInterval", 120);
    if (ahMarketInterval < 5) ahMarketInterval = 5;
    if (ahMarketInterval > 3600) ahMarketInterval = 3600;
    ahMarketBatchSize = (uint32)config.GetIntDefault("AiPlayerbot.AhMarketBatchSize", 1);
    if (ahMarketBatchSize > 5) ahMarketBatchSize = 5;
    // Synthetic AH Supply and Buyer Engine with Work Budgets (Issue #88)
    ahMarketSyntheticSupply = config.GetBoolDefault("AiPlayerbot.AhMarketSyntheticSupply", false);
    ahMarketBuyer = config.GetBoolDefault("AiPlayerbot.AhMarketBuyer", true);
    ahMarketBudgetUs = (uint32)config.GetIntDefault("AiPlayerbot.AhMarketBudgetUs", 2000);
    if (ahMarketBudgetUs < 100) ahMarketBudgetUs = 100;
    if (ahMarketBudgetUs > 20000) ahMarketBudgetUs = 20000;
    ahMarketMaxOperations = (uint32)config.GetIntDefault("AiPlayerbot.AhMarketMaxOperations", 32);
    if (ahMarketMaxOperations < 1) ahMarketMaxOperations = 1;
    if (ahMarketMaxOperations > 256) ahMarketMaxOperations = 256;
    ahMarketChanceSell = (uint32)config.GetIntDefault("AiPlayerbot.AhMarketChanceSell", 10);
    if (ahMarketChanceSell > 100) ahMarketChanceSell = 100;
    ahMarketChanceBuy = (uint32)config.GetIntDefault("AiPlayerbot.AhMarketChanceBuy", 10);
    if (ahMarketChanceBuy > 100) ahMarketChanceBuy = 100;
    ahMarketBuyValue = (uint32)config.GetIntDefault("AiPlayerbot.AhMarketBuyValue", 80);
    if (ahMarketBuyValue > 200) ahMarketBuyValue = 200;
    ahMarketMaxSpendPerBot = (uint32)config.GetIntDefault("AiPlayerbot.AhMarketMaxSpendPerBot", 0);
    ahMarketMaxQuality = (uint32)config.GetIntDefault("AiPlayerbot.AhMarketMaxQuality", 4);
    if (ahMarketMaxQuality > 6) ahMarketMaxQuality = 6;
    ahMarketMaxLevel = (uint32)config.GetIntDefault("AiPlayerbot.AhMarketMaxLevel", 60);
    if (ahMarketMaxLevel < 1) ahMarketMaxLevel = 1;
    if (ahMarketMaxLevel > 60) ahMarketMaxLevel = 60;
    ahMarketDynamicLevel = config.GetBoolDefault("AiPlayerbot.AhMarketDynamicLevel", false);
    ahMarketLevelRefresh = (uint32)config.GetIntDefault("AiPlayerbot.AhMarketLevelRefresh", 600);
    if (ahMarketLevelRefresh < 60) ahMarketLevelRefresh = 60;
    if (ahMarketLevelRefresh > 86400) ahMarketLevelRefresh = 86400;
    ahMarketVariance = (uint32)config.GetIntDefault("AiPlayerbot.AhMarketVariance", 10);
    if (ahMarketVariance > 100) ahMarketVariance = 100;
    ahMarketBidMin = (uint32)config.GetIntDefault("AiPlayerbot.AhMarketBidMin", 75);
    if (ahMarketBidMin < 1) ahMarketBidMin = 1;
    if (ahMarketBidMin > 100) ahMarketBidMin = 100;
    ahMarketBidMax = (uint32)config.GetIntDefault("AiPlayerbot.AhMarketBidMax", 90);
    if (ahMarketBidMax < 1) ahMarketBidMax = 1;
    if (ahMarketBidMax > 100) ahMarketBidMax = 100;
    if (ahMarketBidMin > ahMarketBidMax) ahMarketBidMin = ahMarketBidMax;
    ahMarketTimeMin = (uint32)config.GetIntDefault("AiPlayerbot.AhMarketTimeMin", 8);
    if (ahMarketTimeMin < 1) ahMarketTimeMin = 1;
    if (ahMarketTimeMin > 72) ahMarketTimeMin = 72;
    ahMarketTimeMax = (uint32)config.GetIntDefault("AiPlayerbot.AhMarketTimeMax", 24);
    if (ahMarketTimeMax < 1) ahMarketTimeMax = 1;
    if (ahMarketTimeMax > 72) ahMarketTimeMax = 72;
    if (ahMarketTimeMin > ahMarketTimeMax) ahMarketTimeMin = ahMarketTimeMax;
    //
    // AhBot pool
    ahBotListedItemLevelRestrictEnabled = config.GetBoolDefault("AiPlayerbot.AhBot.ListedItemLevelRestrict.Enabled", false);
    ahBotListedItemLevelRestrictUseCrafted = config.GetBoolDefault("AiPlayerbot.AhBot.ListedItemLevelRestrict.UseCraftedItemForCalculation", true);
    ahBotListedItemLevelMin = (uint32)config.GetIntDefault("AiPlayerbot.AhBot.ListedItemLevelRestrict.MinItemLevel", 0);
    ahBotListedItemLevelMax = (uint32)config.GetIntDefault("AiPlayerbot.AhBot.ListedItemLevelRestrict.MaxItemLevel", 999);
    ahBotListedItemLevelExceptionItemIds = config.GetStringDefault("AiPlayerbot.AhBot.ListedItemLevelRestrict.ExceptionItemIDs", "");
    ahBotListedItemIdRestrictEnabled = config.GetBoolDefault("AiPlayerbot.AhBot.ListedItemIDRestrict.Enabled", false);
    ahBotListedItemIdMin = (uint32)config.GetIntDefault("AiPlayerbot.AhBot.ListedItemIDRestrict.MinItemID", 0);
    ahBotListedItemIdMax = (uint32)config.GetIntDefault("AiPlayerbot.AhBot.ListedItemIDRestrict.MaxItemID", 200000);
    ahBotListedItemIdExceptionItemIds = config.GetStringDefault("AiPlayerbot.AhBot.ListedItemIDRestrict.ExceptionItemIDs", "");
    ahBotEquipLevelRestrictEnabled = config.GetBoolDefault("AiPlayerbot.AhBot.EquipItemUseOrEquipLevelRestrict.Enabled", true);
    ahBotEquipLevelMin = (uint32)config.GetIntDefault("AiPlayerbot.AhBot.EquipItemUseOrEquipLevelRestrict.MinLevel", 0);
    ahBotEquipLevelMax = (uint32)config.GetIntDefault("AiPlayerbot.AhBot.EquipItemUseOrEquipLevelRestrict.MaxLevel", 60);
    ahBotEquipLevelExceptionItemIds = config.GetStringDefault("AiPlayerbot.AhBot.EquipItemUseOrEquipLevelRestrict.ExceptionItemIDs", "");
    ahBotDisabledItemTextFilter = config.GetBoolDefault("AiPlayerbot.AhBot.DisabledItemTextFilter", true);
    ahBotDisabledRecipeProducedItemFilterEnabled = config.GetBoolDefault("AiPlayerbot.AhBot.DisabledRecipeProducedItemFilterEnabled", false);
    ahBotDisabledRecipeProducedItemClassSubClasses = config.GetStringDefault("AiPlayerbot.AhBot.DisabledRecipeProducedItemClassSubClasses", "2:*,4:*,15:2,15:5");
    ahBotDisabledInvalidItemIds = config.GetStringDefault("AiPlayerbot.AhBot.DisabledInvalidItemIDs",
        "17,37,88,91,100,119,128,138,143,156,734,763,813,862,876,895,896,900,913,931,956,958,960,964,996,997,"
        "1014,1020,1021,1024,1025,1027,1028,1029,1041,1057,1113,1114,1133,1134,1162,1166,1167,1189,1192,1193,"
        "1222,1255,1259,1298,1324,1352,1356,1389,1402,1450,1487,1623,1672,1902,1911,1914,1923,1977,2016,2055,"
        "2064,2081,2128,2133,2136,2184,2273,2275,2288,2305,2306,2320,2321,2324,2325,2377,2410,2413,2415,2441,"
        "2442,2443,2444,2477,2481,2482,2483,2484,2485,2486,2487,2496,2497,2498,2499,2500,2501,2502,2503,2550,"
        "2556,2588,2599,2604,2605,2638,2664,2665,2668,2678,2688,2692,2693,2705,2715,2755,2810,2880,2891-2893,"
        "2918,2920,2921,2922,2927,2929,2931,2932,2946,2947,2995,3003,3004,3005,3031,3034,3046,3052,3062,3063,"
        "3068,3107,3111,3122,3131,3135,3137,3144,3148,3168,3222,3245,3260,3316,3320,3368,3371,3372,3466,3513,"
        "3516,3536,3584,3648,3675,3686,3707,3713,3744,3772,3774,3775,3776,3777,3857,3861,3884,3895,3934,3952,"
        "3953,3954,3955,3956,3957,3958,3959,3977,3978,3979,3980,3981,3982,3983,3984,3988,3991,4008,4009,4010,"
        "4011,4012,4013,4014,4015,4143,4191,4193,4196,4200,4273,4289,4291,4340,4341,4342,4399,4400,4470,4471,"
        "4524,4603,4688,4703,4749,4761,4763,4773,4774,4868,4912,4959,5004,5005,5008,5010,5013,5024,5043,5044,"
        "5049,5056,5091,5105,5106,5108,5150,5184,5220,5229,5235,5255,5259,5283,5330,5333,5349,5350,5353,5362,"
        "5363,5364,5367,5370,5371,5377,5379,5389,5400,5417,5418,5435,5455,5468,5495,5509,5510,5511,5512,5515,"
        "5517,5518,5523,5531,5549,5550,5555,5560,5562,5563,5577,5600,5625,5632,5639,5641,5645,5646,5654,5657,"
        "5660,5663,5670,5681,5748,5823,5828,5845,5859,5874,5875,5878,5968,6130,6131,6150,6174,6182,6183,6216,"
        "6222,6225,6227,6232,6260,6261,6273,6276,6277,6278,6279,6280,6289,6291,6292,6294,6295,6297,6301,6303,"
        "6307,6308,6309,6310,6311,6317,6343,6345,6351,6352,6353,6354,6355,6356,6357,6358,6360,6361,6362,6363,"
        "6364,6366,6374,6376,6435,6455,6490,6491,6492,6495,6496,6497,6498,6500,6501,6516,6544,6589,6623,6643,"
        "6645,6647,6648,6649,6650,6651,6698,6707,6708,6711,6715,6717,6718,6724,6728,6734,6736,6834,6891,6927,"
        "6949-6951,6988,7093,7134,7135,7170,7187,7188,7190,7192,7206,7208,7268,7271,7286,7287,7333,7392,7426,"
        "7427,7428,7466,7467,7497,7547,7548,7550,7678,7679,7680,7681,7733,7737,7769,7770,7771,7807,7808,7867,"
        "7923,7973,7977,8072,8075,8076,8077,8078,8079,8147,8164,8171,8343,8350,8365,8366,8368,8383,8388,8425,"
        "8426,8427,8546,8583,8547,8585,8589,8590,8627,8628,8630,8633,8756-8765,8767-8826,8828-8830,8832-8835,"
        "8837,8840-8844,8847-8922,8925-8928,8929-8931,8933-8947,8954,8955,8958,8959,8960-8972,8974-8984,"
        "8985-9029,9031-9035,9037-9059,9062-9087,9089-9143,9145-9148,9150-9152,9156-9171,9174-9178,9180-9185,"
        "9188,9190-9196,9198-9205,9207-9209,9211-9223,9225-9233,9239,9254,9280,9281,9282,9284,9311,9316,9319,"
        "9325,9330,9365,9421,9438,9440,9441,9443,9484,9593,9594,9595,9596,9597,9700,9701,9718,9888,10290,"
        "10303,10304,10313,10319,10322,10324,10450,10457,10464,10579,10580,10595,10647,10648,10662,10691,"
        "10692,10693,10694,10918,10920-10922,11111,11131,11149,11170,11222,11230,11264,11270,11282,11283,"
        "11291,11413,11470,11507,11511,11602,11609,11613,11616,11903,11947,11949,11954,12238,12241,12258,"
        "12263,12347,12348,12349,12468,12567,12615,12616,12617,12648,12649,12723,12731,12787,12816,12817,"
        "12826,12831,12832,12847,12866,12885,12904,12943,12947,12991,13155,13159,13316,13370,13422,13477,"
        "13480,13500,13612,13673,13754,13755,13756,13757,13758,13759,13760,13875,13876,13877,13878,13879,"
        "13880,13881,13882,13883,13884,13885,13886,13887,13888,13889,13890,13891,13893,13901,13902,13903,"
        "13904,13905,13906,13907,13908,13910,13911,13912,13914,13915,13916,13917,13918,14062,14083,14339,"
        "14341,14389,14390,14392,14481,14488,14550,14586,14645,14891,15326,15327,15409,15410,15415,15417,"
        "15419,15422,15423,15448,15756,15769,15843,15845,16026,16041,16042,16047,16072,16073,16082,"
        "16083-16085,16165,16171,16180,16339,16664,16792,16967,16968,16969,16970,16971,16973,16999,17012,"
        "17024,17027,17195,17242,17262,17302,17305,17308,17323,17325,17362,17363,17696,17758,17882,17887,"
        "17967,17968,18002,18151,18153,18154,18235,18256,18341,18342,18492,18540,18566,18567,18599,18636,"
        "18642,18643,18651,18964,19004,19005,19006,19007,19008,19009,19010,19011,19012,19013,19296,19297,"
        "19298,19642,19725,19775,19808,19880,19882,19924,19960,20018,20020,20021,20030,20337,20364,20381,"
        "20404,20416,20418,20419,20420,20432,20433,20435,20436,20447,20448,20449,20450,20454,20455,20456,"
        "20498,20500,20501,20541,20545,20552,20591,20596,20676-20679,20696,20698,20708,20709,20720,20721,"
        "20722,20815,20819,20822,20824,20825,20829,20834,20844,20902,20903,20904,20913,20952,20953,20957,"
        "20962,20965,20977,20979,20981,20984,21043,21071,21113,21114,21135,21140,21141,21150,21153,21162,"
        "21164,21168,21171,21228,21243,21281,21282,21283,21293,21302,21442,21536,21560,21577,21578,21591,"
        "21772,21773,21785,21786,21816,21817,21818,21819,21820,21821,21822,21823,21831,21835,21878,21927,"
        "21975,21979-21981,21992,22012,22018,22019,22020,22042,22045,22053,22054,22058,22103,22104,22105,"
        "21369,22140,22141,22142,22143,22144,22145,22154,22155,22156,22157,22158,22159,22160,22161,22162,"
        "22163,22164,22165,22166,22167,22168,22169,22170,22171,22172,22178,22202,22262,22263,22283,22284,"
        "22285,22286,22287,22288,22289,22290,22291,22292,22293,22294,22295,22296,22297,22298,22299,22300,"
        "22373,22374,22375,22376,22386,22387,22584,22596,22709,22710,22733,22782,22788,22795,22797,22822,"
        "22891,22895,22896,22899,23003,23055,23076,23131,23135,23137,23140,23141,23144,23147,23148,23151,"
        "23152,23153,23157,23214,23227,23233,23234,23235,23248,23330,23340,23341,23342,23350,23352,23355,"
        "23360,23362-23364,23366,23378,23471,23486,23552,23567,23578,23579,23670,23683,23684,23686,"
        "23725-23728,23750,23754,23773,23840,23858,23866,23867,23868,23878,23879,23880,23885,23952,23980,"
        "24071,24100,24156,24188,24190,24226,24234,24235,24242,24243,24283");
    ahBotDisabledCustomItemIds = config.GetStringDefault("AiPlayerbot.AhBot.DisabledCustomItemIDs", "");
    for (uint32 category = 0; category < ai::kAhBotMaxItemClass; ++category)
    {
        char const* catName = ai::AhBotCategoryName(category);
        for (uint32 quality = 0; quality < ai::kAhBotMaxItemQuality; ++quality)
        {
            char const* qualName = ai::AhBotQualityName(quality);
            if (!catName || !qualName)
            {
                ahBotListProportions[category][quality] = 0;
                continue;
            }
            std::string const key = std::string("AiPlayerbot.AhBot.ListProportion.Category") + catName + ".Quality" + qualName;
            uint32 const defProp = ai::AhBotDefaultListProportion(category, quality);
            int32 const rawProp = config.GetIntDefault(key.c_str(), static_cast<int32>(defProp));
            ahBotListProportions[category][quality] = rawProp > 0 ? static_cast<uint32>(rawProp) : 0;
        }
    }
    ahBotListMultipliedItemIds = config.GetStringDefault("AiPlayerbot.AhBot.ListProportion.ListMultipliedItemIDs",
        "2589:10,2592:10,4306:10,4338:10,14047:10,2318:10,2319:10,4234:10,4304:10,8170:10,2770:10,2771:10,"
        "2772:10,2775:10,2776:10,3858:10,7911:10,10620:10,2835:10,2836:10,2838:10,12365:10,7912:10,765:10,"
        "785:10,2447:10,2449:10,2450:10,2452:10,2453:10,3355:10,3356:10,3357:10,3358:10,3369:10,3818:10,"
        "3819:10,3820:10,3821:10,4625:10,8153:10,8831:10,8836:10,8838:10,8839:10,8845:10,8846:10,13463:10,"
        "13464:10,13465:10,13466:10,13467:10,13468:10,10938:10,10939:10,10940:10,10978:10,10998:10,11082:10,"
        "11084:10,11128:10,11134:10,11135:10,11138:10,11139:10,11174:10,11175:10,11177:10,11178:10,14343:10,"
        "16202:10,16203:10,16204:10,774:10,818:10,1206:10,1210:10,1529:10,1705:10,3864:10,7909:10,7910:10,"
        "12361:10,12364:10,12799:10,12800:10,118:5,858:5,929:5,2455:5,2456:5,3827:5,3928:5,6149:5,13443:5,"
        "13446:5,18253:5");
    logInGroupOnly = config.GetBoolDefault("AiPlayerbot.LogInGroupOnly", true);
    logValuesPerTick = config.GetBoolDefault("AiPlayerbot.LogValuesPerTick", false);
    fleeingEnabled = config.GetBoolDefault("AiPlayerbot.FleeingEnabled", true);
    poolMeleeHoldGround = config.GetBoolDefault("AiPlayerbot.PoolMeleeHoldGround", false);
    altBots = config.GetBoolDefault("AiPlayerbot.AltBots", false);
    altBotsLogoutWithOwner = config.GetBoolDefault("AiPlayerbot.AltBotsLogoutWithOwner", false);
    bombRunoutDistance = config.GetFloatDefault("AiPlayerbot.BombRunoutDistance", 30.0f);
    hazardEvasionDistance = config.GetFloatDefault("AiPlayerbot.HazardEvasionDistance", 12.0f);
    enableCustomRaidTactics = config.GetBoolDefault("AiPlayerbot.EnableCustomRaidTactics", true);
    summonAtInnkeepersEnabled = config.GetBoolDefault("AiPlayerbot.SummonAtInnkeepersEnabled", true);
    randomBotMaxLevel = config.GetIntDefault("AiPlayerbot.RandomBotMaxLevel", 60);
    randomBotLoginAtStartup = config.GetBoolDefault("AiPlayerbot.RandomBotLoginAtStartup", true);
    randomBotAutoCreate = config.GetBoolDefault("AiPlayerbot.RandomBotAutoCreate", true);
    enableRandomTeleports = config.GetBoolDefault("AiPlayerbot.EnableRandomTeleports", true);
    randomBotEvenStartZones = config.GetBoolDefault("AiPlayerbot.RandomBotEvenStartZones", true);
    int factionRatio = config.GetIntDefault("AiPlayerbot.RandomBotFactionRatio", 50);
    randomBotFactionRatio = uint32(factionRatio < 0 ? 0 : (factionRatio > 100 ? 100 : factionRatio));
    if (factionRatio < 0 || factionRatio > 100)
        sLog.outError("AiPlayerbot.RandomBotFactionRatio %d is out of range (0-100), clamping to %u", factionRatio, randomBotFactionRatio);
    randomBotStartZoneWeightsText = config.GetStringDefault("AiPlayerbot.RandomBotStartZoneWeights", "");
    randomBotStartZoneWeightsValid = false;
    for (uint32& weight : randomBotStartZoneWeights)
        weight = 0;
    uint32_t zoneWeights[TortoiseBots::kStartZoneCount] = {};
    if (!TortoiseBots::ParseStartZoneWeights(randomBotStartZoneWeightsText.c_str(), zoneWeights))
        sLog.outError("AiPlayerbot.RandomBotStartZoneWeights '%s' is malformed, ignoring it and keeping the even start-zone spread (e.g. \"Durotar=100,Elwynn=50\")",
            randomBotStartZoneWeightsText.c_str());
    else
    {
        for (uint32 i = 0; i < TortoiseBots::kStartZoneCount; ++i)
            randomBotStartZoneWeights[i] = zoneWeights[i];
        randomBotStartZoneWeightsValid = true;
    }
    std::string nameWeights = config.GetStringDefault("AiPlayerbot.RandomBotNameStyleWeights", "55,20,25");
    if (!TortoiseBots::ParseBotNameStyleWeights(nameWeights, randomBotNameStyleWeights))
        sLog.outError("AiPlayerbot.RandomBotNameStyleWeights '%s' is malformed, keeping %u,%u,%u (three numbers: race-flavoured,compound,fun)",
            nameWeights.c_str(), randomBotNameStyleWeights[0], randomBotNameStyleWeights[1], randomBotNameStyleWeights[2]);
    randomBotNamesFile = config.GetStringDefault("AiPlayerbot.RandomBotNamesFile", "");
    allowIsolatedCustomStartingZones = config.GetBoolDefault("AiPlayerbot.AllowIsolatedCustomStartingZones", false);
    relocateHopelessDeaths = config.GetBoolDefault("AiPlayerbot.RelocateHopelessDeaths", true);
    abandonedInstanceGraceSec = static_cast<uint32>(std::max(0, config.GetIntDefault("AiPlayerbot.AbandonedInstanceGraceSec", 90)));
    repopAtGraveyard = config.GetBoolDefault("AiPlayerbot.RepopAtGraveyard", true);
    turtleMountAtLevel = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.TurtleMountAtLevel", 18));
    levelUpMounts = config.GetBoolDefault("AiPlayerbot.LevelUpMounts", true);
    randomBotShopMounts = config.GetBoolDefault("AiPlayerbot.RandomBotShopMounts", false);
    poolGearTopUpOnLevel = config.GetBoolDefault("AiPlayerbot.PoolGearTopUpOnLevel", false);
    poolMoneyTopUp = config.GetBoolDefault("AiPlayerbot.PoolMoneyTopUp", false);
    mountBreakEvenFactor = config.GetFloatDefault("AiPlayerbot.MountBreakEvenFactor", 1.5f);
    avoidHostileTowns = config.GetBoolDefault("AiPlayerbot.AvoidHostileTowns", true);
    unstuckHearthLevelFit = config.GetBoolDefault("AiPlayerbot.UnstuckHearthLevelFit", true);
    unstuckHearthMinDistance = config.GetFloatDefault("AiPlayerbot.UnstuckHearthMinDistance", 300.0f);
    stuckTravelTeleport = config.GetBoolDefault("AiPlayerbot.StuckTravelTeleport", true);
    guildOrderAuctionVisits = static_cast<uint32_t>(config.GetIntDefault("AiPlayerbot.GuildOrderAuctionVisits", 3));
    botQuestLogUpkeep = config.GetBoolDefault("AiPlayerbot.BotQuestLogUpkeep", true);
    leaveOutgrownZones = config.GetBoolDefault("AiPlayerbot.LeaveOutgrownZones", true);
    vendorBatchMinCount = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.VendorBatchMinCount", 8));
    vendorBatchMinBagSpace = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.VendorBatchMinBagSpace", 60));
    lowLevelVendorBatchMinCount = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.LowLevelVendorBatchMinCount", 3));
    lowLevelVendorBatchMinBagSpace = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.LowLevelVendorBatchMinBagSpace", 25));
    lowLevelVendorMaxDistance = config.GetFloatDefault("AiPlayerbot.LowLevelVendorMaxDistance", 600.0f);
    travelPickBudgetUs = (uint32)config.GetIntDefault("AiPlayerbot.TravelPickBudgetUs", 4000);
    botLootRateUncommon = config.GetFloatDefault("AiPlayerbot.BotLootRateUncommon", 1.0f);
    botLootRateRare = config.GetFloatDefault("AiPlayerbot.BotLootRateRare", 1.0f);
    randomBotLftEnabled = config.GetBoolDefault("AiPlayerbot.RandomBotLftEnabled", true);
    randomBotLftUpdateInterval = config.GetIntDefault("AiPlayerbot.RandomBotLftUpdateInterval", 15000);
    randomBotLftMaxFillsPerInterval = config.GetIntDefault("AiPlayerbot.RandomBotLftMaxFillsPerInterval", 1);
    randomBotLftAllowRoleBorrow = config.GetBoolDefault("AiPlayerbot.RandomBotLftAllowRoleBorrow", false);
    // Issue #192: on-demand companion hiring (see PlayerbotAIConfig.h). All
    // bounds keep the level-scaled copper math inside 32 bits (multiplier x
    // base x level/60 can never overflow a uint32 at sane values).
    hireEnabled = config.GetBoolDefault("AiPlayerbot.HireEnabled", true);
    hireMinAccountSecurity = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.HireMinAccountSecurity", 0));
    if (hireMinAccountSecurity > 6)
        hireMinAccountSecurity = 6;
    hireMaxBotsPerPlayer = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.HireMaxBotsPerPlayer", 4));
    // 39 stays the ceiling: the 40th seat of a bound raid is the commander's alone (HireMaxCompanions in HireRaidPolicy.h).
    if (hireMaxBotsPerPlayer > 39)
        hireMaxBotsPerPlayer = 39;
    // Read signed: a negative value must mean 0, not wrap to a huge allowance.
    hireMaxLevelAboveHirer = TortoiseBots::ClampHireLevelAllowance(
        static_cast<int64_t>(config.GetIntDefault("AiPlayerbot.HireMaxLevelAboveHirer", 0)));
    // Read signed: a negative value must mean 0 (off), not wrap to a huge level.
    hireFreeMinSecurity = TortoiseBots::ClampHireFreeMinSecurity(
        static_cast<int64_t>(config.GetIntDefault("AiPlayerbot.HireFreeMinSecurity", 0)));
    hireAnywhere = config.GetBoolDefault("AiPlayerbot.HireAnywhere", false);
    hireAllowPvP = config.GetBoolDefault("AiPlayerbot.HireAllowPvP", false);
    hireCrossFactionNeutralInns = config.GetBoolDefault("AiPlayerbot.HireCrossFactionNeutralInns", false);
    hireLoot = config.GetBoolDefault("AiPlayerbot.HireLoot", false);
    shamanTankEnabled = config.GetBoolDefault("AiPlayerbot.ShamanTankEnabled", true);
    campEnabled = config.GetBoolDefault("AiPlayerbot.CampEnabled", true);
    campChancePct = (uint32) config.GetIntDefault("AiPlayerbot.CampChancePct", 15);
    campNightChancePct = (uint32) config.GetIntDefault("AiPlayerbot.CampNightChancePct", 30);
    campMinLevel = (uint32) config.GetIntDefault("AiPlayerbot.CampMinLevel", 10);
    campSitDurationSec = (uint32) config.GetIntDefault("AiPlayerbot.CampSitDurationSec", 1200);
    campKeepRestedXp = config.GetBoolDefault("AiPlayerbot.CampKeepRestedXp", false);
    campJoinChancePct = (uint32) config.GetIntDefault("AiPlayerbot.CampJoinChancePct", 10);
    campJoinMinSec = (uint32) config.GetIntDefault("AiPlayerbot.CampJoinMinSec", 60);
    campJoinMaxSec = (uint32) config.GetIntDefault("AiPlayerbot.CampJoinMaxSec", 180);
    campGreetChancePct = (uint32) config.GetIntDefault("AiPlayerbot.CampGreetChancePct", 15);
    campGreetCooldownSec = (uint32) config.GetIntDefault("AiPlayerbot.CampGreetCooldownSec", 120);
    hireBaseCostCopper = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.HireBaseCostCopper", 15000));
    if (hireBaseCostCopper > 100000000)
        hireBaseCostCopper = 100000000;
    hirePartyMult2 = config.GetFloatDefault("AiPlayerbot.HirePartyMult2", 1.66f);
    if (hirePartyMult2 < 0.0f)
        hirePartyMult2 = 0.0f;
    if (hirePartyMult2 > 100.0f)
        hirePartyMult2 = 100.0f;
    hirePartyMult3 = config.GetFloatDefault("AiPlayerbot.HirePartyMult3", 2.66f);
    if (hirePartyMult3 < 0.0f)
        hirePartyMult3 = 0.0f;
    if (hirePartyMult3 > 100.0f)
        hirePartyMult3 = 100.0f;
    hirePartyMult4 = config.GetFloatDefault("AiPlayerbot.HirePartyMult4", 4.66f);
    if (hirePartyMult4 < 0.0f)
        hirePartyMult4 = 0.0f;
    if (hirePartyMult4 > 100.0f)
        hirePartyMult4 = 100.0f;
    hireRaidFlatCostCopper = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.HireRaidFlatCostCopper", 10000));
    if (hireRaidFlatCostCopper > 100000000)
        hireRaidFlatCostCopper = 100000000;
    hireDisconnectGracePeriod = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.HireDisconnectGracePeriod", 300));
    if (hireDisconnectGracePeriod > 3600)
        hireDisconnectGracePeriod = 3600;
    // Issue #900: `.bot hire raid` class spread and presets. A malformed value
    // falls back to the shipped default (fallbacks equal the template).
    hireRaidSpread = config.GetStringDefault("AiPlayerbot.HireRaidSpread", TortoiseBots::kHireRaidSpreadDefault);
    {
        TortoiseBots::HireRaidSpread probe;
        if (!TortoiseBots::ParseHireRaidSpread(hireRaidSpread, probe))
        {
            sLog.outError("AiPlayerbot.HireRaidSpread '%s' is malformed (tank=..;healer=..;dps=.. with class names); using the default", hireRaidSpread.c_str());
            hireRaidSpread = TortoiseBots::kHireRaidSpreadDefault;
        }
    }
    hireRaidPresets = config.GetStringDefault("AiPlayerbot.HireRaidPresets", TortoiseBots::kHireRaidPresetsDefault);
    {
        std::vector<TortoiseBots::HireRaidPreset> probe;
        if (!TortoiseBots::ParseHireRaidPresets(hireRaidPresets, probe))
        {
            sLog.outError("AiPlayerbot.HireRaidPresets '%s' is malformed (name=composition;name=composition); using the default", hireRaidPresets.c_str());
            hireRaidPresets = TortoiseBots::kHireRaidPresetsDefault;
        }
    }
    botGearPresets = config.GetStringDefault("AiPlayerbot.BotGearPresets", TortoiseBots::BotGear::kPresetsDefault);
    if (TortoiseBots::BotGear::ParsePresets(botGearPresets).empty())
    {
        sLog.outError("AiPlayerbot.BotGearPresets '%s' is malformed (name=t<tier> [ilvl=<cap>];...); using the default",
            botGearPresets.c_str());
        botGearPresets = TortoiseBots::BotGear::kPresetsDefault;
    }
    // Issue #490: static hire-gear tiers. Fallbacks equal the shipped
    // template values.
    hireGearTiersEnabled = config.GetBoolDefault("AiPlayerbot.HireGearTiersEnabled", true);
    hireGearStartTier = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.HireGearStartTier", 0));
    if (hireGearStartTier > 7)
        hireGearStartTier = 7;
    hireGearUnlockAccountWide = config.GetBoolDefault("AiPlayerbot.HireGearUnlockAccountWide", false);
    hireGearAllQuestsAvailable = config.GetBoolDefault("AiPlayerbot.HireGearAllQuestsAvailable", false);
    {
        std::list<uint32> optionalMaps;
        LoadList<std::list<uint32> >(config.GetStringDefault("AiPlayerbot.HireGearOptionalMaps", ""), optionalMaps);
        hireGearOptionalMaps = std::set<uint32>(optionalMaps.begin(), optionalMaps.end());
        std::string multText = config.GetStringDefault("AiPlayerbot.HireGearTierCostMultipliers", "1,1.5,2,3,4,5,6,6");
        if (!TortoiseBots::ParseHireTierCostMultipliers(multText, hireGearTierCostMult))
            sLog.outError("AiPlayerbot.HireGearTierCostMultipliers '%s' is not 8 non-negative numbers; using the defaults", multText.c_str());
    }
    // Issue #492: world buffs at the six capital recruiters. Fallbacks equal
    // the shipped template values (price pairs in copper, 10000 cu = 1g).
    worldBuffsEnabled = config.GetBoolDefault("AiPlayerbot.WorldBuffsEnabled", true);
    worldBuffsKeepInRaids = config.GetBoolDefault("AiPlayerbot.WorldBuffsKeepInRaids", false);
    worldBuffsMinLevel = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.WorldBuffsMinLevel", 60));
    if (worldBuffsMinLevel > 60)
        worldBuffsMinLevel = 60;
    worldBuffsPriceBaseCopper = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.WorldBuffsPriceBaseCopper", 100000));
    if (worldBuffsPriceBaseCopper > 100000000)
        worldBuffsPriceBaseCopper = 100000000;
    worldBuffsPricePerPersonCopper = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.WorldBuffsPricePerPersonCopper", 20000));
    if (worldBuffsPricePerPersonCopper > 100000000)
        worldBuffsPricePerPersonCopper = 100000000;
    worldBuffsSaygePriceBaseCopper = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.WorldBuffsSaygePriceBaseCopper", 60000));
    if (worldBuffsSaygePriceBaseCopper > 100000000)
        worldBuffsSaygePriceBaseCopper = 100000000;
    worldBuffsSaygePricePerPersonCopper = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.WorldBuffsSaygePricePerPersonCopper", 10000));
    if (worldBuffsSaygePricePerPersonCopper > 100000000)
        worldBuffsSaygePricePerPersonCopper = 100000000;
    worldBuffsSongflowerPriceBaseCopper = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.WorldBuffsSongflowerPriceBaseCopper", 40000));
    if (worldBuffsSongflowerPriceBaseCopper > 100000000)
        worldBuffsSongflowerPriceBaseCopper = 100000000;
    worldBuffsSongflowerPricePerPersonCopper = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.WorldBuffsSongflowerPricePerPersonCopper", 10000));
    if (worldBuffsSongflowerPricePerPersonCopper > 100000000)
        worldBuffsSongflowerPricePerPersonCopper = 100000000;
    worldBuffsSilithystPriceBaseCopper = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.WorldBuffsSilithystPriceBaseCopper", 20000));
    if (worldBuffsSilithystPriceBaseCopper > 100000000)
        worldBuffsSilithystPriceBaseCopper = 100000000;
    worldBuffsSilithystPricePerPersonCopper = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.WorldBuffsSilithystPricePerPersonCopper", 4000));
    if (worldBuffsSilithystPricePerPersonCopper > 100000000)
        worldBuffsSilithystPricePerPersonCopper = 100000000;
    worldBuffsUnlockFeeRaidCopper = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.WorldBuffsUnlockFeeRaidCopper", 2000000));
    if (worldBuffsUnlockFeeRaidCopper > 100000000)
        worldBuffsUnlockFeeRaidCopper = 100000000;
    worldBuffsUnlockFeeCopper = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.WorldBuffsUnlockFeeCopper", 1000000));
    if (worldBuffsUnlockFeeCopper > 100000000)
        worldBuffsUnlockFeeCopper = 100000000;
    ammoStacksNonHunter = ai::ClampNonHunterAmmoStacks(
        config.GetIntDefault("AiPlayerbot.AmmoStacksNonHunter", static_cast<int>(ai::kAmmoStacksNonHunterDefault)));
    // Issue #473: opt-in owned/hired-bot quality of life (all off by default).
    ownedBotMaintenanceEnabled = config.GetBoolDefault("AiPlayerbot.OwnedBotMaintenanceEnabled", false);
    ownedBotAutogearEnabled = config.GetBoolDefault("AiPlayerbot.OwnedBotAutogearEnabled", false);
    ownedBotAutogearQualityCap = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.OwnedBotAutogearQualityCap", 2));
    if (ownedBotAutogearQualityCap > 5)
        ownedBotAutogearQualityCap = 5;
    ownedBotAutogearIlvlCap = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.OwnedBotAutogearIlvlCap", 0));
    playerAutogearForAll = config.GetBoolDefault("AiPlayerbot.PlayerAutogearForAll", false);
    ownedBotSummonWhenGroup = config.GetBoolDefault("AiPlayerbot.OwnedBotSummonWhenGroup", false);
    ownedBotSummonAllowInCombat = config.GetBoolDefault("AiPlayerbot.OwnedBotSummonAllowInCombat", false);
    ownedBotSummonAllowMasterDead = config.GetBoolDefault("AiPlayerbot.OwnedBotSummonAllowMasterDead", false);
    ownedBotSummonAllowBotDead = config.GetBoolDefault("AiPlayerbot.OwnedBotSummonAllowBotDead", false);
    ownedBotSummonRevive = config.GetBoolDefault("AiPlayerbot.OwnedBotSummonRevive", false);
    ownedBotSummonRepair = config.GetBoolDefault("AiPlayerbot.OwnedBotSummonRepair", false);
    ownedBotSummonCooldown = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.OwnedBotSummonCooldown", 300));
    if (ownedBotSummonCooldown > 3600)
        ownedBotSummonCooldown = 3600;
    // Remote commander: the outside commander protocol (off by default).
    remoteCommander = config.GetBoolDefault("AiPlayerbot.RemoteCommander", false);
    // Issue #884, phase 2: the commander's TBM:VIEW feed period (250..60000 ms).
    remoteCommanderViewMs = TortoiseBots::RemoteCommander::View::ClampIntervalMs(
        config.GetIntDefault("AiPlayerbot.RemoteCommanderViewMs",
            static_cast<int>(TortoiseBots::RemoteCommander::View::kDefaultIntervalMs)));
    raidEntryByCommander = config.GetBoolDefault("AiPlayerbot.RaidEntryByCommander", true);
    // Run trace (opt-in): see docs/server/run-trace.md.
    runTrace = config.GetBoolDefault("AiPlayerbot.RunTrace.Enabled", false);
    runTraceIntervalMs = TortoiseBots::RunTrace::ClampIntervalMs(
        config.GetIntDefault("AiPlayerbot.RunTrace.IntervalMs",
            static_cast<int>(TortoiseBots::RunTrace::kDefaultIntervalMs)));
    runTraceDir = config.GetStringDefault("AiPlayerbot.RunTrace.Dir", "traces");
    runTraceGmOnly = config.GetBoolDefault("AiPlayerbot.RunTrace.GmOnly", true);
    runTraceMaxFileMb = TortoiseBots::RunTrace::ClampMaxFileMb(
        config.GetIntDefault("AiPlayerbot.RunTrace.MaxFileMb",
            static_cast<int>(TortoiseBots::RunTrace::kDefaultMaxFileMb)));
    // Decision capture (opt-in, test servers): see docs/server/decision-capture.md.
    decisionCapture = config.GetBoolDefault("AiPlayerbot.DecisionCapture.Enabled", false);
    decisionCaptureDir = config.GetStringDefault("AiPlayerbot.DecisionCapture.Dir", "decisions");
    decisionCaptureSegmentMiB = TortoiseBots::DecisionCapture::ClampSegmentMiB(
        config.GetIntDefault("AiPlayerbot.DecisionCapture.SegmentMiB", 32));
    decisionCaptureCaptureMiB = TortoiseBots::DecisionCapture::ClampCaptureMiB(
        config.GetIntDefault("AiPlayerbot.DecisionCapture.CaptureMiB", 512), decisionCaptureSegmentMiB);
    decisionCaptureTotalMiB = TortoiseBots::DecisionCapture::ClampTotalMiB(
        config.GetIntDefault("AiPlayerbot.DecisionCapture.TotalMiB", 4096), decisionCaptureCaptureMiB);
    decisionCaptureRecordKiB = TortoiseBots::DecisionCapture::ClampRecordKiB(
        config.GetIntDefault("AiPlayerbot.DecisionCapture.RecordKiB", 512));
    decisionCaptureKeyframeEvery = TortoiseBots::DecisionCapture::ClampKeyframeEvery(
        config.GetIntDefault("AiPlayerbot.DecisionCapture.KeyframeEvery", 60));
    {
        // Issue #1012: the NPCs `.bot cmd gossip` may talk to. Empty (the default) = off.
        std::string const gossipText = config.GetStringDefault("AiPlayerbot.BotGossipNpcs", "");
        std::set<uint32_t> gossipNpcs;
        if (uint32_t const bad = TortoiseBots::GossipOrder::ParseNpcList(gossipText, gossipNpcs))
            sLog.outError("AiPlayerbot.BotGossipNpcs '%s': %u word(s) are not creature entries and were skipped",
                gossipText.c_str(), bad);
        botGossipNpcs = std::set<uint32>(gossipNpcs.begin(), gossipNpcs.end());
    }
     openGoSpell = config.GetIntDefault("AiPlayerbot.OpenGoSpell", 6477);

    randomChangeMultiplier = config.GetFloatDefault("AiPlayerbot.RandomChangeMultiplier", 1.0);

    randomBotCombatStrategies = config.GetStringDefault("AiPlayerbot.RandomBotCombatStrategies", "+dps assist,-threat");
    randomBotNonCombatStrategies = config.GetStringDefault("AiPlayerbot.RandomBotNonCombatStrategies", "+grind,+loot,+custom::say,+tfish,+wander,+rpg craft");
    randomBotReactStrategies = config.GetStringDefault("AiPlayerbot.RandomBotReactStrategies", "");
    randomBotDeadStrategies = config.GetStringDefault("AiPlayerbot.RandomBotDeadStrategies", "");
    combatStrategies = config.GetStringDefault("AiPlayerbot.CombatStrategies", "");
    nonCombatStrategies = config.GetStringDefault("AiPlayerbot.NonCombatStrategies", "+return,+delayed roll,+loot");
    reactStrategies = config.GetStringDefault("AiPlayerbot.ReactStrategies", "");
    deadStrategies = config.GetStringDefault("AiPlayerbot.DeadStrategies", "");

    commandPrefix = config.GetStringDefault("AiPlayerbot.CommandPrefix", "");
    commandSeparator = config.GetStringDefault("AiPlayerbot.CommandSeparator", "\\\\");

    perfMonEnabled = config.GetBoolDefault("AiPlayerbot.PerfMonEnabled", false);
    bExplicitDbStoreSave = config.GetBoolDefault("AiPlayerbot.ExplicitDbStoreSave", false);

    randomBotLoginWithPlayer = config.GetBoolDefault("AiPlayerbot.RandomBotLoginWithPlayer", false);
    // Autonomous WSG/AB/AV queue. Default on. Uses proven
    // BattleGroundJoinAction/BGStatusAction path via
    // WorldSession::HandleBattlemasterJoinOpcode (guid 1337 bypass) and
    // BattleGroundMgr ownership for invite/queue updates. Bounded cadence
    // and max-per-interval, in-memory Headless/random selection, faction/
    // level bracket/state/deserter/taxi/combat/queue checks, no second
    // queue/thread/arena/vehicle/expansion or DB tick scans.
    randomBotBgEnabled = config.GetBoolDefault("AiPlayerbot.RandomBotBgEnabled", true);
    randomBotBgQueueInterval = config.GetIntDefault("AiPlayerbot.RandomBotBgQueueInterval", 30000);
    randomBotBgMaxQueuePerInterval = config.GetIntDefault("AiPlayerbot.RandomBotBgMaxQueuePerInterval", 1);
    randomBotBgAutonomous = config.GetBoolDefault("AiPlayerbot.RandomBotBgAutonomous", true);
    randomBotBgAutonomousMaxInstances = config.GetIntDefault("AiPlayerbot.RandomBotBgAutonomousMaxInstances", 1);
    wsgFlagRoles = config.GetBoolDefault("AiPlayerbot.WsgFlagRoles", false);

    sLog.outString("Loading Race/Class probabilities");

    classRaceProbabilityTotal = 0;

    useFixedClassRaceCounts = config.GetBoolDefault("AiPlayerbot.ClassRace.UseFixedClassRaceCounts", false);
    auto isAvailableRace = [](uint8 cls, uint8 race)
    {
        // Character creation is the authoritative class/race contract. This
        // includes Tortoise rows such as Goblin and High Elf and avoids making
        // the bot module maintain a second expansion-specific matrix.
        return sObjectMgr.GetPlayerInfo(race, cls) != nullptr;
    };

    for (uint32 race = 1; race < MAX_RACES; ++race)
    {
        //Set race defaults
        if (race > 0)
        {
            std::string key = "AiPlayerbot.ClassRaceProb.0." + std::to_string(race);
            int rProb = config.GetIntDefault(key.c_str(), 100);

            for (uint32 cls = 1; cls < MAX_CLASSES; ++cls)
            {
                classRaceProbability[cls][race] = rProb;
            }
        }
    }

    //Class overrides
    for (uint32 cls = 1; cls < MAX_CLASSES; ++cls)
    {
        std::string key = "AiPlayerbot.ClassRaceProb." + std::to_string(cls);
        int cProb = config.GetIntDefault(key.c_str(), -1);

        if (cProb >= 0)
        {
            for (uint32 race = 1; race < MAX_RACES; ++race)
            {
                classRaceProbability[cls][race] = cProb;
            }
        }
    }

    //Race Class overrides
    for (uint32 race = 1; race < MAX_RACES; ++race)
    {
        for (uint32 cls = 1; cls < MAX_CLASSES; ++cls)
        {
            std::string key = "AiPlayerbot.ClassRaceProb." + std::to_string(cls) + "." + std::to_string(race);
            int rcProb = config.GetIntDefault(key.c_str(), -1);
            if (rcProb >= 0)
                classRaceProbability[cls][race] = rcProb;

            if (!isAvailableRace(cls, race))
            {
                // Dropped without a word until now, so an entry naming a
                // combination that cannot exist looked like it had been accepted
                // and the bots simply came out as something else.
                if (rcProb > 0)
                    sLog.outError("AiPlayerbot.ClassRaceProb.%u.%u is set to %d, but that class cannot be that race. Ignoring it - those bots will be created as another race.",
                        cls, race, rcProb);

                classRaceProbability[cls][race] = 0;
            }
            else
                classRaceProbabilityTotal += classRaceProbability[cls][race];
        }
    }

    if (useFixedClassRaceCounts)
    {

        // Warn about unsupported config keys
        for (uint32 race = 1; race < MAX_RACES; ++race)
        {
            std::string raceKey = "AiPlayerbot.ClassRaceProb.0." + std::to_string(race);
            int val = config.GetIntDefault(raceKey.c_str(), -1);
            if (val >= 0)
                sLog.outError("Fixed class/race counts does not yet support '%s' (race-only). This config entry will be ignored.", raceKey.c_str());
        }

        for (uint32 cls = 1; cls < MAX_CLASSES; ++cls)
        {
            std::string classKey = "AiPlayerbot.ClassRaceProb." + std::to_string(cls);
            int val = config.GetIntDefault(classKey.c_str(), -1);
            if (val >= 0)
                sLog.outError("Fixed class/race counts does not yet support '%s' (class-only). This config entry will be ignored.", classKey.c_str());
        }

        //Parse and build fixedClassRacesCounts
        {
            for (uint32 cls = 1; cls < MAX_CLASSES; ++cls)
	    {
	        for (uint32 race = 1; race < MAX_RACES; ++race)
	        {
		    std::string key = "AiPlayerbot.ClassRaceProb." + std::to_string(cls) + "." + std::to_string(race);
		    int count = config.GetIntDefault(key.c_str(), -1);

		    if (count >= 0 && !isAvailableRace(cls, race))
		        sLog.outError("AiPlayerbot.ClassRaceProb.%u.%u asks for %d bots, but that class cannot be that race. Ignoring it.",
		            cls, race, count);
		    else if (count >= 0)
		    {
		        fixedClassRaceCounts[{cls, race}] = count;
		    }
	        }
	    }
        }
    }

    botCheatMask = uint32(CheatAction::GetCheatMask(config.GetStringDefault("AiPlayerbot.BotCheats", "repair,breath")));

    rndBotCheatMask = uint32(CheatAction::GetCheatMask(config.GetStringDefault("AiPlayerbot.RndBotCheats", "repair,breath,item")));

    combatMonitor = config.GetBoolDefault("AiPlayerbot.CombatMonitor", false);
    combatTimeline = config.GetBoolDefault("AiPlayerbot.CombatTimeline", false);
    raidBossDiagnostics = config.GetBoolDefault("AiPlayerbot.RaidBossDiagnostics", false);
    LoadListString<std::list<std::string>>(config.GetStringDefault("AiPlayerbot.AllowedLogFiles", "bot_events.csv,deaths.csv"), allowedLogFiles);
    logRetentionDays = uint32(std::max(0, config.GetIntDefault("AiPlayerbot.LogRetentionDays", 3)));
    updateCheck = config.GetBoolDefault("AiPlayerbot.UpdateCheck", true);
    LoadListString<std::list<std::string>>(config.GetStringDefault("AiPlayerbot.DebugFilter", "add gathering loot,check values,emote,check mount state,jump"), debugFilter);

    worldBuffs.clear();

    //Get all config values starting with AiPlayerbot.WorldBuff
    std::vector<std::string> values = configA.GetValues("AiPlayerbot.WorldBuff");

    if (values.size())
    {
        sLog.outString("Loading WorldBuffs");
        BarGoLink wbuffBar(values.size());

        for (auto value : values)
        {
            std::vector<std::string> ids = split(value, '.');
            std::vector<uint32> params = { 0,0,0,0,0,0 };

            //Extract faction, class, spec, minlevel, maxlevel
            for (uint8 i = 0; i < 6; i++)
                if (ids.size() > i + 2)
                    params[i] = stoi(ids[i + 2]);

            //Get list of buffs for this combination.
            std::list<uint32> buffs;
            LoadList<std::list<uint32>>(config.GetStringDefault(value.c_str(), ""), buffs);

            //Store buffs for later application.
            for (auto buff : buffs)
            {
                worldBuff wb = { buff, params[0], params[1], params[2], params[3], params[4], params[5] };
                worldBuffs.push_back(wb);
            }

            wbuffBar.step();
        }
    }

    randomBotAccountPrefix = config.GetStringDefault("AiPlayerbot.RandomBotAccountPrefix", "RNDBOT");
    // Issue #265: managed-pool reset mode. Read once here for the reset
    // planner; `.reload config` must never initiate deletion, and the value is
    // never interpolated into SQL (tokens are stored and compared as data).
    randomBotPoolReset = config.GetStringDefault("AiPlayerbot.RandomBotPoolReset", "off");
    randomBotShowCloak = config.GetBoolDefault("AiPlayerbot.RandomBotShowCloak", true);
    randomBotShowHelmet = config.GetBoolDefault("AiPlayerbot.RandomBotShowHelmet", true);

	//SPP switches
    enableGreet = config.GetBoolDefault("AiPlayerbot.EnableGreet", true);
    greetCooldown = config.GetIntDefault("AiPlayerbot.GreetCooldown", 600);
    randomBotGreet = config.GetBoolDefault("AiPlayerbot.RandomBotGreet", false);
    passingBuffs = config.GetBoolDefault("AiPlayerbot.PassingBuffs", true);
    playerbotsXPrate = config.GetFloatDefault("AiPlayerbot.XPRate", 3.0f);
    disableActivityPriorities = config.GetBoolDefault("AiPlayerbot.DisableActivityPriorities", true);
    forceActiveWhenNearPlayer = config.GetBoolDefault("AiPlayerbot.ForceActiveWhenNearPlayer", false);
    limitCombatActivity = config.GetBoolDefault("AiPlayerbot.LimitCombatActivity", false);
    guildOrderAlwaysActive = config.GetBoolDefault("AiPlayerbot.GuildOrderAlwaysActive", true);
    guildOrderBankWithdraw = config.GetBoolDefault("AiPlayerbot.GuildOrderBankWithdraw", true);
    craftToolsAlways = config.GetBoolDefault("AiPlayerbot.CraftToolsAlways", true);
    guildOrderBankGold = config.GetBoolDefault("AiPlayerbot.GuildOrderBankGold", true);
    guildOrderPlayerBuy = config.GetBoolDefault("AiPlayerbot.GuildOrderPlayerBuy", false);
    guildOrderSubOrders = config.GetBoolDefault("AiPlayerbot.GuildOrderSubOrders", true);
    uint32_t depth = static_cast<uint32_t>(config.GetIntDefault("AiPlayerbot.GuildOrderSubOrderDepth", 3));
    guildOrderSubOrderDepth = (depth < 1 || depth > 16) ? 3 : depth;
    guildOrderBotRank = config.GetBoolDefault("AiPlayerbot.GuildOrderBotRank", true);
    botActiveAlone = config.GetIntDefault("AiPlayerbot.botActiveAlone", 10);
    RandombotsWalkingRPG = config.GetBoolDefault("AiPlayerbot.RandombotsWalkingRPG", false);
    minEnchantingBotLevel = config.GetIntDefault("AiPlayerbot.minEnchantingBotLevel", 10);
    pullDpsJoinDelay = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.PullDpsJoinDelay", 10));
    pullBackDpsJoinDelay = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.PullBackDpsJoinDelay", 5));
    pullBackMaxReturnTime = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.PullBackMaxReturnTime", 15));
    rangedPull = config.GetBoolDefault("AiPlayerbot.RangedPull", true);
    rangedPullMeleeApproach = config.GetFloatDefault("AiPlayerbot.RangedPullMeleeApproach", 20.0f);
    randomBotStartLevelMin = config.GetIntDefault("AiPlayerbot.RandomBotStartLevelMin", 5);
    randomBotStartLevelMax = config.GetIntDefault("AiPlayerbot.RandomBotStartLevelMax", 60);
    gearscorecheck = config.GetBoolDefault("AiPlayerbot.GearScoreCheck", false);
    levelCheck = config.GetIntDefault("AiPlayerbot.LevelCheck", 30);
    // Issue #469: pool-bot trading defaults to safe (0 = off). Out-of-range
    // values fail closed in ParsePoolBotTradeMode, never open.
    poolBotTradeMode = config.GetIntDefault("AiPlayerbot.PoolBotTradeMode", 0);
    LoadListString<std::list<std::string>>(config.GetStringDefault("AiPlayerbot.TradeActionExcludedPrefixes", "RPLL_H_,DBMv4"), tradeActionExcludedPrefixes);
    randomBotInvitePlayer = config.GetBoolDefault("AiPlayerbot.RandomBotInvitePlayer", false);
    randomBotGroupNearby = config.GetBoolDefault("AiPlayerbot.RandomBotGroupNearby", false);
    randomBotRaidNearby = config.GetBoolDefault("AiPlayerbot.RandomBotRaidNearby", true);
    randomBotGuildNearby = config.GetBoolDefault("AiPlayerbot.RandomBotGuildNearby", true);
    observability = config.GetBoolDefault("AiPlayerbot.Observability", true);
    observabilityPort = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.ObservabilityPort", 9195));
    observabilityHost = config.GetStringDefault("AiPlayerbot.ObservabilityHost", "");
    observabilityDungeonMobs = config.GetBoolDefault("AiPlayerbot.ObservabilityDungeonMobs", true);
    LoadObservabilityRaids();
    enableActionLog = config.GetBoolDefault("AiPlayerbot.EnableActionLog", true);
    {
        bool scopeKnown = true;
        actionLogScope = ai::botdiag::ParseActionLogScope(
            config.GetStringDefault("AiPlayerbot.ActionLogScope", "commanded"), &scopeKnown);
        if (!scopeKnown)
            sLog.outError("AiPlayerbot.ActionLogScope must be all, commanded or none; using commanded");
    }
    actionLogMaxAgeDays = static_cast<uint32>(std::max(0, config.GetIntDefault("AiPlayerbot.ActionLogMaxAgeDays", 3)));
    actionLogMaxTotalMB = static_cast<uint32>(std::max(0, config.GetIntDefault("AiPlayerbot.ActionLogMaxTotalMB", 2048)));
    actionLogMaxFileMB = static_cast<uint32>(std::max(0, config.GetIntDefault("AiPlayerbot.ActionLogMaxFileMB", 100)));
    actionCountsLog = config.GetBoolDefault("AiPlayerbot.ActionCountsLog", false);
    healerDiagnostics = config.GetBoolDefault("AiPlayerbot.HealerDiagnostics", false);
    botLogFile = config.GetStringDefault("AiPlayerbot.BotLogFile", "");
    {
        std::string logsDir = sConfig.GetStringDefault("LogsDir", "");
        bool botLogDebug = config.GetBoolDefault("AiPlayerbot.BotLogDebug", false);
        BotLog::Instance().Initialize(botLogFile.c_str(), logsDir.c_str(), botLogDebug);
    }
    enableOffSpecStrategies = config.GetBoolDefault("AiPlayerbot.EnableOffSpecStrategies", true);
    useWanderAsDefaultFollowStrategy = config.GetBoolDefault("AiPlayerbot.UseWanderAsDefaultFollowStrategy", true);
    defaultFormation = config.GetStringDefault("AiPlayerbot.DefaultFormation", "chaos");

    guildMaxBotLimit = config.GetIntDefault("AiPlayerbot.GuildMaxBotLimit", 1000);

    botAcceptDuelMinimumLevel = config.GetIntDefault("AiPlayerbot.BotAcceptDuelMinimumLevel", 10);

    randomBotFormGuild = config.GetBoolDefault("AiPlayerbot.RandomBotFormGuild", false);

    boostFollow = config.GetBoolDefault("AiPlayerbot.BoostFollow", true);
    raidFollowWedge = config.GetBoolDefault("AiPlayerbot.RaidFollowWedge", false);
    dungeonFollowHoldBeforePacks = config.GetBoolDefault("AiPlayerbot.DungeonFollowHoldBeforePacks", false);
    warlockPetBySpec = config.GetBoolDefault("AiPlayerbot.WarlockPetBySpec", true);
    warlockRaidRotation = config.GetBoolDefault("AiPlayerbot.WarlockRaidRotation", false);
    rogueDungeonThreatDrops = config.GetBoolDefault("AiPlayerbot.RogueDungeonThreatDrops", true);
    sapApproachAvoidPack = config.GetBoolDefault("AiPlayerbot.SapApproachAvoidPack", false);
    warriorGapCloser = config.GetBoolDefault("AiPlayerbot.WarriorGapCloser", false);
    announceSunderStacks = config.GetBoolDefault("AiPlayerbot.AnnounceSunderStacks", false);
    hunterViperOnLowMana = config.GetBoolDefault("AiPlayerbot.HunterViperOnLowMana", true);
    hunterCarveInReachOnly = config.GetBoolDefault("AiPlayerbot.HunterCarveInReachOnly", false);
    paladinJudgementUpkeep = config.GetBoolDefault("AiPlayerbot.PaladinJudgementUpkeep", true);
    onyxiaWhelpsAndEggs = config.GetBoolDefault("AiPlayerbot.OnyxiaWhelpsAndEggs", true);
    dungeonTactics = config.GetBoolDefault("AiPlayerbot.DungeonTactics", true);
    // Startup-only on purpose (not in LoadDungeonRunKeys): the key shapes the reaction engine's trigger list, which
    // is built when an engine inits, so `.reload config` must not change it under live engines; a restart applies it.
    raidReactions = config.GetBoolDefault("AiPlayerbot.RaidReactions", false);
    LoadDungeonRunKeys();
    avoidCreatureAuras = config.GetBoolDefault("AiPlayerbot.AvoidCreatureAuras", true);
    allowRepairedTaxiPaths = config.GetBoolDefault("AiPlayerbot.AllowRepairedTaxiPaths", false);
    botInstantFlight = config.GetBoolDefault("AiPlayerbot.BotInstantFlight", false);
    turnInRpg = config.GetBoolDefault("AiPlayerbot.TurnInRpg", false);
    shareTargets = config.GetBoolDefault("AiPlayerbot.ShareTargets", true);
    globalSoundEffects = config.GetBoolDefault("AiPlayerbot.GlobalSoundEffects", false);
    nonGmFreeSummon = config.GetBoolDefault("AiPlayerbot.NonGmFreeSummon", false);

    //SPP automation
    autoPickReward = config.GetStringDefault("AiPlayerbot.AutoPickReward", "yes");
    autoEquipUpgradeLoot = config.GetBoolDefault("AiPlayerbot.AutoEquipUpgradeLoot", true);
    claimedBotSelfEquip = config.GetBoolDefault("AiPlayerbot.ClaimedBotSelfEquip", true);
    equipUpgradeThreshold = config.GetFloatDefault("AiPlayerbot.EquipUpgradeThreshold", 1.1f);
    syncQuestWithPlayer = config.GetBoolDefault("AiPlayerbot.SyncQuestWithPlayer", false);
    syncQuestForPlayer = config.GetBoolDefault("AiPlayerbot.SyncQuestForPlayer", false);
    autoTrainSpells = config.GetStringDefault("AiPlayerbot.AutoTrainSpells", "yes");
    autoPickTalents = config.GetStringDefault("AiPlayerbot.AutoPickTalents", "full");
    autoLearnTrainerSpells = config.GetBoolDefault("AiPlayerbot.AutoLearnTrainerSpells", true);
    autoLearnQuestSpells = config.GetBoolDefault("AiPlayerbot.AutoLearnQuestSpells", true);
    autoLearnQuestSpellsAlts = config.GetBoolDefault("AiPlayerbot.AutoLearnQuestSpellsAlts", false);
    autoLearnDroppedSpells = config.GetBoolDefault("AiPlayerbot.AutoLearnDroppedSpells", false);
    petAutoTrain = config.GetBoolDefault("AiPlayerbot.PetAutoTrain", true);
    petAutoTrainAlts = config.GetBoolDefault("AiPlayerbot.PetAutoTrainAlts", false);
    autoDoQuests = config.GetBoolDefault("AiPlayerbot.AutoDoQuests", true);
    autoShareConjuredOnTrade = config.GetBoolDefault("AiPlayerbot.AutoShareConjuredOnTrade", true);
    generateTravelNodes = config.GetBoolDefault("AiPlayerbot.GenerateTravelNodes", false);
    generateFishLocations = config.GetBoolDefault("AiPlayerbot.GenerateFishLocations", false);
    asyncTravelPartitions = config.GetBoolDefault("AiPlayerbot.AsyncTravelPartitions", false); // false = travel/terrain lookups on main thread only (crash-safe on cores without concurrent terrain load)
    if (generateTravelNodes || generateFishLocations)
    {
        sLog.outError("TortoiseBots: travel/fish cache generation is disabled because the pinned core PathInfo has no area query or avoidance filter; use persisted caches or direct movement/fishing.");
        generateTravelNodes = false;
        generateFishLocations = false;
    }
    syncAltLevelToMaster = config.GetBoolDefault("AiPlayerbot.SyncAltLevelToMaster", false);
    talentsInPublicNote = config.GetBoolDefault("AiPlayerbot.TalentsInPublicNote", true);
    professionsGrowWithLevel = config.GetBoolDefault("AiPlayerbot.ProfessionsGrowWithLevel", true);
    botProfessionSpecializations = config.GetIntDefault("AiPlayerbot.BotProfessionSpecializations", 2);

    // Gear progression system
    gearProgressionSystemEnabled = config.GetBoolDefault("AiPlayerbot.GearProgressionSystem.Enable", false);

    // Gear progression phase
    for (uint8 phase = 0; phase < MAX_GEAR_PROGRESSION_LEVEL; phase++)
    {
        std::ostringstream os; os << "AiPlayerbot.GearProgressionSystem." << std::to_string(phase) << ".MinItemLevel";
        gearProgressionSystemItemLevels[phase][0] = config.GetIntDefault(os.str().c_str(), 9999999);
        os.str(""); os << "AiPlayerbot.GearProgressionSystem." << std::to_string(phase) << ".MaxItemLevel";
        gearProgressionSystemItemLevels[phase][1] = config.GetIntDefault(os.str().c_str(), 9999999);

        // Gear progression class
        for (uint8 cls = 1; cls < MAX_CLASSES; cls++)
        {
            // Gear progression spec
            for (uint8 spec = 0; spec < 4; spec++)
            {
                // Gear progression slot
                for (uint8 slot = 0; slot < SLOT_EMPTY; slot++)
                {
                    std::ostringstream os; os << "AiPlayerbot.GearProgressionSystem." << std::to_string(phase) << "." << std::to_string(cls) << "." << std::to_string(spec) << "." << std::to_string(slot);
                    gearProgressionSystemItems[phase][cls][spec][slot] = config.GetIntDefault(os.str().c_str(), -1);
                }
            }
        }
    }

    sLog.outString("Loading free bots.");
    selfBotLevel = BotSelfBotLevel(config.GetIntDefault("AiPlayerbot.SelfBotLevel", uint32(BotSelfBotLevel::GM_ONLY)));
    LoadListString<std::list<std::string>>(config.GetStringDefault("AiPlayerbot.ToggleAlwaysOnlineAccounts", ""), toggleAlwaysOnlineAccounts);
    LoadListString<std::list<std::string>>(config.GetStringDefault("AiPlayerbot.ToggleAlwaysOnlineChars", ""), toggleAlwaysOnlineChars);

    for (std::string& nm : toggleAlwaysOnlineAccounts)
        std::transform(nm.begin(), nm.end(), nm.begin(), toupper);

    for (std::string& nm : toggleAlwaysOnlineChars)
    {
        std::transform(nm.begin(), nm.end(), nm.begin(), tolower);
        nm[0] = toupper(nm[0]);
    }

    // Issue #265: the managed-account registry is loaded before the free-alt
    // scan so module-owned pool accounts are excluded by identity rather than
    // by username prefix. A failed load is reported and leaves the registry
    // unvalidated; destructive pool operations then refuse to run.
    TortoiseBots::RandomBotAccountRegistry::Instance().LoadValidatedAccounts();

    loadFreeAltBotAccounts();

    targetPosRecalcDistance = config.GetFloatDefault("AiPlayerbot.TargetPosRecalcDistance", 0.1f),

    sLog.outString("Loading area levels.");
    sTravelMgr.LoadAreaLevels();
    sLog.outString("Loading spellIds.");
    ChatHelper::PopulateSpellNameList();
    ItemUsageValue::PopulateProfessionReagentIds();
    ItemUsageValue::PopulateSoldByVendorItemIds();
    ItemUsageValue::PopulateReagentItemIdsForCraftableItemIds();

    sRandomItemMgr.Init();
    sPlayerbotTextMgr.LoadBotTexts();
    sPlayerbotHelpMgr.LoadBotHelpTexts();

    LoadTalentSpecs();

    if (sPlayerbotAIConfig.autoDoQuests)
    {
        sLog.outString("Loading Quest Detail Data...");
        sTravelMgr.LoadQuestTravelTable();
    }

    sLog.outString("Loading guild order sources.");
    ai::BuildGuildOrderSourceIndex();
    ai::BuildGuildOrderEnchantIndex();

    sLog.outString("Loading taxi node zones.");
    sTravelMgr.LoadTaxiNodeZones();

    sLog.outString("Loading named locations.");
    sRandomBotFacade.LoadNamedLocationCache();
    sLog.outString("Loading boss tactics.");
    ai::LoadBossTactics();
    sLog.outString("Loading combat timeline aura watch.");
    TortoiseBots::CombatMonitorService::Instance().LoadTimelineWatch();
    sLog.outString("Loading boss stand sets.");
    ai::LoadBossStands();
    sLog.outString("Loading dungeon plans.");
    ai::LoadDungeonPlans();
    sLog.outString("Loading bot camp spots.");
    sCampMgr.Load();
    sRandomBotFacade.LoadBattleMastersCache();
    sRandomBotFacade.LoadAuctionPrices();

    sLog.outString("---------------------------------------");
    sLog.outString("        AI Playerbot initialized       ");
    sLog.outString("---------------------------------------");
    sLog.outString();

    return true;
}

bool PlayerbotAIConfig::IsInRandomAccountList(uint32 id)
{
    // Issue #265: pool identity comes from the managed-account registry, never
    // from a username prefix. An account that merely looks like a bot account
    // is not module-owned and stays outside every random-pool operation.
    return TortoiseBots::RandomBotAccountRegistry::Instance().IsRegistered(id);
}

bool PlayerbotAIConfig::IsFreeAltBot(uint32 guid)
{
    // Issue #872: an autonomous alt-bot plays with the free-bot strategy set.
    if (TortoiseBots::AltBotService::Instance().IsAutonomous(guid))
        return true;

    for (auto bot : freeAltBots)
        if (bot.second == guid)
            return true;

    return false;
}

bool PlayerbotAIConfig::IsInRandomQuestItemList(uint32 id)
{
    return find(randomBotQuestItems.begin(), randomBotQuestItems.end(), id) != randomBotQuestItems.end();
}

bool PlayerbotAIConfig::IsInPvpProhibitedZone(uint32 id)
{
	return find(pvpProhibitedZoneIds.begin(), pvpProhibitedZoneIds.end(), id) != pvpProhibitedZoneIds.end();
}

bool PlayerbotAIConfig::IsExtraTownGuard(uint32 entry) const
{
	return find(extraTownGuardEntries.begin(), extraTownGuardEntries.end(), entry) != extraTownGuardEntries.end();
}

std::string PlayerbotAIConfig::GetValue(std::string name)
{
    std::ostringstream out;

    if (name == "GlobalCooldown")
        out << globalCoolDown;
    else if (name == "ReactDelay")
        out << reactDelay;

    else if (name == "SightDistance")
        out << sightDistance;
    else if (name == "SpellDistance")
        out << spellDistance;
    else if (name == "ReactDistance")
        out << reactDistance;
    else if (name == "GrindDistance")
        out << grindDistance;
    else if (name == "LootDistance")
        out << lootDistance;
    else if (name == "FleeDistance")
        out << fleeDistance;
    else if (name == "BombRunoutDistance")
        out << bombRunoutDistance;
    else if (name == "HazardEvasionDistance")
        out << hazardEvasionDistance;

    else if (name == "CriticalHealth")
        out << criticalHealth;
    else if (name == "LowHealth")
        out << lowHealth;
    else if (name == "MediumHealth")
        out << mediumHealth;
    else if (name == "AlmostFullHealth")
        out << almostFullHealth;
    else if (name == "LowMana")
        out << lowMana;

    else if (name == "IterationsPerTick")
        out << iterationsPerTick;

    return out.str();
}

void PlayerbotAIConfig::SetValue(std::string name, std::string value)
{
    std::istringstream out(value, std::istringstream::in);

    if (name == "GlobalCooldown")
        out >> globalCoolDown;
    else if (name == "ReactDelay")
        out >> reactDelay;

    else if (name == "SightDistance")
        out >> sightDistance;
    else if (name == "SpellDistance")
        out >> spellDistance;
    else if (name == "ReactDistance")
        out >> reactDistance;
    else if (name == "FleeDistance")
        out >> fleeDistance;
    else if (name == "BombRunoutDistance")
        out >> bombRunoutDistance;
    else if (name == "HazardEvasionDistance")
        out >> hazardEvasionDistance;
    else if (name == "LootDistance")
        out >> lootDistance;

    else if (name == "CriticalHealth")
        out >> criticalHealth;
    else if (name == "LowHealth")
        out >> lowHealth;
    else if (name == "MediumHealth")
        out >> mediumHealth;
    else if (name == "AlmostFullHealth")
        out >> almostFullHealth;
    else if (name == "LowMana")
        out >> lowMana;

    else if (name == "IterationsPerTick")
        out >> iterationsPerTick;
}

void PlayerbotAIConfig::loadFreeAltBotAccounts()
{
    bool allCharsOnline = (selfBotLevel == BotSelfBotLevel::ALWAYS_ACTIVE);

    freeAltBots.clear();

    // Issue #265: free-alt handling must never enroll a pool account. Without a
    // validated registry the module cannot tell a managed pool account from a
    // personal one, so no account is enrolled at all rather than misclassifying
    // every pool account as somebody's alt.
    if (!TortoiseBots::RandomBotAccountRegistry::Instance().IsValidated())
    {
        sLog.outError("TortoiseBots: managed-account registry is not validated; skipping the free-alt account scan");
        return;
    }

    auto results = LoginDatabase.PQuery("SELECT username, id FROM account");
    if (results)
    {
        do
        {
            bool accountToggle = false;

            Field* fields = results->Fetch();
            std::string accountName = fields[0].GetString();
            uint32 accountId = fields[1].GetUInt32();

            // Issue #265: module-owned pool accounts are identified by the
            // registry, not by their username prefix. A prefix-matching
            // account that was never adopted stays a normal account here.
            if (TortoiseBots::RandomBotAccountRegistry::Instance().IsRegistered(accountId))
                continue;

            if (std::find(toggleAlwaysOnlineAccounts.begin(), toggleAlwaysOnlineAccounts.end(), accountName) != toggleAlwaysOnlineAccounts.end())
                accountToggle = true;

            auto result = CharacterDatabase.PQuery("SELECT name, guid FROM characters WHERE account = '%u'", accountId);
            if (!result)
                continue;

            do
            {
                bool charToggle = false;

                Field* fields = result->Fetch();
                std::string charName = fields[0].GetString();
                uint32 guid = fields[1].GetUInt32();

                BotAlwaysOnline always = BotAlwaysOnline(sRandomBotFacade.GetValue(guid, "always"));

                if (always == BotAlwaysOnline::DISABLED_BY_COMMAND)
                    continue;

                if (std::find(toggleAlwaysOnlineChars.begin(), toggleAlwaysOnlineChars.end(), charName) != toggleAlwaysOnlineChars.end())
                    charToggle = true;

                bool thisCharAlwaysOnline = allCharsOnline;

                if (accountToggle || charToggle)
                    thisCharAlwaysOnline = !thisCharAlwaysOnline;

                if ((thisCharAlwaysOnline && always != BotAlwaysOnline::DISABLED_BY_COMMAND) || always == BotAlwaysOnline::ACTIVE)
                {
                    sLog.outString("Enabling always online for %s", charName.c_str());
                    freeAltBots.push_back(std::make_pair(accountId, guid));
                }

            } while (result->NextRow());


        } while (results->NextRow());
    }
}

// Packs a closed log piece into <piece>.gz off the world thread and deletes
// the plain piece once the pack is complete. A stopped server leaves the plain
// piece, and the next start packs it again.
static void CompressLogPiece(std::string const& piece)
{
    std::thread([piece]()
    {
        FILE* in = fopen(piece.c_str(), "rb");
        if (!in)
            return;
        gzFile out = gzopen((piece + ".gz").c_str(), "wb6");
        if (!out)
        {
            fclose(in);
            return;
        }
        bool ok = true;
        char buffer[64 * 1024];
        size_t n;
        while (ok && (n = fread(buffer, 1, sizeof(buffer), in)) > 0)
            ok = gzwrite(out, buffer, unsigned(n)) == int(n);
        fclose(in);
        if (gzclose(out) != Z_OK)
            ok = false;
        if (ok)
            std::remove(piece.c_str());
    }).detach();
}

std::string PlayerbotAIConfig::GetTimestampStr()
{
    time_t t = time(nullptr);
    tm* aTm = localtime(&t);
    //       YYYY   year
    //       MM     month (2 digits 01-12)
    //       DD     day (2 digits 01-31)
    //       HH     hour (2 digits 00-23)
    //       MM     minutes (2 digits 00-59)
    //       SS     seconds (2 digits 00-59)
    char buf[20];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", aTm);

    return std::string(buf);
}

bool PlayerbotAIConfig::openLog(std::string fileName, char const* mode, bool haslog)
{
    std::lock_guard<std::mutex> guard(m_logMtx);
    return openLogLocked(fileName, mode, haslog);
}

bool PlayerbotAIConfig::openLogLocked(std::string const& fileName, char const* mode, bool haslog)
{
    if (!haslog && !hasLog(fileName))
        return false;

    // A file that failed to open or rotate is skipped for a cool-down: log() calls
    // this on every event, and retrying cost an fopen plus an error line each time.
    // The clock is only read for a file that already failed.
    auto const failed = logOpenFailed.find(fileName);
    if (failed != logOpenFailed.end())
    {
        if (time(nullptr) < failed->second)
            return false;
        logOpenFailed.erase(failed);
    }

    auto logFileIt = logFiles.find(fileName);
    if (logFileIt == logFiles.end())
    {
        logFiles.insert(make_pair(fileName, std::make_pair(nullptr, false)));
        logFileIt = logFiles.find(fileName);
    }

    FILE* file = logFileIt->second.first;
    bool fileOpen = logFileIt->second.second;

    if (fileOpen) //close log file
        fclose(file);
    logFileIt->second = {nullptr, false};

    std::string const m_logsDir = logsDirPath();
    std::string const path = m_logsDir + fileName;
    time_t const now = time(nullptr);
    if (logRotation.FirstOpen(path))
    {
        // Pack the last run's file, and finish the packs a stopped run left.
        std::vector<std::string> plain, compressed;
        ai::LogFileRotation::Pieces(path, plain, compressed);
        for (std::string const& piece : plain)
            CompressLogPiece(piece);
        if (!archiveLog(path, now))
        {
            logOpenFailed[fileName] = time(nullptr) + LOG_OPEN_RETRY_SECONDS;
            return false;
        }
    }
    file = fopen(path.c_str(), mode);

    // fopen fails whenever the logs directory does not exist yet, which on a
    // fresh install it usually does not. This used to mark the file open all the
    // same and return true, so log() fetched a null handle back out of the map
    // and handed it to fputs. That is a segfault on the very first bot event -
    // during startup, which makes it read as "the server dies on boot" rather
    // than "a log file could not be opened".
    if (!file)
    {
        sLog.outError("Could not open bot log file %s%s (%s). Logging to it is off for %d minutes.",
            m_logsDir.c_str(), fileName.c_str(), strerror(errno), int(LOG_OPEN_RETRY_SECONDS / 60));

        logFileIt->second.first = nullptr;
        logFileIt->second.second = false;
        logOpenFailed[fileName] = time(nullptr) + LOG_OPEN_RETRY_SECONDS;

        return false;
    }

    logFileIt->second.first = file;
    logFileIt->second.second = true;
    logHour[fileName] = now / 3600;

    return true;
}

std::string PlayerbotAIConfig::logsDirPath()
{
    std::string logsDir = sConfig.GetStringDefault("LogsDir", "");
    if (!logsDir.empty())
    {
        if ((logsDir.at(logsDir.length() - 1) != '/') && (logsDir.at(logsDir.length() - 1) != '\\'))
            logsDir.append("/");
    }
    return logsDir;
}

// Moves a live log to a timestamped piece, packs it in the background and
// deletes pieces older than AiPlayerbot.LogRetentionDays. False when the
// rename failed; the live file is then left as it was.
bool PlayerbotAIConfig::archiveLog(std::string const& path, time_t now)
{
    std::string piece;
    std::error_code error;
    if (!ai::LogFileRotation::Archive(path, now, piece, error))
    {
        sLog.outError("Could not rotate bot log file %s: %s. Existing file preserved.",
            path.c_str(), error.message().c_str());
        return false;
    }
    if (!piece.empty())
        CompressLogPiece(piece);
    if (logRetentionDays)
        ai::LogFileRotation::Prune(path, logRetentionDays);
    return true;
}

// The live log holds at most the current hour: when the hour changes it is
// packed and the next line starts a new file. Caller holds m_logMtx.
void PlayerbotAIConfig::rotateLogHourly(std::string const& fileName, FILE* file)
{
    time_t const now = time(nullptr);
    auto const hour = logHour.find(fileName);
    if (hour == logHour.end() || hour->second == now / 3600)
        return;

    fclose(file);
    logFiles[fileName] = {nullptr, false};
    logHour.erase(hour);
    archiveLog(logsDirPath() + fileName, now);
}

void PlayerbotAIConfig::log(std::string fileName, const char* line)
{
    if (!line)
        return;

    std::lock_guard<std::mutex> guard(m_logMtx);

    if (!isLogOpen(fileName))
        if (!openLogLocked(fileName, "a", false))
            return;

    FILE* file = logFiles.find(fileName)->second.first;
    if (!file)
        return;
    if (!file)
        return;

    fputs(line, file);
    fputc('\n', file);
    fflush(file);
    rotateLogHourly(fileName, file);

    fflush(stdout);
}

void PlayerbotAIConfig::logf(std::string fileName, const char* format, ...)
{
    if (!format)
        return;

    std::lock_guard<std::mutex> guard(m_logMtx);

    if (!isLogOpen(fileName))
        if (!openLogLocked(fileName, "a", false))
            return;

    FILE* file = logFiles.find(fileName)->second.first;

    va_list ap;
    va_start(ap, format);
    vfprintf(file, format, ap);
    va_end(ap);
    fputc('\n', file);
    fflush(file);
    rotateLogHourly(fileName, file);

    fflush(stdout);
}

void PlayerbotAIConfig::logEvent(PlayerbotAI* ai, std::string eventName, std::string info1, std::string info2,
    std::string info3, std::string info4, std::string info5)
{
    if (hasLog("bot_events.csv"))
    {
        Player* bot = ai->GetBot();

        std::ostringstream out;
        out << sPlayerbotAIConfig.GetTimestampStr() << "+00,";
        out << bot->GetName() << ",";
        out << eventName << ",";
        out << std::fixed << std::setprecision(2);
        WorldPosition(bot).printWKT(out);

        out << std::to_string(bot->GetRace()) << ",";
        out << std::to_string(bot->GetClass()) << ",";
        float subLevel = ai->GetLevelFloat();

        out << subLevel << ",";

        out << "\"" << info1 << "\",";
        out << "\"" << info2 << "\"";
        if (!info3.empty() || !info4.empty() || !info5.empty())
        {
            out << ",\"" << info3 << "\",";
            out << "\"" << info4 << "\",";
            out << "\"" << info5 << "\"";
        }

        log("bot_events.csv", out.str().c_str());
    }

    // Mirror the dashboard-relevant subset to the observability daemon over
    // UDP. Deliberately outside the hasLog() gate: the activity rollup is a
    // telemetry feature, not a CSV-logging one. EmitBotActivity filters to its
    // own whitelist and no-ops when the emitter is disabled.
    if (ai)
    {
        sObservabilityEmitter.EmitBotActivity(eventName, info1, info2, ai->GetBot());
        // Run trace: loot events of the traced bots (one atomic load when off).
        if (TortoiseBots::RunTraceService::Instance().Active())
            TortoiseBots::RunTraceService::Instance().NoteBotEvent(ai, eventName, info1, info2);
    }
};

void PlayerbotAIConfig::logEvent(PlayerbotAI* ai, std::string eventName, ObjectGuid guid, std::string info2)
{
    std::string info1 = "";

    Unit* victim;
    if (guid)
    {
        victim = ai->GetUnit(guid);
        if (victim)
            info1 = victim->GetName();
    }

    logEvent(ai, eventName, info1, info2);
};

bool PlayerbotAIConfig::CanLogAction(PlayerbotAI* ai, std::string actionName, bool isExecute, std::string lastActionName)
{
    bool forRpg = (actionName.find("rpg") == 0) && ai->HasStrategy("debug rpg", BotState::BOT_STATE_NON_COMBAT);

    if (!forRpg)
    {
        if (isExecute && !ai->HasStrategy("debug", BotState::BOT_STATE_NON_COMBAT))
            return false;

        if (!isExecute && !ai->HasStrategy("debug action", BotState::BOT_STATE_NON_COMBAT))
            return false;

        if ((lastActionName == actionName) && (actionName == "melee"))
        {
            return false;
        }
    }

    return std::find(debugFilter.begin(), debugFilter.end(), actionName) == debugFilter.end();
}

void PlayerbotAIConfig::LoadTalentSpecs()
{
    sLog.outString("Loading TalentSpecs");

    uint32 maxSpecLevel = 0;

    for (uint32 cls = 1; cls < MAX_CLASSES; ++cls)
    {
        classSpecs[cls] = ClassSpecs(1 << (cls - 1));
        for (uint32 spec = 0; spec < 10; ++spec)
        {
            std::ostringstream os; os << "AiPlayerbot.PremadeSpecName." << cls << "." << spec;
            std::string specName = config.GetStringDefault(os.str().c_str(), "");
            if (!specName.empty())
            {
                std::ostringstream os; os << "AiPlayerbot.PremadeSpecProb." << cls << "." << spec;
                int probability = config.GetIntDefault(os.str().c_str(), 100);

                TalentPath talentPath(spec, specName, probability);

                for (uint32 level = 10; level <= DEFAULT_MAX_LEVEL; level++)
                {
                    std::ostringstream os; os << "AiPlayerbot.PremadeSpecLink." << cls << "." << spec << "." << level;
                    std::string specLink = config.GetStringDefault(os.str().c_str(), "");
                    specLink = specLink.substr(0, specLink.find("#", 0));
                    specLink = specLink.substr(0, specLink.find(" ", 0));

                    if (!specLink.empty())
                    {
                        if (maxSpecLevel < level)
                            maxSpecLevel = level;

                        std::ostringstream out;

                        //Ignore bad specs.
                        if (!classSpecs[cls].baseSpec.CheckTalentLink(specLink, &out))
                        {
                            sLog.outErrorDb("Error with premade spec link: %s", specLink.c_str());
                            sLog.outErrorDb("%s", out.str().c_str());
                            continue;
                        }

                        TalentSpec linkSpec(&classSpecs[cls].baseSpec, specLink);

                        if (!linkSpec.CheckTalents(TalentSpec::LeveltoPoints(level), &out))
                        {
                            sLog.outErrorDb("Error with premade spec: %s", specLink.c_str());
                            sLog.outErrorDb("%s", out.str().c_str());
                            continue;
                        }


                        talentPath.talentSpec.push_back(linkSpec);
                    }

                }

                //Only add paths that have atleast 1 spec.
                if (talentPath.talentSpec.size() > 0)
                    classSpecs[cls].talentPath.push_back(talentPath);
            }
        }
    }

    if (classSpecs[1].talentPath.empty())
        sLog.outErrorDb("No premade specs found!!");
    else
    {
        if (maxSpecLevel < DEFAULT_MAX_LEVEL && randomBotMaxLevel < DEFAULT_MAX_LEVEL)
            sLog.outErrorDb("!!!!!!!!!!! randomBotMaxLevel and the talent specs are below the Vanilla/Tortoise level cap. Please check the configuration.");

    }
}
