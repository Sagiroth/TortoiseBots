#pragma once

#include <string>
#include <unordered_set>
#include "Config/Config.h"
#include "Talentspec.h"
#include "ActionLogPolicy.h"
#include "LogFileRotation.h"
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
    // The run leader's keys (AiPlayerbot.DungeonSelfClear and AiPlayerbot.DungeonRun*) and the `.reload`-able combat
    // keys (AiPlayerbot.InterruptIgnoresThreat, AiPlayerbot.SpellFacingPrecheck), read from the loaded aiplayerbot.conf:
    // by Initialize, and again by ReloadDungeonRunKeys after a `.reload config`.
    void LoadDungeonRunKeys();
    // The two-file gate's aiplayerbot.conf half (AiPlayerbot.ObservabilityRaids): read by Initialize, and again by
    // ReloadDungeonRunKeys, so the file that turned the feed on can turn it off on a `.reload config`.
    void LoadObservabilityRaids();
    // Reads aiplayerbot.conf again and only the keys that follow a `.reload config` (the run leader's, the raids
    // observability flag; the rest of the settings keep their start values). World thread only
    // (BotHostAdapter::OnUpdate). False when the AI is disabled or the file could not be read; the keys are then
    // left as they were.
    bool ReloadDungeonRunKeys();
    bool IsInRandomAccountList(uint32 id);
    bool IsFreeAltBot(uint32 guid);
    bool IsFreeAltBot(Player* player) {return IsFreeAltBot(player->GetGUIDLow());}
    bool IsInRandomQuestItemList(uint32 id);
	bool IsInPvpProhibitedZone(uint32 id);

    bool enabled;
    uint32 globalCoolDown, reactDelay, maxWaitForMove, expireActionTime, dispelAuraDuration, passiveDelay, repeatDelay,
        rpgDelay, sitDelay, returnDelay, lootDelay;
    float sightDistance, spellDistance, reactDistance, grindDistance, lootDistance, groupMemberLootDistance,
        gatheringDistance, groupMemberGatheringDistance, groupMemberGatheringDistanceWithActiveMaster, shootDistance,
        fleeDistance, tooCloseDistance, meleeDistance, followDistance, raidFollowDistance, wanderMinDistance, wanderMaxDistance, whisperDistance, contactDistance,
        aoeRadius, rpgDistance, targetPosRecalcDistance, farDistance, healDistance, healDistanceBg, aggroDistance, proximityDistance, walkDistance;
    uint32 criticalHealth, lowHealth, mediumHealth, almostFullHealth;
    uint32 lowMana, mediumMana;

    uint32 openGoSpell;
    bool randomBotAutologin;
    std::list<uint32> randomBotQuestItems;
    std::list<uint32> randomBotAccounts;
    std::unordered_set<uint32> nonRandomBotAccounts;
    std::list<uint32> randomBotSpellIds;
    std::list<uint32> immuneSpellIds;
    std::list<std::pair<uint32, uint32>> freeAltBots;
    std::list<std::string> toggleAlwaysOnlineAccounts;
    std::list<std::string> toggleAlwaysOnlineChars;
    bool enableMinimalMove;
    uint32 transportTeleportType;
    uint32 randomGearMaxLevel;
    uint32 randomGearMaxDiff;
    // Owner rule (Oct 2026): generated gear is bounded by the bot's own level,
    // not only by randomGearMaxLevel. Pool bots (fresh seed and every later
    // regear) cap at level + randomGearMaxItemLevelAboveLevel; the standard
    // (tier 0) hire kit at level + hireGearMaxItemLevelAboveLevel. 0 disables
    // the above-level part. Earned gear (loot, quests, vendor/AH) is uncapped.
    uint32 randomGearMaxItemLevelAboveLevel;
    uint32 hireGearMaxItemLevelAboveLevel;
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
    bool randomBotTimedLogout;
    // Level ladder (AiPlayerbot.LevelLadder*): choose the next random bot to log in by the
    // shortfall of its level band instead of round-robin over the whole pool, highest level
    // first inside the band, so the field spreads over the levels and the leaders climb to 60.
    bool levelLadder = true;
    uint32 levelLadderBandSize = 5;
    uint32 levelLadderMaxLevelSharePct = 10;
    uint32 levelLadderLogMinutes = 5;
    uint32 maxRandomBotInWorldTime;
    uint32 randomBotStallRelogMinutes = 45;
    uint32 minRandomBotRandomizeTime, maxRandomBotRandomizeTime;
    uint32 minRandomBotChangeStrategyTime, maxRandomBotChangeStrategyTime;
    uint32 randomBotsMaxLoginsPerInterval;
    uint32 maxRandomBotsPriceChangeInterval;
    //Auction house settings
    bool shouldQueryAHListingsOutsideOfAH;
    std::list<uint32> ahOverVendorItemIds;
    std::list<uint32> vendorOverAHItemIds;
    std::list<uint32> ahSellBlacklist;
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
    // Issue #1101: the DUNGEON_MOBS datagram (mobs per dungeon run); only acts while observability is on.
    bool observabilityDungeonMobs = true;
    // Spec dash-raids-tab: the RAIDS datagram and the observed-raid combat sampler (leader, offline members and the live
    // fight window of unbound raid groups); only acts while observability is on, off by default.
    bool observabilityRaids = false;
    // Raids tab detail (ready check, boss debuffs): appended at the END of the RAIDS line behind
    // AiPlayerbot.ObservabilityRaidsDetail, sent at most every 5 s. Off by default.
    bool observabilityRaidsDetail = false;
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
    // Evenly spread new random bots across the six racial starting zones:
    // auto-create picks a valid race from the least-populated start zone and
    // isolated-custom-zone normalization sends the bot to the least-populated
    // standard start of its faction. Default true; false keeps the old
    // behaviour (uniform random race, goblins to Valley of Trials, high
    // elves to Northshire).
    bool randomBotEvenStartZones = true;
    // % of new random bots that are Alliance (0-100); the rest are Horde.
    // 50 = no bias (today's behaviour). Rolls faction first, then the start
    // zone inside that faction (weights below, else even/uniform). Narrowed
    // by AllowTwoSide/account faction limits; empty after narrowing falls
    // back to the full account-eligible list.
    uint32 randomBotFactionRatio = 50;
    // Optional per-start-zone weights for new random bots, e.g.
    // "Durotar=100,Elwynn=50". Empty = today's even spread. Malformed or
    // all-zero text logs an error and falls back to the even spread.
    std::string randomBotStartZoneWeightsText;
    uint32 randomBotStartZoneWeights[6] = {};
    bool randomBotStartZoneWeightsValid = false;
    // Issue #1003: share of new bot names per style (race-flavoured /
    // compound / fun) and an optional extra names file (read once).
    uint32 randomBotNameStyleWeights[3] = { 55, 20, 25 };
    std::string randomBotNamesFile;
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
    // Repop brings a random masterless bot back at the nearest graveyard instead of its
    // racial starting area, so evacuations stop draining bots into the starting zones.
    bool repopAtGraveyard = true;
    // Turtle mount (quest 40302 "Torta's Egg" -> item 23720 -> spell 30174):
    // minimum level at which random bots learn the Swift Riding Turtle.
    // 0 disables. Default 18.
    uint32 turtleMountAtLevel = 18;
    // Organic level-up mounts: a random pool bot that dings 40 or 60 learns
    // the same racial/collection mount set the seed/hire path grants (slow at
    // 40, epic at 60; riding skill is level-gated separately). 1 = on; 0 =
    // off, so a bot levelling from 1 only gets a 40/60 mount when re-seeded.
    // Default true.
    bool levelUpMounts = true;
    // Random bots ride racial mounts only by default: item-backed shop mounts
    // (any race can use them, except the Swift Riding Turtle) are granted and
    // chosen only when this is on. Player-owned alts are never gated. Default false.
    bool randomBotShopMounts = false;
    // Safety factor on the mount break-even distance: bots mount for travel
    // only when the remaining trip exceeds
    // 7 y/s * castTime / (mountSpeed/100) * this. 0 disables the threshold
    // (mount for any trip). Default 1.5.
    float mountBreakEvenFactor = 1.5f;
    // Avoid hostile towns: a random masterless bot refuses travel destinations and
    // grind targets sitting among guards hostile to its team (12.6% of deaths are
    // +13-level town guards). Static spawn data, scanned once per candidate point.
    bool avoidHostileTowns = true;
    // "Long stuck" hearths a random masterless bot only when its homebind area is within
    // 10 levels of the bot; otherwise it repops instead of flying home to a zone it outgrew.
    bool unstuckHearthLevelFit = true;
    // Minimum distance (yards) from its own homebind a "long stuck" bot must be before a
    // hearthstone makes sense: a level 1 bot's whole starting zone is a few hundred yards
    // wide, so hearthing there only threw it back into the same camp with a 30-minute
    // cooldown burnt (478 uses/100 min, 95% within 300 yd of the homebind, 2026-09-29).
    // Closer than this the unstuck chain repops instead. 0 disables the gate.
    float unstuckHearthMinDistance = 300.0f;
    // Stuck-travel teleport (default on) (mod-playerbots MoveFarTo): a masterless pool
    // bot whose trip hits the six-failed-move drop teleports to its destination
    // instead (same map, unwatched, once per 10 min). Owned/hired bots and bots
    // with a real master never teleport. Default off.
    bool stuckTravelTeleport = true;
    // Auction-house visit budget for the guild-order auction leg: an arrived
    // trip with no auctioneer in interact range moves the leg down the ladder
    // after this many visits. No config gate by design: it ends no order, it
    // only moves the leg on after 3 visits like the pick streak does. Tests
    // pin the constant (see tools/test_guild_order_giveup_policy.cpp).
    uint32_t guildOrderAuctionVisits = 3;
    // Quest-log upkeep for masterless random bots: drop finished quests that
    // can no longer be rewarded, refuse grey quests unless the reward is an
    // equip upgrade, and prefer turn-in travel once 2 quests are finished.
    // Default on; off restores the old accept-all / clean-only-grey /
    // hand-in-at-5 behaviour.
    bool botQuestLogUpkeep = true;
    // Leave outgrown zones: a random masterless bot that outlevels its zone walks to a
    // level-fitting grind hub, avoids low-level services except in capitals, and refuses
    // low-level homebinds. Default on; fail-closed on unknown area levels.
    bool leaveOutgrownZones = true;
    // Vendor trip thresholds (#339): the stock a bot must carry before it walks to a
    // vendor on its own. Either the sell value covers the money missing for the next
    // trainable class rank, or the batch rules below fire. Bots below level 5 use the
    // LowLevel* pair instead: a fresh bot carries one or two greys, not a bag of them.
    uint32 vendorBatchMinCount = 8;
    uint32 vendorBatchMinBagSpace = 60;
    uint32 lowLevelVendorBatchMinCount = 3;
    uint32 lowLevelVendorBatchMinBagSpace = 25;
    // Radius in yards of the vendor trip a bot below level 5 may make. The blanket
    // "no RPG travel below level 5" rule exists because the path to an NPC usually
    // crosses level 5+ mobs; a beginner may still reach its own camp vendor, so the
    // trip is capped to this radius instead (and to the starting-zone level band).
    float lowLevelVendorMaxDistance = 600.0f;
    // Per-visit microseconds a travel-destination pick may spend scanning
    // candidates before yielding to the next visit (issue #642). 0 = no
    // slicing (the whole pick runs in one visit, the old behavior).
    uint32 travelPickBudgetUs = 4000;
    // Bot-only green/blue drop boost. Masterless pool bots roll each quality-2/3
    // entry of a creature's loot template one extra time per kill, at
    // (multiplier - 1) x the entry's DB chance, so the expected number of
    // greens/blues per kill is roughly the multiplier times the normal rate.
    // Players and hired/alt bots keep the server's own rates; group loot is never
    // touched. 1.0 = off (no extra roll at all).
    float botLootRateUncommon = 1.0f;
    float botLootRateRare = 1.0f;
    // Default-on bounded LFT fill: observe native queue (GetQueuedPlayers),
    // identify human groups/instances and missing 1/1/3 roles, filter in-memory
    // Headless random candidates by authoritative Turtle client LFT ranges,
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
    // separate gamemaster rank on this core (SEC_GAMEMASTER aliases developer, 3).
    uint32 hireMinAccountSecurity = 0;
    // Maximum hired companions per player. Party hires cap at 4 regardless;
    // raise toward 39 to fill a full 40-man raid (player + hires).
    uint32 hireMaxBotsPerPlayer = 4;
    // Issue #936: how many levels above the hirer a `.bot hire ... level=<n>`
    // companion may be (0 = never above the hirer; a GM is not limited by it).
    uint32 hireMaxLevelAboveHirer = 0;
    // Accounts with this security level or more hire for free (no gold taken,
    // reply says "free (GM)"). 0 = off. runtime/HireFeePolicy.h is the rule.
    uint32 hireFreeMinSecurity = 0;
    // Companions are hired at a <Mercenary Hire> recruiter. When set, the fast
    // `.bot hire` command also works anywhere; fees are the same either way.
    bool hireAnywhere = false;
    // Hires stay out of battlegrounds when unset: queueing warns the master,
    // entry dismisses them, and hiring is refused while queued or inside.
    // Set for today's behaviour (hires may go to battlegrounds).
    bool hireAllowPvP = false;
    // Hired companions loot and roll by default. Unset: a hire never opens a
    // corpse and passes on every roll (pool bots and alts are unaffected).
    bool hireLoot = true;
    // Issue #1125: the experimental 7.3 shaman tank build. 0 withholds it from
    // the recruiter spec menu, `.bot hire`, raid hire and the random pool roll.
    bool shamanTankEnabled = true;
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
    // Issue #900: `.bot hire raid`. Class spread per role (round-robin) and
    // the named presets; both are parsed at use, the defaults live in
    // runtime/HireRaidPolicy.h and equal the shipped template.
    std::string hireRaidSpread;
    std::string hireRaidPresets;
    // Named gear presets for `.bot gear <target> preset <name>`; parsed at
    // use, the default lives in runtime/policy/hire/BotGearPolicy.h and
    // equals the shipped template.
    std::string botGearPresets;
    // Issue #490: static best-in-slot sets for level-60 hires (tiers 1-7).
    // The master switch gates the static pass; tier 0 always keeps today's
    // random hire gear. A player may hire at any tier from 0 up to the
    // higher of HireGearStartTier and their highest rewarded unlock quest.
    bool hireGearTiersEnabled = true;
    // Tier every player has from the start (0..7, clamped); 7 skips the
    // whole ladder.
    uint32 hireGearStartTier = 0;
    // Unlock quests rewarded on any character of the account count.
    bool hireGearUnlockAccountWide = false;
    // All seven unlock quests offered at once (no PrevQuestId chain).
    // Written into the quests at startup, live after the next restart.
    bool hireGearAllQuestsAvailable = false;
    // Map ids whose bosses are credited automatically when an unlock quest
    // is taken (instance too hard or not scripted). Their loot stays.
    std::set<uint32> hireGearOptionalMaps;
    // Hire fee multiplier per gear tier 0..7.
    float hireGearTierCostMult[8] = { 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 6.0f };
    // Issue #492: world buffs at the six capital <Mercenary Hire> recruiters.
    // Unlock fees (copper) are written into the unlock quests at startup and
    // apply from the next restart; the pairs are per purchase (base +
    // per-person, copper).
    // Code fallbacks equal the shipped template values.
    bool worldBuffsEnabled = true;
    bool worldBuffsKeepInRaids = false;
    uint32 worldBuffsMinLevel = 60;
    uint32 worldBuffsPriceBaseCopper = 100000;
    uint32 worldBuffsPricePerPersonCopper = 20000;
    uint32 worldBuffsSaygePriceBaseCopper = 60000;
    uint32 worldBuffsSaygePricePerPersonCopper = 10000;
    uint32 worldBuffsSongflowerPriceBaseCopper = 40000;
    uint32 worldBuffsSongflowerPricePerPersonCopper = 10000;
    uint32 worldBuffsSilithystPriceBaseCopper = 20000;
    uint32 worldBuffsSilithystPricePerPersonCopper = 4000;
    uint32 worldBuffsUnlockFeeRaidCopper = 2000000;
    uint32 worldBuffsUnlockFeeCopper = 1000000;
    // Ammo stacks (200 rounds each) a warrior or rogue with a bow/gun/crossbow carries; hunters keep 5 + level / 10.
    uint32 ammoStacksNonHunter = 2;
    // Issue #473: opt-in alternative play style for owned/hired bots only.
    // Every part defaults off and never touches pool bots. The maintenance
    // command refreshes one owned bot's kit (consumables/reagents/ammo/
    // repair); autogear re-gears an owned bot to its level within the
    // quality/ilvl caps; summon-on-group-accept teleports the joining bot
    // to its inviter when the extra condition knobs pass.
    bool ownedBotMaintenanceEnabled = false;
    bool ownedBotAutogearEnabled = false;
    uint32 ownedBotAutogearQualityCap = 2;
    uint32 ownedBotAutogearIlvlCap = 0;
    // `.autogear` on real players: 0 = GM-only, 1 = every player may gear
    // only themselves. GMs are never restricted.
    bool playerAutogearForAll = false;
    bool ownedBotSummonWhenGroup = false;
    bool ownedBotSummonAllowInCombat = false;
    bool ownedBotSummonAllowMasterDead = false;
    bool ownedBotSummonAllowBotDead = false;
    bool ownedBotSummonRevive = false;
    bool ownedBotSummonRepair = false;
    // Seconds between summon-on-accept teleports per bot (default 300 = 5
    // minutes). An uninvite/invite cycle inside the window is refused, so it
    // cannot be macroed into a free in-combat rez/repair.
    uint32 ownedBotSummonCooldown = 300;
    // Issue #884: remote commander. A GM or the owner of every bot binds a
    // bot-only group of up to five to themselves (`.bot commander bind`) and
    // leads it from anywhere with GUID-targeted orders (`.bot cmd`). Off by
    // default: with 0 both commands answer that the feature is off.
    bool remoteCommander = false;
    // Issue #884, phase 2: milliseconds between two TBM:VIEW frames to a bound
    // commander whose feed is on (`.bot commander view on|off`). Clamped to
    // 250..60000; default 1000.
    uint32 remoteCommanderViewMs = 1000;
    // A bot's failed dungeon entrance condition (Onyxia's lair asks for the Drakefire
    // Amulet) is waived when its commander is a real player who commands the bot's raid
    // (a member of it, or the binding's commander) and meets it himself. On by default.
    bool raidEntryByCommander = true;
    // Run trace (docs/server/run-trace.md): `.bot trace start <run_id>` records
    // the party's bots as bt-trace v1 JSONL. Off by default; with 0 the command
    // answers that the feature is off.
    bool runTrace = false;
    // Milliseconds between two trace frames (250..10000).
    uint32 runTraceIntervalMs = 500;
    // Output directory; relative paths are under LogsDir.
    std::string runTraceDir = "traces";
    // Only a GameMaster may start a trace (traces take disk space); with 0 the
    // owner of the bots may too.
    bool runTraceGmOnly = true;
    // Size cap of one trace file, in MiB (1..1024); the trace stops when reached.
    uint32 runTraceMaxFileMb = 64;
    // Decision capture (docs/server/decision-capture.md): a running run leader's
    // every policy tick is written to a rotating JSONL capture, so a live
    // failure can become a replay fixture. Off by default; test servers only.
    bool decisionCapture = false;
    // Output folder; relative paths are under LogsDir.
    std::string decisionCaptureDir = "decisions";
    // One segment rotates past this many MiB (1..256).
    uint32 decisionCaptureSegmentMiB = 32;
    // A capture's segments past this many MiB are deleted oldest first
    // (SegmentMiB..4096).
    uint32 decisionCaptureCaptureMiB = 512;
    // The whole folder past this many MiB drops its oldest ended captures
    // (CaptureMiB..65536).
    uint32 decisionCaptureTotalMiB = 4096;
    // A larger record becomes a stub (64..4096 KiB).
    uint32 decisionCaptureRecordKiB = 512;
    // A full keyframe every this many records (1..600).
    uint32 decisionCaptureKeyframeEvery = 60;
    // Issue #1012: creature entries a commanded bot may talk to with `.bot cmd
    // gossip` (AiPlayerbot.BotGossipNpcs). Empty by default: the order is off.
    std::set<uint32> botGossipNpcs;
    bool logInGroupOnly, logValuesPerTick;
    bool fleeingEnabled;
    // Opt-in: pool warriors and rogues get no generic "flee" combat strategy
    // (panic / outnumbered / critical health no master) and fight on.
    bool poolMeleeHoldGround;
    // Issue #872: an owned alt kicked from its owner's party keeps playing on
    // its own (AltBotService). Off by default; the second key logs such alts
    // out together with their owner instead of leaving them in the world.
    bool altBots;
    bool altBotsLogoutWithOwner;
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
    bool boostFollow;
    // Inside an instance, a group follows in a 150 degree wedge behind the leader
    // (RaidFollowWedgePolicy.h), with far stragglers walking the leader's trail.
    bool raidFollowWedge;
    // Dungeon follow holds short of unpulled packs when the leader is far ahead (audit fix 127).
    bool dungeonFollowHoldBeforePacks;
    // Warlock summons the demon that fits its spec in PvE instead of the Voidwalker (opt-in).
    bool warlockPetBySpec;
    // Warlock raid rotation: DoT/curse life gate, raid spread cap, trash-only
    // Drain Soul and a pay-as-needed combat Life Tap (opt-in).
    bool warlockRaidRotation;
    // Rogue Feint (and Vanish as the last resort) on medium/high threat in 5-man dungeons (opt-in).
    bool rogueDungeonThreatDrops;
    bool sapApproachAvoidPack;
    bool warriorGapCloser;
    // Hunter swaps to Aspect of the Viper on low mana and back to Hawk once mana recovers (opt-in).
    bool hunterViperOnLowMana;
    // Hunters cast Carve only on a target already in melee reach, never walking in for it.
    bool hunterCarveInReachOnly;
    // Paladin bots keep Judgement of Wisdom / Light up on the boss in groups (opt-in).
    bool paladinJudgementUpkeep;
    // Onyxia phase 2 whelp duty for AoE ranged DPS and egg-cave avoidance (opt-in).
    bool onyxiaWhelpsAndEggs;
    bool dungeonTactics;
    // `.bot run`: the run leader (default on).
    bool dungeonSelfClear;
    // Raid bindings for the run leader (`.bot commander bind <LeadBot> raid`); read with dungeonSelfClear.
    bool dungeonRunRaid = false;
    // A raid binding's boss fight: a 1200 s ceiling, deaths that do not pause it while it loses hp, and the adds
    // sent to the off-tanks instead of the main tank (default off: the party rules).
    bool dungeonRunRaidBossRules = false;
    // Healer seats of a raid binding go by class instead of member order (default off: member order).
    bool raidHealerSeatsByClass = false;
    // A layout tank healer's second seat on its tank helps the raid while the tank is quiet (default off).
    bool raidTankHealerAssist = false;
    // A raid boss fight's healer guard: an add on a healer goes to the nearest ranged dps, up to two, and the off-tanks
    // take such an add only when none is free (only with dungeonRunRaidBossRules; default off, the off-tanks take it).
    bool dungeonRunRaidHealerGuard = false;
    // Pure interrupts (Kick, Pummel, Shield Bash, Bash, Counterspell, Silence) skip the damage threat back-off the way
    // heals do, so a damage dealer at the 80 % high-threat line still stops a cast, while the interrupt would not pull
    // its target off the tank (bot threat plus the spell's flat threat under 110 % of the tank's in melee, 130 %
    // ranged). Earth Shock and Death Coil interrupt rows stay gated. Off = as before (vetoed at the high line).
    bool interruptIgnoresThreat = false;
    // PlayerbotAI::CanCastSpell refuses a unit-target cast the target's facing makes impossible (Gouge from behind,
    // Backstab from the front) before it probes the core's CheckCast, which broadcasts a spell failure on every probe.
    // Off = as before (the probe runs, and a Gouge from behind reads as castable and fails in the real cast).
    bool spellFacingPrecheck = false;
    // The threat back-off counts a single-target damage cast's own expected threat (average direct damage, threat
    // modifiers applied): the cast is held while the bot's threat plus it would reach the tank's threat on the target,
    // so a slow nuke started under the 80 % high-threat line no longer lands past the core's pull line. Read with the
    // run keys, so `.reload config` applies it. Off = as before (only the threat at cast start is checked).
    bool threatCapCountsCastThreat = false;
    // Mortal Wound stacks at which an opt-in "fankriss tank" swaps (1-9).
    uint32 fankrissSwapStacks = 5;
    // Seconds the DPS hold Patchwerk once an opt-in "patchwerk main" engages him (0 = off, else 3-5).
    uint32 patchwerkOpeningDelay = 4;
    // The linked_death boss tactic ("die together"): free DPS bots equalise a group's health, hold every member at or
    // below the row's param % and burn them together once all are there (BossTacticsPolicy.h; its own key on top of
    // the tactics gate, default off).
    bool dungeonRunLinkedDeath = false;
    // The aura tactic kinds and their row gates (dps_hold_after, dispel_priority, flee_on_aura, freeze_on_aura,
    // taunt_after_cast, swap_on_aura, and a row's hp_min/hp_max/bot_aura gate): on, the fight scan keeps them (its own
    // key on top of the tactics gate). Off, the six kinds are ignored and a gated row is closed, so the table behaves
    // exactly as before them (BossTacticsPolicy.h RowGateOpen; default off).
    bool dungeonRunAuraTactics = false;
    // The needs_aura boss-tactic kind (a bot without the row's aura never targets that unit, and the run guard sends
    // nobody at it): on, the rows are read; off, the rows load but nothing reads them (its own key on top of the
    // tactics gate; default off).
    bool dungeonRunAuraGatedTargets = false;
    // The run leader's respawn-nest rules (DungeonRun::NestLeavable, GatherAnchorInNest): a unit that respawns within a
    // minute and stands at its spawn, and hits nobody in the party, does not hold a fight open, and a regroup never
    // gathers on one (default on).
    bool dungeonRunRespawnNest = false;
    // The boss-tactic kill_first candidates come from a grid search too, not only from the bot's own attackers list:
    // off, the candidates are the fight's units as today (its own key; default off).
    bool dungeonRunKillFirstGrid = false;
    // A kill-first focus on a totem is DPS-only and goes out before the guard, and the guard skips totems: off, the
    // focus sends every bot and the guard picks totems, as today (its own key; default off).
    bool dungeonRunTotemFocus = false;
    // The use_object boss-tactic kind (one bot clicks the ready game object nearest the boss; mode tank_anchor also
    // walks the boss over): on, the fight scan keeps the rows (its own key on top of the tactics gate). Off, the
    // rows are ignored, so the table behaves exactly as before the kind (default off).
    bool dungeonRunUseObject = false;
    // The swap_on_aura boss-tactic kind (the tanks of the row's rotation trade the boss at a stack count, and the
    // swapped-off tank holds its threat): on, the rows are read (its own key on top of the aura-tactics and tactics
    // gates). Off, the rows are ignored, so the table behaves exactly as before the kind (default off).
    bool dungeonRunTankSwap = false;
    // The tank_post boss-tactic kind (a named tank waits at the row's spot for a unit that is not in the fight yet and
    // attacks it on arrival): on, the rows are read (its own key on top of the tactics gate). Off, the rows are ignored,
    // so the table behaves exactly as before the kind (default off).
    bool dungeonRunTankPost = false;
    // The raid guard leaves a unit to the tank a boss-tactic row assigns it to (an until_tank row with a tank slot, or
    // a tank_post row): on, the off-tank pick-ups never send another tank at it; off, the raid's off-tanks take it as
    // before (its own key on top of the raid and boss rules; default off).
    bool dungeonRunGuardLeavesAssignedTank = false;
    // The seek_aura boss-tactic kind (the raid gathers in the cloud a dying `target_entry` leaves, to carry its aura
    // into the boss's cycle): on, the rows load and run; off, they are ignored (default off).
    bool dungeonRunSeekAura = false;
    // The seek_pull boss-tactic kind (a seek_aura source is walked out and killed every cycle, up to the row's
    // walk-outs a cycle): on, the rows load and run; off, the old one-walk-out-per-cycle rules hold (default off).
    bool dungeonRunSeekPullCycle = false;
    // The pulled Son's meet (a seek_pull row's walk-out, Hakkar): on, the walk back ends where a waiting off-tank
    // stands, instead of at the tanks' circle edge, so the Son lands on that tank and is taunted on arrival; off, the
    // walk back is the circle edge as before (default off).
    bool dungeonRunSeekSonMeet = false;
    // The take's chase refusal for a Son at the raid's edge (Hakkar): on, a tank takes a Son standing outside the raid
    // circle when its victim stands inside the circle (plus 2 yd), the Son is on the platform (z >= 46.5), within 8 yd of the
    // circle and within 10 yd of that victim; off, the take refuses every Son outside the circle as before (default off).
    bool dungeonRunSeekTakeEdge = false;
    // Healers skip the open pulled-Son meet's puller and returner as heal targets unless they are under 40 % hp (a heal on
    // the puller adds threat on the Son and peels it onto the healer); off, they heal them as before (default off).
    bool dungeonRunSeekMeetNoHeal = false;
    // The walk-out focus (Hakkar's seek pull): on, the live walk-out puller's combat actions are held until its shot
    // lands (reaction actions still run; nothing below 40 % health), it turns to the Son one tick before it casts, and
    // a hunter opens with Arcane Shot when it can (else Auto Shot); off, the puller's other actions may take its ticks
    // and it turns and casts in one tick as before (default off, recommended on).
    bool dungeonRunSeekWalkoutFocus = false;
    // The walk-out keeps its puller in combat (Hakkar's seek pull): on, `combat end` does not fire for the live,
    // unshot walk-out puller while Hakkar fights, and the pick skips a bot whose AI is off the combat engine or that eats
    // or drinks; it needs dungeonRunSeekWalkoutFocus (without it nothing is held); off,
    // the puller may drop to the non-combat engine mid-task as before (default off, recommended on).
    bool dungeonRunSeekWalkoutHoldCombat = false;
    // The walk-out's kept boss (Hakkar's seek pull): on, the fight scan adds the live, unshot walk-out puller's boss
    // while that boss is missing from its attackers (a puller more than SightDistance from the raid), so the seek row's
    // trigger stays open and its own tick keeps running. It needs dungeonRunSeekWalkoutFocus (without it the puller runs
    // its other actions anyway); off, such a puller loses the scan as before (default off, recommended on).
    bool dungeonRunSeekPullerKeepsFight = false;
    // The walk-out's face-and-cast (Hakkar's seek pull): on, the puller at its stand stops, turns to the Son with the
    // instant forced turn and casts in the same tick, UpdateFaceTarget leaves the live unshot puller's facing alone, and
    // the 6 s no-shot clock counts only the time spent in shot position; off, the turn-then-cast-next-tick of the walk-out
    // focus and the wall-time clock stay as before (default off, recommended on).
    bool dungeonRunSeekWalkoutFaceCast = false;
    // The walk-out stand rows (Hakkar's seek pull, diagnostic, safe on for test walks): on, while the puller walks out
    // unshot, one `seek pull: walk-out stand` row a second names the branch its tick took, and every refused pull cast
    // logs `seek pull: walk-out castrefused` (at most 10 a walk-out); no behaviour changes (default off).
    bool dungeonRunSeekWalkoutStandLog = false;
    // The walk-out stand's sight (Hakkar's seek pull): on, the stand search aims the way the bot's own sight test does
    // (its collision height to the Son's z + the Son's collision height), and a refused move within 3 yd of the stand
    // re-plans the stand (up to 3 times a walk-out) instead of counting toward the three-refusal abort; off, floor + 2
    // to z + 2 and every refusal counts (default off, recommended on).
    bool dungeonRunSeekWalkoutStandSight = false;
    // The navmesh-island unstick (PlaceUnstickPolicy.h): on, a bot in a dungeon or raid whose moves have failed with
    // dispatch-short / NOPATH for 10 s, and whose own spot one navmesh query proves cut off from its master, is
    // teleported beside the master (combat kept, a pet stuck with it taken along, no whisper), at most once per 30 s; only
    // while the master is alive, off any transport, more than 20 yd away and within sight distance (or the group is
    // commander-bound), never on a battleground, never while the bot is dead, charmed, possessing another unit or casting, and never while the master fights the bot's own target
    // within 5 yd of it; off, such a bot stays where it is (default off, recommended on).
    bool dungeonRunPlaceUnstick = false;
    // The nearest take (Hakkar's Sons): on, the first off-tank takes a Son on a non-tank at once, without the 3 s
    // healer clock, when the receiver (the third tank) is more than 8 yd from it or its taunt is on cooldown and the
    // first off-tank is within 8 yd with its taunt ready, is not Hakkar's victim and holds no Son; the main tank never
    // takes. Off, the 3 s rule stands as before (default off, recommended on).
    bool dungeonRunSeekNearestTake = false;
    // The nearest take's reach (Hakkar's Sons, walk 57 F4; needs dungeonRunSeekNearestTake): on, the first off-tank
    // also takes a walked Son on a non-tank from up to 20 yd when the receiver is more than 15 yd from it, or that Son's
    // meet closed as peeled or receiver_victim in the last 10 s and the receiver cannot take it itself (within 8 yd,
    // its taunt ready); never while it is Hakkar's victim or holds a Son, or when its leaving Hakkar would collapse the
    // damage dealers' threat-cap reference (no off-tank reference left, or one under half of today's). The same cap
    // guard then holds back its 3 s healer-clock take too, until the Son has sat 6 s on that non-tank or its victim is
    // under 40 % health. A take that passed the guard and started is kept for its Son for 10 s. The take
    // row says src=nearest_reach. Off, the 8 yd nearest take and the healer clock stand as before (default off,
    // recommended on).
    bool dungeonRunSeekNearestTakeReach = false;
    // The meet's pre-place (Hakkar's Sons): on, when a walk-out starts the third tank (alive, not charmed, not Hakkar's
    // victim, holding no Son, inside the tanks' circle) walks to a point 2 yd inside the tanks' circle edge on the line
    // from the anchor's centre toward the puller's stand and holds there for the meet. It ends when that meet closes,
    // the task ends without one, another tank is picked at the shot, or the tank dies, is charmed or becomes Hakkar's
    // victim. Off, the receiver stands wherever its tank AI left it, as before (default off, recommended on).
    bool dungeonRunSeekMeetPrePlace = false;
    // The pre-place's late walk (Hakkar's Sons, walk 57 F3; needs dungeonRunSeekMeetPrePlace): on, the pre-placed third
    // tank stays on Hakkar until the walk-out puller's walk left to its stand is no longer than the tank's own walk to
    // its point plus 4 yd (never later than 10 yd from the stand), or the puller is in position (at its stand, in shot
    // position or shot), then walks to its point. Off, it walks when the walk-out starts, as before (default off,
    // recommended on; its value is still being tested).
    bool dungeonRunSeekMeetPrePlaceLate = false;
    // The walk-out shot waits for the pre-placed receiver (walk 58 F2; runs with DungeonRunSeekMeetPrePlace): in shot
    // position the puller holds its shot until the pre-placed third tank is within 5 yd of its point, at most 6 s, and
    // never once the next Blood Siphon is 15 s or less away. No pre-place this cycle: no wait. Off, the shot goes at
    // once, as before (default off, recommended on).
    bool dungeonRunSeekShotWaitsReceiver = false;
    bool dungeonRunSeekMeetPrePlaceHold = false;
    bool dungeonRunSeekMeetTakeReach = false;
    bool dungeonRunSeekTakenSonThreatCap = false;
    // The siphon clock (Hakkar's seek pull; needs dungeonRunSeekPullCycle and dungeonRunSeekWalkoutFaceCast): a walk-out
    // that sent no pull cast and never stood in shot position aborts as `no sight` after 15 s rather than timing out at
    // 25 s, and a seek_pull row's start and retry floors are lowered to 50 s (never raised; defaults off, recommended
    // on). Off keeps today's clock: the 25 s timeout and the row's own 60 s floors.
    bool dungeonRunSeekSiphonClock = false;
    // The rolled cycle number (Hakkar's seek pull; needs DungeonRunSeekPullCycle): when a due time rolls past a
    // missed sighting, the record's cycle number advances by the rolls too, so the walk-out's spent cycle reopens and a
    // later Son can still be pulled; the estimated sighting time moves with it (default off, recommended on).
    bool dungeonRunSeekCycleRoll = false;
    // The Cause Insanity threat window (Hakkar; needs a threat_successor row carrying the reset spell, cycle_spell):
    // around each expected reset, a damage dealer at or above the row's priority % of the best tank that does not hold
    // him stops its damage on him (specials and white damage), resuming 5 points under, so the threat wipe hands him to
    // a tank. Off by default: the walk shows an uncertain net (DPS cost against fewer non-tank deaths), so it ships as
    // an A/B and turns on only after the siphon clock has a walk.
    bool dungeonRunThreatResetWindow = false;
    // The pull_gate boss-tactic kind (the run leader refuses a boss pull while the boss wears the row's spell above
    // its stack band, then ends the run): off, the rows are ignored (default off).
    bool dungeonRunPullGate = false;
    // The spread boss-tactic kind (the ranged raid keeps the row's spacing while the boss fights): off, the rows are
    // ignored (default off).
    bool dungeonRunBossSpread = false;
    // A seated bot keeps its boss stand seat through Onyxia's Deep Breath instead of walking to the generic safe
    // spots: off, the breath dodge runs as before even for a seated bot (default off).
    bool bossStandOutranksBreathDodge = false;
    // The avoid_cast blast mode `all` (a listed blast also clears the tanks while its spell is cast): on, an `all`
    // row holds no caster for any bot, so every tank walks out of that blast too (its own key on top of the tactics
    // gate; default off).
    bool dungeonRunAvoidCastAll = false;
    // The burn_mana boss-tactic kind (a few eligible bots drain the boss's mana while it is above the row's start %,
    // Moam's Drain Mana refills him so the burners keep him from a full-mana eruption): off, the rows are ignored
    // (default off).
    bool dungeonRunBurnMana = false;
    // The ranged_only boss-tactic kind (melee bots leave a subject that stands the row's gap above them: Ayamiss in the
    // air, so the melee take the adds): off, the rows and the held target are ignored (default off).
    bool dungeonRunRangedOnly = false;
    // The gate_hold boss-tactic kind (damage dealers stop on a boss at or below the row's health gate while its blocker
    // still fights): off, the rows are ignored and nothing reads the gate (default off, recommended on).
    bool dungeonRunGateHold = false;
    // The creature-charm yield (a bot charmed by a creature hands its AI to the charmer and runs no engine): off, a
    // charmed bot behaves as before (default off).
    bool dungeonRunCharmYield = false;
    // The escape of a taunt-immune boss's non-tank victim (Feign Death, Vanish, Ice Block, Divine Shield, else a
    // paladin's Blessing of Protection within 30 yd): off, nothing changes (default off).
    bool dungeonRunImmuneBossVictimEscape = false;
    // A cursed healer or off-tank strikes its own Shade (a kill_first row gated by the curse) while the raid is safe:
    // the lowest member at or above 50 %, the main tank at or above 60 %, two other uncursed healers, 30 % mana and no
    // pending `now` dispel. The main tank never strikes and an off-tank only while the boss is not on it. Off, the
    // carrier node is never active (default off).
    bool dungeonRunCarrierStrike = false;
    // The carrier scan: a Delusions carrier whose attackers list never showed a Shade finds it by the grid -- the
    // nearest living unit of the carrier row's entry that is this fight's, in reach, in sight and attackable -- and the
    // kill-first row says which check kept every grid hit out of the pick. Off, the pick stays as before (default off).
    bool dungeonRunCarrierScan = false;
    // Shade healer cover (walk 56 F3): a cursed off-tank that does not hold the boss takes a Shade of Jin'do attacking a
    // healer, and a cursed DPS carrier ranks a Shade on a healer within 30 yd of it, on its floor, first. Off = as before.
    bool dungeonRunShadeHealerCover = false;
    // Carrier hold: a Delusions carrier standing on its Shade keeps it ("not dps target active" stays quiet for it, so
    // "dps assist" no longer pulls it to Jin'do), and the kill-first trigger checks every second. Off = as before.
    bool dungeonRunCarrierHold = false;
    // Threat successor = the tank that does NOT hold the boss: on a `threat_successor` fight the damage dealers are capped
    // against the real threat of the highest tank that is on the boss and is not the one it is hitting, so an
    // off-tank holding Jin'do no longer caps them against itself while a Hexed main tank sits below. Off = as before.
    bool dungeonRunThreatSuccessorNotHolder = false;
    // Party cures and dispels (CurePartyMemberAction: dispel magic, cure poison/disease, remove curse, cleanse, purify on a
    // party member) skip the damage threat back-off the way heals do, so a healer high on its own target's threat still
    // cures Jin'do's Hex on the tank. Off = as before (the cure is vetoed at the high-threat line).
    bool dungeonRunCureIgnoresThreat = false;
    // A successor line that holds: on a boss whose `threat_successor` row carries `strict` (a plain row keeps today's
    // behaviour) the high-threat trigger's 5 s projection escape is off
    // (a damage dealer at or over 80 % is vetoed even while it projects under the tank), and the melee swing and the
    // auto-repeat (wand Shoot, Auto Shot) are stopped in the trigger pass at 92 % of the reference; melee and wand start
    // again under 85 %, Auto Shot under 80 % (its SINGLE veto). A 0 reference (a tank Hex) never stops them
    // (ThreatCapPolicy.h SuccessorWhiteLatch). Off = as before.
    bool dungeonRunThreatSuccessorStrict = false;
    // The successor pick keeps a tank wearing Delusions of Jin'do (24306, on its Shade by design) as on the boss, like a
    // Hexed one, while its threat is above 0 and at least half the highest tank's (ThreatCapPolicy.h
    // ThreatCapCursedOnBoss). A charmed tank is still skipped. Off = as before.
    bool dungeonRunThreatSuccessorKeepsAway = false;
    // Fact judge for a charmed lead (ZG walk 57 F1): while the lead bot is charmed (Brain Wash, any charm) and an
    // uncharmed member is fit to judge (RunJudge), the run's fact read, the order's target check and the totem focus'
    // selection owner test friendliness and engagement against that member, not the charmed lead. Off = as before.
    bool dungeonRunFactJudgeCharmedLead = false;
    // The taunt gate: a tank does not taunt a needs_aura subject (a Shade of Jin'do, which only a carrier can damage) or
    // a creature the core marks immune to taunts (Jin'do). Off, every taunt goes out as before (default off).
    bool dungeonRunTauntGate = false;
    // The run leader's time limit from `run start`, in minutes (0 = none; default 180 = 3 h).
    uint32 dungeonRunMaxMinutes;
    // The run leader estimates how many creatures a pull brings (PullAggroEstimatePolicy.h) and rests the party
    // once before a pull that brings more than the ceiling (opt-in; ceiling default 6 joiners).
    bool dungeonRunAggroEstimate;
    uint32 dungeonRunAggroCeiling;
    // ... the ring estimate (EstimateAggroRing): the walk to the pull, the stand and the fight point wake a pack, and
    // every member calls for help within dungeonRunAggroRingYd (only read with dungeonRunAggroEstimate).
    bool dungeonRunAggroWalkRing;
    float dungeonRunAggroRingYd;
    // The run leader's pack-cleared gate: a pull step is done only when no unit of its entries is left within its
    // radius + this many yards (2D, 10 yd of height), a unit that spawned inside the radius belongs to the step
    // wherever it walks, and a formation follower of a waypoint leader counts as a patroller. 0 turns it off.
    float dungeonRunPackClearMarginYd;
    // ... and how many residue pulls a step may send before it pauses.
    uint32 dungeonRunPackClearRepulls;
    // The run leader's straggler pull: a unit a pull step or stand-pull row waits for that did not come (a spawn-owned
    // patroller, a patrol of the row) is walked to and pulled by the acting tank (the lead bot, else the off-tank
    // holding the main tank role) instead of the pause, when the way to it keeps clear of another pack
    // (DungeonRunPolicy.h StragglerPullTick; default off).
    bool dungeonRunStragglerPull = false;
    // The run leader's near-wipe stop: this many wipe events (a near wipe, a wipe, the revive after either) within
    // dungeonRunNearWipeStopMinutes of running time pause the run once the fight is over. 0: off.
    uint32 dungeonRunNearWipeStop;
    uint32 dungeonRunNearWipeStopMinutes;
    // ... and whether a party binding gets it too (a raid binding gets it by default).
    bool dungeonRunNearWipeStopParty;
    // The run leader's regroup holds for a living resurrector to raise the fallen (RezHoldPolicy.h; default off).
    bool dungeonRunRezHold;
    // The run leader orders `pullback` only when the tank can shoot from where it stands, else it walks the
    // party to a firing stand first (DungeonRunPolicy.h HoldForShot; default off).
    bool dungeonRunPullStand;
    // A firing stand is refused when another live pack (its patrol loop included) can reach it; none clear: the
    // plain pull goes out (DungeonRunPolicy.h HoldForShot; default off).
    bool dungeonRunStandClearance;
    // The approach row before a stand-pull step holds outside the pulled pack's wake reach and the pull's crowd
    // control goes out there, before the walk in wakes the pack (DungeonRunPolicy.h NextStandPull; default off).
    bool dungeonRunStandPullPreCc;
    // A pull row fires from the plan's authored approach only (no closer firing stand), and every tank and melee is
    // waited for at that stand or clear of the next pull's pack before the row fires (DungeonRunPolicy.h
    // HoldForShot, StandDisciplineOutOfPlace; default off).
    bool dungeonRunStandDiscipline;
    // The run leader marks crowd control on a pack from what the raid has when the pull brings more than
    // dungeonRunAutoCcAbove bodies (DungeonRunPolicy.h RunAutoCcPickOrder; default off), the fear families only with
    // dungeonRunAutoCcRisky.
    bool dungeonRunAutoCc;
    uint32 dungeonRunAutoCcAbove;
    bool dungeonRunAutoCcRisky;
    // 1: the run leader walks the tank back to the guard's stand point after every guarded fight, as before
    // 2026-10-22; 0 (default): not once the step's pack is cleared (DungeonRunPolicy.h GuardWalkBackWanted).
    bool dungeonRunWalkBackAfterWonFight;
    bool dungeonRunRetreatAt2Dead;
    bool dungeonRunWalkBackRescue;
    // The run leader's route picks stay off deep water (DungeonRunPolicy.h DryWalkWanted, the glue's DryWayPath;
    // default off).
    bool dungeonRunDryWalk;
    // A member a run brings back from farther than this is teleported with its combat dropped (0: the old summon).
    float runTeleportReturnYd;
    // A run's bot revived farther than this from the lead bot, and any bot the run brings back, stands still
    // until it arrives (0: off).
    float dungeonReviveHoldYd;
    bool dungeonRunFightForward;
    bool dungeonRunLightRest;
    // A run leader loot step whose item is another member's under the group's loot rules sets free-for-all for the
    // carrier's loot and puts the method back (DungeonRunPolicy.h LootFfaStep; default off).
    bool dungeonRunLootFfa;
    // Player-led runs (`.bot run start ... player`, PlayerLedPolicy.h; default off): a player walks his own party,
    // the run leader pulls the plan's packs near him (within dungeonRunPlayerLedPullYd, default 40 yd).
    bool dungeonRunPlayerLed;
    float dungeonRunPlayerLedPullYd;
    // The run leader's bot-only giving (spec item-leg-skip D1): every needed item goes to every bot at the start
    // and is taken back at the end (default on). The warn/hold/skip check is always on.
    bool dungeonRunGiveItems;
    // With a real player in the party, bots also carry the needed dungeon keys/items and open the doors for them
    // (spec item-leg-skip D1 rule 5; default off: players bring their own key, the bots stop and ask).
    bool dungeonRunBotKeysForPlayers;
    // ... and how long the leading player may be out of the plan's dungeon before the run stops (minutes, default 10).
    uint32 dungeonRunPlayerLedAwayMinutes;
    // Bot LFG dungeon groups (runtime/BotLfgService.cpp; default off); dormant while dungeonSelfClear is off.
    bool botLfgGroups;
    // The wait for a player in minutes.
    uint32 botLfgFillAfterMinutes;
    // Groups per 100 bots online.
    uint32 botLfgGroupsPer100Bots;
    // The most groups at once.
    uint32 botLfgMaxGroups;
    // Groups form only while a real player is online.
    bool botLfgOnlyWithPlayersOnline;
    // A waiting bot LFG group posts in LookingForGroup and answers "inv" whispers.
    bool botLfgChat;
    // Bots stay out of listed creatures' permanent damaging auras, e.g. Maraudon's Creeping Sludge (opt-in).
    bool avoidCreatureAuras;
    // Bots may fly the taxi paths a core repair fixes (TaxiPathPolicy.h).
    bool allowRepairedTaxiPaths;
    // Test shortcut: a bot that boards a taxi arrives at the destination
    // node at once instead of flying the route (BotInstantFlightPolicy.h).
    bool botInstantFlight;
    bool turnInRpg;
    bool globalSoundEffects;
    bool shareTargets;
	std::list<uint32> pvpProhibitedZoneIds;
    bool randomBotForcePvp;
    uint32 worldPvpSeek;
    uint32 worldPvpSeekBotPercent;
    uint32 worldPvpSeekRerollMinutes;
    bool enableGreet;
    // Seconds before the same real player may be greeted again by the same bot.
    uint32 greetCooldown;
    // Masterless pool bots also greet real players (off: greetings are for a
    // bot's own player).
    bool randomBotGreet;
    // Passing buffs from pool bots to real players they meet: a friendly
    // player running past gets a buff cast on them. On by default.
    bool passingBuffs;
    bool randomBotShowHelmet;
    bool randomBotShowCloak;
    bool gearscorecheck;
    int32 levelCheck;
    // Pool-bot trade safety (issue #469, donor EnableRandomBotTrading 0-3
    // plus our open 4): 0 off / 1 trusted (master, group, hire master) /
    // 2 buy-only / 3 sell-only / 4 on. Out-of-range fails closed to 0.
    int32 poolBotTradeMode;
    // Addon chatter prefixes that never drive the trade action
    // (donor TradeActionExcludedPrefixes parity, e.g. Questie/DBM lines).
    std::list<std::string> tradeActionExcludedPrefixes;
    float playerbotsXPrate;
    bool disableActivityPriorities;
    bool forceActiveWhenNearPlayer;
    bool limitCombatActivity;
    bool guildOrderAlwaysActive;
    bool guildOrderBankWithdraw;
    bool craftToolsAlways;
    bool guildOrderBankGold;
    bool guildOrderPlayerBuy;
    bool guildOrderSubOrders;
    uint32_t guildOrderSubOrderDepth;
    // Guild bank access for guild bots: every bot in a guild with a real
    // player member holds one shared rank (`.bot order rank` picks it,
    // defaulting to the guild's lowest rank).
    bool guildOrderBotRank;
    uint32 botActiveAlone;
    uint32 minEnchantingBotLevel;
    // Ordered pull: DPS hold after the tank's pull lands (pull) or after the
    // tank is back at the anchor (pullback). Per-command [seconds] overrides
    // the defaults; the return leg is hard-capped by pullBackMaxReturnTime.
    uint32 pullDpsJoinDelay;
    uint32 pullBackDpsJoinDelay;
    uint32 pullBackMaxReturnTime;
    // Commanded pulls open from range (the tank's ranged weapon) when the mob
    // is in sight and in shot range but cannot be walked to, or is farther than
    // rangedPullMeleeApproach. Off = a plain pull always walks in unless the
    // order says `ranged`.
    bool rangedPull;
    float rangedPullMeleeApproach;
    // Level seeding: fresh pool bots are created at level 1 by
    // CharacterCreation, so the pool starts at 1 unless this range seeds it.
    // 1/1 keeps the historic "walk from level 1" behaviour.
    uint32 randomBotStartLevelMin;
    uint32 randomBotStartLevelMax;
    bool randomBotInvitePlayer;
    bool randomBotGroupNearby;
    bool randomBotRaidNearby;
    bool randomBotGuildNearby;
    bool randomBotFormGuild;
    // Opt-in diagnostic logging: when true, [BOT] log lines and per-bot action
    // log files (logs/bots/<name>_acc<id>_<timestamp>.log) are emitted. Default
    // off so production servers don't pay disk I/O / branch overhead.
    bool enableActionLog;
    // Which bots get a per-bot file while enableActionLog is on (all |
    // commanded | none), and how much the files may hold. Cleanup runs at
    // startup and hourly; see ActionLogPolicy.h. 0 switches a limit off.
    ai::botdiag::ActionLogScope actionLogScope;
    uint32 actionLogMaxAgeDays;
    uint32 actionLogMaxTotalMB;
    uint32 actionLogMaxFileMB;
    // Opt-in aggregate counter of executed bot actions (Engine OK/FAILED/
    // IMPOSSIBLE outcomes keyed by bot class + action name), dumped every 5
    // minutes to logs/action_counts.csv. Default off so production servers
    // pay one branch per executed action and nothing else.
    bool actionCountsLog;
    bool healerDiagnostics = false;
    // Filename (relative to LogsDir) for the bot subsystem log. When set,
    // all sLog calls from bot .cpp files are redirected there instead of
    // writing to the main server log. Default: "bots.log". Empty = disabled.
    std::string botLogFile;
    bool enableOffSpecStrategies;
    bool useWanderAsDefaultFollowStrategy;
    std::string defaultFormation;

    uint32 guildMaxBotLimit;

    uint32 botAcceptDuelMinimumLevel;

    bool talentsInPublicNote;
    bool professionsGrowWithLevel;
    // Profession specializations (runtime/ProfessionSpecializationPolicy.h):
    // 0 off, 1 one branch per bot (guid parity, fixed forever), 2 all
    // branches of each qualified profession. Default 2: the core enforces
    // no exclusivity between branches (no chain req, no unlearn, no
    // trainer/skill gating), so both known together is safe.
    int botProfessionSpecializations = 2;
    bool nonGmFreeSummon;

    BotSelfBotLevel selfBotLevel;
    uint32 iterationsPerTick;
    // Per-tick AI pass scheduling in BotManager::UpdateBots: bots owned by a
    // real player always run first and unbudgeted, the random pool runs after
    // them as one turn queue (BotTurnScheduler.h). The budget below applies
    // to the pool pass only, and only while the previous world tick ran
    // longer than poolBudgetWhenTickOverMs (0 = every tick; pair with a
    // nonzero target below so healthy ticks reclaim the ceiling).
    // Microseconds of module work per tick; 0 disables the budget entirely.
    uint32 poolTickBudgetUs = 10000;
    // Near-first turn priority (BotTurnScheduler.h): due pool bots on the same
    // map and instance as a real player and within this many yards run ahead
    // of the rest when the budget cuts the pass. 0 (the default) keeps every bot
    // equally late.
    float poolNearPlayerYd = 0.0f;
    uint32 poolBudgetWhenTickOverMs = 0;
    uint32 combatTickBudgetUs = 15000;
    // Self-tuning tick budget: when nonzero, a controller moves the effective
    // pool/combat budgets each tick from the measured previous world tick so
    // the tick stays near this target (above target + 10 ms -> shrink, at or
    // below -> reclaim toward the ceilings). 0 disables the controller
    // (static budgets).
    uint32 targetWorldTickMs = 100;
    // Calculated values refresh after their full checkInterval instead of
    // half of it (fight-sensing and interval 0/1 values keep today's rule).
    // Mirrored into ai::gValueRefreshAtInterval (strategy/ValueRefreshPolicy.h)
    // so the per-read check does no config lookup.
    bool valueRefreshAtInterval = false;
    // Issue #84: bounded failure backoff tuning. Zero base/max disables.
    uint32 failedActionRetryBaseMs;
    uint32 failedActionRetryMaxMs;
    uint32 failedActionCacheTtlMs;
    uint32 failedActionCacheMaxEntries;

    std::string autoPickReward;
    bool autoEquipUpgradeLoot;
    // Equip-upgrade score threshold (AG-1): a new item must beat the old
    // stat weight by this factor to count as an upgrade. Stops epsilon
    // swap churn across audits. 1.0 restores any-gain swaps.
    float equipUpgradeThreshold = 1.1f;
    bool syncQuestWithPlayer;
    bool syncQuestForPlayer;
    std::string autoTrainSpells;
    std::string autoPickTalents;
    bool autoLearnTrainerSpells;
    bool autoLearnQuestSpells;
    bool autoLearnQuestSpellsAlts;
    bool autoLearnDroppedSpells;
    bool petAutoTrain;
    bool petAutoTrainAlts;
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

    bool randomBotLoginWithPlayer;
    // Autonomous BG queue for WSG/AB/AV. Default on. Bounded cadence and
    // max-per-interval, in-memory Headless/random selection, faction/level
    // bracket/state/deserter/taxi/combat/queue checks, native handler
    // ownership/invites/queue updates via HandleBattlemasterJoinOpcode (guid
    // 1337 bypass) + SMSG_BATTLEFIELD_STATUS/BGStatusAction invite path.
    bool randomBotBgEnabled = true;
    uint32 randomBotBgQueueInterval = 30000;
    uint32 randomBotBgMaxQueuePerInterval = 1;
    // Autonomous bot-only WSG (default on). Seeds one 10v10 in the most
    // populated bracket, capped at one concurrent instance (average PC).
    bool randomBotBgAutonomous = true;
    uint32 randomBotBgAutonomousMaxInstances = 1;

    bool jumpInBg;
    bool jumpWithPlayer;
    // Ready-check rebuff defer (SOC-S5). Off by default: when on, a bot that
    // gets a ready check out of combat reports status immediately but holds
    // the confirm until buffs settle (grace) or a hard cap fires, instead of
    // answering ready and buffing through the pull.
    bool forceRebuffOnReadyCheck = false;
    bool jumpFollow;
    bool jumpChase;
    bool useKnockback;
    std::list<uint32> botAreaTriggerIds;
    float jumpNoCombatChance;
    float jumpMeleeInCombatChance;
    float jumpRandomChance;
    float jumpInPlaceChance;
    float jumpBackwardChance;
    float jumpHeightLimit;
    // A near teleport whose destination ends in mid-air (Jin'do's Banish 24466) lands the bot on the floor under
    // it instead of leaving it hovering. Off by default: it changes every near teleport.
    bool teleportLandingFall = false;
    float jumpVSpeed;
    float jumpHSpeed;

    std::mutex m_logMtx;
    ai::LogFileRotation logRotation;
    // AiPlayerbot.LogRetentionDays; 0 = never delete old pieces.
    uint32 logRetentionDays = 0;
    // Hour (unix time / 3600) each live log was opened in.
    std::unordered_map<std::string, time_t> logHour;

    // Per-fight combat numbers of every bound group for the observability dashboard.
    bool combatMonitor = false;
    // The boss timeline in each fight record (health, victims, watched auras); needs combatMonitor and Observability.
    bool combatTimeline = false;
    bool raidBossDiagnostics = false;
    std::list<std::string> allowedLogFiles;
    std::list<std::string> debugFilter;

    std::unordered_map <std::string, std::pair<FILE*, bool>> logFiles;
    // Logs that could not be opened or rotated, with the time they may be tried
    // again. Guarded by m_logMtx.
    static constexpr time_t LOG_OPEN_RETRY_SECONDS = 300;
    std::unordered_map<std::string, time_t> logOpenFailed;

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


    float EatDrinkMinDistance;
    float EatDrinkMaxDistance;

    std::string GetValue(std::string name);
    void SetValue(std::string name, std::string value);

    void loadFreeAltBotAccounts();

    std::string GetTimestampStr();

    bool hasLog(std::string fileName) { return std::find(allowedLogFiles.begin(), allowedLogFiles.end(), fileName) != allowedLogFiles.end(); };
    // Takes m_logMtx; log()/logf() use openLogLocked, which needs it already held.
    bool openLog(std::string fileName, char const* mode = "a", bool haslog = false);
    bool openLogLocked(std::string const& fileName, char const* mode, bool haslog);
    bool isLogOpen(std::string fileName) { auto it = logFiles.find(fileName); return it != logFiles.end() && it->second.second;}
    std::string logsDirPath();
    bool archiveLog(std::string const& path, time_t now);
    void rotateLogHourly(std::string const& fileName, FILE* file);
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

    void logEvent(PlayerbotAI* ai, std::string eventName, std::string info1 = "", std::string info2 = "",
        std::string info3 = "", std::string info4 = "", std::string info5 = "");
    void logEvent(PlayerbotAI* ai, std::string eventName, ObjectGuid guid, std::string info2);

    bool CanLogAction(PlayerbotAI* ai, std::string actionName, bool isExecute, std::string lastActionName);

    // The keys set in the bot config file whose name holds `filter` (any
    // case), with their values; secrets (API keys, passwords) read "***".
    std::vector<std::pair<std::string, std::string>> FindConfigEntries(std::string const& filter);

private:
    void LoadTalentSpecs();

    Config config;
};

#define sPlayerbotAIConfig MaNGOS::Singleton<PlayerbotAIConfig>::Instance()
