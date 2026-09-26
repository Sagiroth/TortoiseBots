#pragma once

#include <string>
#include <unordered_set>
#include "Config/Config.h"
#include "Talentspec.h"
#include "SharedDefines.h"
#include "SystemConfig.h"

class Player;
class ChatHandler;

#if PLATFORM == PLATFORM_WINDOWS
inline std::string _D_AIPLAYERBOT_CONFIG = "aiplayerbot.conf";
#else
inline std::string _D_AIPLAYERBOT_CONFIG = SYSCONFDIR "aiplayerbot.conf";
#endif

enum class BotCheatMask : uint32
{
    none = 0,
    taxi = 1 << 0,
    gold = 1 << 1,
    health = 1 << 2,
    mana = 1 << 3,
    power = 1 << 4,
    item = 1 << 5,
    cooldown = 1 << 6,
    repair = 1 << 7,
    movespeed = 1 << 8,
    attackspeed = 1 << 9,
    breath = 1 << 10,
    quest = 1 << 11,
    maxMask = 1 << 12
};

enum class BotAutoLogin : uint32
{
    DISABLED = 0,
    LOGIN_ALL_WITH_MASTER = 1,
    LOGIN_ONLY_ALWAYS_ACTIVE = 2
};

enum class BotSelfBotLevel : uint32
{
    DISABLED = 0,
    GM_ONLY = 1,
    ACTIVE_BY_COMMAND = 2,
    ALWAYS_ALLOWED = 3,
    ACTIVE_BY_LOGIN = 4,
    ALWAYS_ACTIVE = 5
};

enum class BotAlwaysOnline : uint32
{
    DISABLED = 0,
    ACTIVE = 1,
    DISABLED_BY_COMMAND = 2
};

#define MAX_GEAR_PROGRESSION_LEVEL 6

class ConfigAccess
{
public:
    explicit ConfigAccess(Config& config) : m_config(config) {}

    std::vector<std::string> GetValues(const std::string& name) const;

private:
    Config& m_config;
};

struct ParsedUrl {
    std::string hostname;
    std::string path;
    int port;
    bool https;
};

class PlayerbotAIConfig
{
public:
    PlayerbotAIConfig();
    static PlayerbotAIConfig& instance()
    {
        static PlayerbotAIConfig instance;
        return instance;
    }

public:
    bool Initialize();
    bool IsInRandomAccountList(uint32 id);
    bool IsFreeAltBot(uint32 guid);
    bool IsFreeAltBot(Player* player) {return IsFreeAltBot(player->GetGUIDLow());}
    bool IsInRandomQuestItemList(uint32 id);
	bool IsInPvpProhibitedZone(uint32 id);

    bool enabled;
    bool allowGuildBots;
    bool allowMultiAccountAltBots;
    uint32 globalCoolDown, reactDelay, maxWaitForMove, expireActionTime, dispelAuraDuration, passiveDelay, repeatDelay,
        errorDelay, rpgDelay, sitDelay, returnDelay, lootDelay;
    float sightDistance, spellDistance, reactDistance, grindDistance, lootDistance, groupMemberLootDistance, groupMemberLootDistanceWithActiveMaster,
        gatheringDistance, groupMemberGatheringDistance, groupMemberGatheringDistanceWithActiveMaster, shootDistance,
        fleeDistance, tooCloseDistance, meleeDistance, followDistance, raidFollowDistance, wanderMinDistance, wanderMaxDistance, whisperDistance, contactDistance,
        aoeRadius, rpgDistance, targetPosRecalcDistance, farDistance, healDistance, healDistanceBg, aggroDistance, proximityDistance, maxFreeMoveDistance, freeMoveDelay, walkDistance;
    uint32 criticalHealth, lowHealth, mediumHealth, almostFullHealth;
    uint32 lowMana, mediumMana;

    uint32 openGoSpell;
    bool randomBotAutologin;
    BotAutoLogin botAutologin;
    std::list<uint32> randomBotQuestItems;
    std::list<uint32> randomBotAccounts;
    std::unordered_set<uint32> nonRandomBotAccounts;
    std::list<uint32> randomBotSpellIds;
    std::list<uint32> randomBotQuestIds;
    std::list<uint32> immuneSpellIds;
    std::list<std::pair<uint32, uint32>> freeAltBots;
    std::list<std::string> toggleAlwaysOnlineAccounts;
    std::list<std::string> toggleAlwaysOnlineChars;
    bool enableMinimalMove;
    uint32 transportTeleportType;
    uint32 randomGearMaxLevel;
    uint32 randomGearMaxDiff;
    bool randomGearUpgradeEnabled;
    // Owner-rule source-tier cap (roadmap #289): 0 base only, 1 + end-game
    // dungeons (229/289/329/800), 2 + raids. REP/PVP are orthogonal flags.
    uint32 randomGearMaxSourceTier;
    bool randomGearAllowReputation;
    bool randomGearAllowPvP;
    // Rare world-epic chance per gear slot on the fresh-seed path.
    float randomGearSeedEpicChance;
    bool randomGearTabards;
    bool randomGearTabardsReplaceGuild;
    bool randomGearTabardsUnobtainable;
    float randomGearTabardsChance;
    std::list<uint32> randomGearBlacklist;
    std::list<uint32> randomGearWhitelist;
    bool randomGearProgression;
    float randomGearLoweringChance;
    bool rollBadItemsWithPlayer;
    // One-time generation of the optional ai_playerbot_equip_cache and
    // ai_playerbot_rnditem_cache tables while they are empty. The in-memory
    // item info cache and the world weight-scale load are never gated by this.
    bool generateItemCaches;
    float usePotionChance;
    float attackEmoteChance;
    uint32 minRandomBots, maxRandomBots;
    uint32 randomBotUpdateInterval;
    bool randomBotTimedLogout, randomBotTimedOffline;
    // Level ladder (AiPlayerbot.LevelLadder*): choose the next random bot to log in by the
    // shortfall of its level band instead of round-robin over the whole pool, highest level
    // first inside the band, so the field spreads over the levels and the leaders climb to 60.
    bool levelLadder = true;
    uint32 levelLadderBandSize = 5;
    uint32 levelLadderMaxLevelSharePct = 10;
    uint32 levelLadderLogMinutes = 5;
    uint32 minRandomBotInWorldTime, maxRandomBotInWorldTime;
    uint32 minRandomBotRandomizeTime, maxRandomBotRandomizeTime;
    uint32 minRandomBotChangeStrategyTime, maxRandomBotChangeStrategyTime;
    uint32 minRandomBotReviveTime, maxRandomBotReviveTime;
    uint32 randomBotsMaxLoginsPerInterval;
    uint32 minRandomBotsPriceChangeInterval, maxRandomBotsPriceChangeInterval;
    //Auction house settings
    bool shouldQueryAHListingsOutsideOfAH;
    std::list<uint32> ahOverVendorItemIds;
    std::list<uint32> vendorOverAHItemIds;
    bool botCheckAllAuctionListings;
    bool botsSaveEpics;
    uint32 auctionPriceRefreshInterval = 60; // seconds between price mirror refreshes
    // Default-off bounded AH market population (module-only, native transaction path).
    bool ahMarketEnabled = false;
    uint32 ahMarketInterval = 120; // seconds between market ticks
    uint32 ahMarketBatchSize = 1;  // max auctions posted per tick
    // Synthetic AH Supply and Buyer Engine with Work Budgets (Issue #88)
    bool ahMarketSyntheticSupply = false;
    bool ahMarketBuyer = false;
    uint32 ahMarketBudgetUs = 2000;         // Max execution microseconds per world tick (budget cap)
    uint32 ahMarketMaxOperations = 32;      // Max work operations per world tick (op-count cap)
    uint32 ahMarketChanceSell = 10;         // Chance % per cycle to enter Gather/Post
    uint32 ahMarketChanceBuy = 10;          // Chance % per cycle to enter Buy
    // Observability telemetry UDP emitter (module-only). Zero/empty means
    // "not set here"; the emitter then falls back to the module config.
    bool observability = false;
    uint32 observabilityPort = 0;
    std::string observabilityHost;
    uint32 ahMarketBuyValue = 80;           // Buyer evaluation willingness % (relative to fair value)
    uint32 ahMarketMaxSpendPerBot = 0;      // Max spend per bot (0 = unlimited up to AH budget)
    uint32 ahMarketMaxQuality = 4;          // Max quality for synthetic supply (0=Poor..4=Epic)
    uint32 ahMarketMaxLevel = 60;           // Max required level for synthetic items
    bool ahMarketDynamicLevel = false;      // Dynamic level cap based on online players
    uint32 ahMarketLevelRefresh = 600;      // Dynamic level refresh interval in seconds
    uint32 ahMarketVariance = 10;           // Random price variance (+/- %)
    uint32 ahMarketBidMin = 75;             // Min start bid % of buyout
    uint32 ahMarketBidMax = 90;             // Max start bid % of buyout
    uint32 ahMarketTimeMin = 8;             // Min auction duration (hours)
    uint32 ahMarketTimeMax = 24;            // Max auction duration (hours)
    bool ahMarketValueVendor = true;        // Use vendor price multiplier for vendor-sold goods
    //
    bool randomBotLoginAtStartup;
    // Bounded idempotent RNDBOT population: reuse existing RNDBOT% accounts/
    // characters, create only the deficit toward the configured Min/Max target
    // (default OFF). Uses AccountMgr.CreateAccount (random password, hashed)
    // and the generic CharacterCreation::CreateCharacter synchronous
    // world-thread seam (core PR #416). Throttled to at
    // most one character per RandomBotUpdateInterval, no per-tick DB scans.
    // Valid race/class is the intersection of DBC ChrRaces/ChrClasses
    // (NOT_PLAYABLE filtered) and PlayerInfo (playercreateinfo); DBC-missing
    // combos are never selected and an empty intersection disables auto-create
    // for this process (log once). Mixed-faction cached accounts and other
    // permanently failed accounts (limit, materialization) are logged once and
    // never retried every RandomBotUpdateInterval; transient CHAR_CREATE_ERROR/
    // DB-count failures and dynamic CHAR_CREATE_DISABLED/CHAR_CREATE_PVP_TEAMS_VIOLATION
    // (faction-balance/creation-disabled, not NOT_PLAYABLE) are retryable with
    // ~60s backoff (log throttled) and do not permanently exclude healthy accounts,
    // while name collisions (NAME_IN_USE/RESERVED/PROFANE) remain silently
    // retryable (reset only on Initialize/restart). LoginDatabase allocation
    // failures are throttled to one log per ~60s and retried after the
    // interval. AccountMgr::CreateAccount queues an async LoginDatabase INSERT
    // (AllowAsyncTransactions; separate from core PR #416), so a successful CreateAccount whose id is not
    // yet visible is remembered as exactly one pending name and retried with
    // bounded/log-throttled cadence while continuing the existing-account
    // selection path and without allocating another fresh account (log once after
    // prolonged unresolved period, no duplication/spin, no 20-orphan loop). After
    // a fresh-account permanent failure, further fresh RNDBOT account
    // allocation is disabled for this process (log once) while valid existing
    // accounts remain eligible. Created GUIDs are appended to the existing
    // candidate pool so the normal Headless login path handles them; no raw
    // INSERT, no DB worker, no blocking loop.
    bool randomBotAutoCreate = false;
    // Scatter random bots on headless login to a validated level-appropriate
    // GenericRpg destination. Default off; fail-closed when no validated level
    // or no destination. Persisted ai_playerbot_zone_level is tried first
    // (with parent fallback) then immutable DBC AreaLevel/parent.
    bool enableRandomTeleports = true;
    // Whether random bots are allowed in custom isolated starting zones
    // (Blackstone Island 5536, Thalassian Highlands 5225, Alah'Thalas 2040).
    // Default false (blocked and normalized to mainland starting zones).
    bool allowIsolatedCustomStartingZones = false;
    static bool IsIsolatedCustomZone(uint32 zoneOrAreaId)
    {
        return zoneOrAreaId == 5536 || zoneOrAreaId == 5225 || zoneOrAreaId == 2040;
    }
    // Relocate a random bot that keeps dying where its level cannot survive:
    // after a successful rez, if the validated zone level exceeds bot level + 5
    // with 2+ deaths and no master/group, teleport once to a validated
    // level-fitting point and reset the death count. Default on; fail-closed.
    bool relocateHopelessDeaths = true;
    // Default-on bounded LFT fill: observe native queue (GetQueuedPlayers),
    // identify human groups/instances and missing 1/1/3 roles, filter in-memory
    // Headless random candidates by authoritative Soromeister/LFT ranges,
    // team/hardcore/state/role (AiFactory), and call core QueuePlayer through
    // native offers. Unknown ranges fail closed; no role hook or DB tick scan.
    bool randomBotLftEnabled = true;
    uint32 randomBotLftUpdateInterval = 15000;
    uint32 randomBotLftMaxFillsPerInterval = 1;
    // Issue #189: LFT role borrowing (respec a bot into a missing role) is
    // opt-in and default-off. Default keeps each bot's own spec; a missing
    // role stays empty with a logged reason.
    bool randomBotLftAllowRoleBorrow = false;
    // Issue #192: on-demand companion hiring (.bot hire + <Mercenary Hire>
    // recruiters). All costs are data, never hardcoded; the calculator in
    // runtime/HireCost.h is the single formula (party escalation for hires
    // 1-4, flat bulk rate for raid hires 5+), linearly scaled by level/60.
    bool hireEnabled = true;
    // Minimum account security that may hire (0 = every player, 1 = observer,
    // 2 = moderator, 3 = developer, 4 = administrator). Note there is no
    // separate gamemaster rank on this core (SEC_GAMEMASTER aliases admin).
    uint32 hireMinAccountSecurity = 0;
    // Maximum hired companions per player. Party hires cap at 4 regardless;
    // raise toward 39 to fill a full 40-man raid (player + hires).
    uint32 hireMaxBotsPerPlayer = 4;
    // Require the resting flag for the fast `.bot hire` command. Gossip hires
    // skip this: standing at the recruiter inside the inn is proof enough.
    bool hireRequiresResting = true;
    // Hiring fee in copper for the 1st companion at level 60 (default 15000 =
    // 1.5g). Hires 2-4 multiply by the party mults below; the full 4-bot
    // party totals ~15g at 60. Set to 0 for free hiring.
    uint32 hireBaseCostCopper = 15000;
    float hirePartyMult2 = 1.66f;
    float hirePartyMult3 = 2.66f;
    float hirePartyMult4 = 4.66f;
    // Flat fee in copper per extra raid hire (5th companion onward) at level
    // 60 (default 10000 = 1g). Set to 0 for free raid top-up.
    uint32 hireRaidFlatCostCopper = 10000;
    // Seconds a hired companion waits on guard after its master disconnects
    // before dismissing (default 300 = 5 minutes).
    uint32 hireDisconnectGracePeriod = 300;
    bool logInGroupOnly, logValuesPerTick;
    bool fleeingEnabled;
    // Universal raid survival knobs (issue #201): bomb carriers run
    // BombRunoutDistance clear, spread/split steps use HazardEvasionDistance.
    float bombRunoutDistance = 30.0f, hazardEvasionDistance = 12.0f;
    // Custom Turtle raid tactics (issue #203): Emerald Sanctum / Lower
    // Karazhan / Karazhan Crypt transition strategies.
    bool enableCustomRaidTactics = true;
    bool summonAtInnkeepersEnabled;
    std::string combatStrategies, nonCombatStrategies, reactStrategies, deadStrategies;
    std::string randomBotCombatStrategies, randomBotNonCombatStrategies, randomBotReactStrategies, randomBotDeadStrategies;
    uint32 randomBotMaxLevel;
    float randomChangeMultiplier;
    uint32 classRaceProbabilityTotal;
    uint32 classRaceProbability[MAX_CLASSES][MAX_RACES];
    bool useFixedClassRaceCounts;
    using ClassRacePair = std::pair<uint8, uint8>;
    std::map<ClassRacePair, uint32> fixedClassRaceCounts;
    ClassSpecs classSpecs[MAX_CLASSES];
    bool gearProgressionSystemEnabled;
    uint32 gearProgressionSystemItemLevels[MAX_GEAR_PROGRESSION_LEVEL][2];
    int32 gearProgressionSystemItems[MAX_GEAR_PROGRESSION_LEVEL][MAX_CLASSES][4][SLOT_EMPTY];
    std::string commandPrefix, commandSeparator;
    std::string randomBotAccountPrefix;
    // AiPlayerbot.RandomBotPoolReset (issue #265): 'off', 'once:<token>' or
    // 'always'. Parsed by the reset planner during initial world startup only;
    // it is read here so `bot pool status` can report the requested value.
    std::string randomBotPoolReset;
    // Character names matched using the database's stored `characters.name`
    // collation; pinned bots stay online and are exempt from timed logout
    // (native login teleport is skipped separately via the facade's normalized
    // name match, best-effort) but still gated by RandomBotLoginWithPlayer=1.
    // Resolved once to GUIDs in the discovered RNDBOT pool.
    std::list<std::string> pinnedBotNames;

	bool RandombotsWalkingRPG;
	bool RandombotsWalkingRPGInDoors;
    bool boostFollow;
    bool turnInRpg;
    bool globalSoundEffects;
    bool shareTargets;
	std::list<uint32> pvpProhibitedZoneIds;
    bool enableGreet;
    bool randomBotShowHelmet;
    bool randomBotShowCloak;
    bool disableRandomLevels;
    bool instantRandomize;
    bool gearscorecheck;
    int32 levelCheck;
	bool randomBotPreQuests;
    float playerbotsXPrate;
    bool disableBotOptimizations;
    bool disableActivityPriorities;
    bool forceActiveWhenNearPlayer;
    bool limitCombatActivity;
    bool guildOrderAlwaysActive;
    uint32 botActiveAlone;
    uint32 diffWithPlayer;
    uint32 diffEmpty;
    uint32 minEnchantingBotLevel;
    // Ordered pull: DPS hold after the tank's pull lands (pull) or after the
    // tank is back at the anchor (pullback). Per-command [seconds] overrides
    // the defaults; the return leg is hard-capped by pullBackMaxReturnTime.
    uint32 pullDpsJoinDelay;
    uint32 pullBackDpsJoinDelay;
    uint32 pullBackMaxReturnTime;
    uint32 randombotStartingLevel;
    // Level seeding for persistent-level pools (DisableRandomLevels=1). Fresh
    // pool bots are created at level 1 by CharacterCreation, so the pool
    // starts at 1 unless this range seeds it. 1/1 keeps the historic
    // "walk from level 1" behaviour.
    uint32 randomBotStartLevelMin;
    uint32 randomBotStartLevelMax;
    bool randomBotSayWithoutMaster;
    bool randomBotInvitePlayer;
    bool randomBotGroupNearby;
    bool randomBotRaidNearby;
    bool randomBotGuildNearby;
    bool randomBotFormGuild;
    bool randomBotRandomPassword;
    bool inviteChat;
    bool botsSilent;
    // Opt-in diagnostic logging: when true, [BOT] log lines and per-bot action
    // log files (logs/bots/<name>_acc<id>_<timestamp>.log) are emitted. Default
    // off so production servers don't pay disk I/O / branch overhead.
    bool enableActionLog;
    // Filename (relative to LogsDir) for the bot subsystem log. When set,
    // all sLog calls from bot .cpp files are redirected there instead of
    // writing to the main server log. Default: "bots.log". Empty = disabled.
    std::string botLogFile;
    bool enableOffSpecStrategies;
    bool useWanderAsDefaultFollowStrategy;
    std::string defaultFormation;

    uint32 guildMaxBotLimit;

    bool enableBroadcasts;
    uint32 broadcastChanceMaxValue;

    uint32 broadcastToGuildGlobalChance;
    uint32 broadcastToWorldGlobalChance;
    uint32 broadcastToGeneralGlobalChance;
    uint32 broadcastToTradeGlobalChance;
    uint32 broadcastToLFGGlobalChance;
    uint32 broadcastToLocalDefenseGlobalChance;
    uint32 broadcastToWorldDefenseGlobalChance;
    uint32 broadcastToGuildRecruitmentGlobalChance;
    uint32 broadcastToSayGlobalChance;
    uint32 broadcastToYellGlobalChance;

    uint32 broadcastChanceLootingItemPoor;
    uint32 broadcastChanceLootingItemNormal;
    uint32 broadcastChanceLootingItemUncommon;
    uint32 broadcastChanceLootingItemRare;
    uint32 broadcastChanceLootingItemEpic;
    uint32 broadcastChanceLootingItemLegendary;
    uint32 broadcastChanceLootingItemArtifact;

    uint32 broadcastChanceQuestAccepted;
    uint32 broadcastChanceQuestUpdateObjectiveCompleted;
    uint32 broadcastChanceQuestUpdateObjectiveProgress;
    uint32 broadcastChanceQuestUpdateFailedTimer;
    uint32 broadcastChanceQuestUpdateComplete;
    uint32 broadcastChanceQuestTurnedIn;

    uint32 broadcastChanceKillNormal;
    uint32 broadcastChanceKillElite;
    uint32 broadcastChanceKillRareelite;
    uint32 broadcastChanceKillWorldboss;
    uint32 broadcastChanceKillRare;
    uint32 broadcastChanceKillUnknown;
    uint32 broadcastChanceKillPet;
    uint32 broadcastChanceKillPlayer;

    uint32 broadcastChanceLevelupGeneric;
    uint32 broadcastChanceLevelupTenX;
    uint32 broadcastChanceLevelupMaxLevel;

    uint32 broadcastChanceSuggestInstance;
    uint32 broadcastChanceSuggestQuest;
    uint32 broadcastChanceSuggestGrindMaterials;
    uint32 broadcastChanceSuggestGrindReputation;
    uint32 broadcastChanceSuggestSell;
    uint32 broadcastChanceSuggestSomething;

    uint32 broadcastChanceSuggestSomethingToxic;

    uint32 broadcastChanceSuggestToxicLinks;
    std::string toxicLinksPrefix;
    uint32 toxicLinksRepliesChance;

    uint32 broadcastChanceSuggestThunderfury;
    uint32 thunderfuryRepliesChance;

    uint32 broadcastChanceGuildManagement;

    uint32 guildRepliesRate;

    uint32 botAcceptDuelMinimumLevel;

    bool talentsInPublicNote;
    bool nonGmFreeSummon;

    BotSelfBotLevel selfBotLevel;
    uint32 iterationsPerTick;
    // Issue #84: bounded failure backoff tuning. Zero base/max disables.
    uint32 failedActionRetryBaseMs;
    uint32 failedActionRetryMaxMs;
    uint32 failedActionCacheTtlMs;
    uint32 failedActionCacheMaxEntries;

    std::string autoPickReward;
    bool autoEquipUpgradeLoot;
    bool syncQuestWithPlayer;
    bool syncQuestForPlayer;
    std::string autoTrainSpells;
    std::string autoPickTalents;
    bool autoLearnTrainerSpells;
    bool autoLearnQuestSpells;
    bool autoLearnDroppedSpells;
    bool autoDoQuests;
    bool autoShareConjuredOnTrade;
    // TravelNode cache generation can load every map and rewrite its database
    // cache. Keep it opt-in; direct movement and quest destinations do not
    // require a generated graph for the owned-bot MVP.
    bool generateTravelNodes;
    bool asyncTravelPartitions;
    // Fish-location generation scans populated grids and writes the resulting
    // points to WorldDatabase. Keep the expensive persistent rebuild opt-in.
    bool generateFishLocations;
    bool syncAltLevelToMaster;
    uint32 tweakValue; //Debugging config
    float respawnModNeutral, respawnModHostile;
    uint32 respawnModThreshold, respawnModMax;
    bool respawnModForPlayerBots, respawnModForInstances;

    bool randomBotLoginWithPlayer;
    // Autonomous BG queue for WSG/AB/AV. Default off. Bounded cadence and
    // max-per-interval, in-memory Headless/random selection, faction/level
    // bracket/state/deserter/taxi/combat/queue checks, native handler
    // ownership/invites/queue updates via HandleBattlemasterJoinOpcode (guid
    // 1337 bypass) + SMSG_BATTLEFIELD_STATUS/BGStatusAction invite path.
    // No second queue/thread/arena/vehicle/expansion or DB tick scans.
    bool randomBotBgEnabled = false;
    uint32 randomBotBgQueueInterval = 30000;
    uint32 randomBotBgMaxQueuePerInterval = 1;

    bool jumpInBg;
    bool jumpWithPlayer;
    bool jumpFollow;
    bool jumpChase;
    bool useKnockback;
    float jumpNoCombatChance;
    float jumpMeleeInCombatChance;
    float jumpRandomChance;
    float jumpInPlaceChance;
    float jumpBackwardChance;
    float jumpHeightLimit;
    float jumpVSpeed;
    float jumpHSpeed;

    std::mutex m_logMtx;

    std::list<std::string> allowedLogFiles;
    std::list<std::string> debugFilter;

    std::unordered_map <std::string, std::pair<FILE*, bool>> logFiles;

    uint32 botCheatMask = 0;
    uint32 rndBotCheatMask = 0;

    std::vector<std::string> BotCheatMaskName = { "taxi", "gold", "health", "mana", "power", "item", "cooldown", "repair", "movespeed", "attackspeed", "breath", "quest", "maxMask" };

    struct worldBuff{
        uint32 spellId;
        uint32 factionId = 0;
        uint32 classId = 0;
        uint32 specId = 0;
        uint32 minLevel = 0;
        uint32 maxLevel = 0;
        uint32 eventId = 0;
    };

    std::vector<worldBuff> worldBuffs;

    bool perfMonEnabled;
    bool bExplicitDbStoreSave = false;

    //LM BEGIN
    std::string llmApiEndpoint, llmApiKey, llmApiJson, llmPrePrompt, llmPreRpgPrompt, llmPrompt, llmPostPrompt, llmResponseStartPattern, llmResponseEndPattern, llmResponseDeletePattern, llmResponseSplitPattern;
    uint32 llmEnabled, llmContextLength, llmBotToBotChatChance, llmGenerationTimeout, llmMaxSimultaniousGenerations, llmRpgAIChatChance;
    bool llmGlobalContext;
    ParsedUrl llmEndPointUrl;
    std::set<uint32> llmBlockedReplyChannels;
    //LM END

    uint32 EatDrinkMinDistance = 5;
    uint32 EatDrinkMaxDistance = 1000;

    std::string GetValue(std::string name);
    void SetValue(std::string name, std::string value);

    void loadFreeAltBotAccounts();

    std::string GetTimestampStr();

    bool hasLog(std::string fileName) { return std::find(allowedLogFiles.begin(), allowedLogFiles.end(), fileName) != allowedLogFiles.end(); };
    bool openLog(std::string fileName, char const* mode = "a", bool haslog = false);
    bool isLogOpen(std::string fileName) { auto it = logFiles.find(fileName); return it != logFiles.end() && it->second.second;}
    // Writes the line verbatim. Nearly every caller here hands over text it has
    // already assembled, and a percent sign anywhere in it - a bot name, a mob
    // name, an item name - used to be read as a conversion specifier. glibc
    // printed nonsense; the Microsoft runtime aborted the server.
    void log(std::string fileName, const char* line);

    // The formatting variant, for the callers that actually pass arguments.
#if defined(__GNUC__) || defined(__clang__)
    void logf(std::string fileName, const char* format, ...)
        __attribute__((format(printf, 3, 4)));
#else
    void logf(std::string fileName, const char* format, ...);
#endif

    void logEvent(PlayerbotAI* ai, std::string eventName, std::string info1 = "", std::string info2 = "");
    void logEvent(PlayerbotAI* ai, std::string eventName, ObjectGuid guid, std::string info2);

    bool CanLogAction(PlayerbotAI* ai, std::string actionName, bool isExecute, std::string lastActionName);

private:
    void LoadTalentSpecs();
    void LoadLLMDefaultPrompts(const std::string& fileName);

    Config config;
};

#define sPlayerbotAIConfig MaNGOS::Singleton<PlayerbotAIConfig>::Instance()
