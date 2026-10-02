#ifndef _RandomItemMgr_H
#define _RandomItemMgr_H

#include "Common.h"
#include "PlayerbotAIBase.h"
#include "playerbot/AiFactory.h"
#ifdef CMANGOS
#include "Objects/Player.h"
#endif
#ifdef MANGOS
#include "Objects/Player.h"
#endif
#include "strategy/values/ItemUsageValue.h"

enum RandomItemType
{
    RANDOM_ITEM_GUILD_TASK,
    RANDOM_ITEM_GUILD_TASK_REWARD_EQUIP_BLUE,
    RANDOM_ITEM_GUILD_TASK_REWARD_EQUIP_GREEN,
    RANDOM_ITEM_GUILD_TASK_REWARD_TRADE,
    RANDOM_ITEM_GUILD_TASK_REWARD_TRADE_RARE
};

#define MAX_STAT_SCALES 32

enum ItemSource
{
    ITEM_SOURCE_NONE,
    ITEM_SOURCE_DROP,
    ITEM_SOURCE_VENDOR,
    ITEM_SOURCE_QUEST,
    ITEM_SOURCE_CRAFT,
    ITEM_SOURCE_PVP
};

enum ItemSpecType
{
    ITEM_SPEC_NONE = 0x0,
    ITEM_SPEC_SPELL_DAMAGE = 0x01,
    ITEM_SPEC_SPELL_HEALING = 0x02,
    ITEM_SPEC_ATTACK = 0x04,
    ITEM_SPEC_TANK = 0x08,
    ITEM_SPEC_CASTER = 0x10,
};

struct WeightScaleInfo
{
    uint32 id;
    std::string name;
    uint8 classId;
};

struct WeightScaleStat
{
    std::string stat;
    uint32 weight;
};

// Source-tier classification for the owner gear rules (roadmap #289: players
// will later unlock higher tiers for hired bots, so the pool classifies by
// tier with a configurable cap instead of a hard-coded exclusion list).
// Lowest-tier source wins: an item available from ANY base source
// (world drop, vendor, quest, trainer/world recipe) counts as base even if
// it also drops in a raid.
enum ItemSourceTier : uint8
{
    ITEM_SOURCE_TIER_BASE = 0,      // world drop / vendor / quest / allowed recipe
    ITEM_SOURCE_TIER_DUNGEON = 1,   // end-game dungeon maps 229/289/329/800
    ITEM_SOURCE_TIER_RAID = 2       // raid maps (DBC map_type=2), world bosses, raid quests/recipes
};

enum ItemSourceFlag : uint8
{
    ITEM_SOURCE_FLAG_NONE = 0,
    ITEM_SOURCE_FLAG_REP = 1 << 0,  // reputation-gated (item row, quest, vendor condition or recipe)
    ITEM_SOURCE_FLAG_PVP = 1 << 1   // PvP gear (NO_DISENCHANT flag or honor rank)
};

struct ItemInfoEntry
{
    ItemInfoEntry() : minLevel(0), itemLevel(0), source(0), team(0), repRank(0), repFaction(0), reqSkill(0), reqSkillRank(0), pvpRank(0), quality(0), slot(0), itemId(0),
        sourceTier(ITEM_SOURCE_TIER_BASE), sourceFlags(ITEM_SOURCE_FLAG_NONE), worldEpic(false)
    {
        for (int i = 1; i <= MAX_STAT_SCALES; ++i)
        {
            weights[i] = 0;
        }
        itemSpec = ITEM_SPEC_NONE;
    }

    std::map<uint32, uint32> weights;
    uint32 minLevel;
    uint32 itemLevel;
    uint32 source;
    std::list<uint32> sourceIds;
    uint32 team;
    uint32 repRank;
    uint32 repFaction;
    uint32 reqSkill;
    uint32 reqSkillRank;
    uint32 pvpRank;
    uint32 quality;
    uint32 slot;
    uint32 itemId;
    ItemSpecType itemSpec;
    // Owner-rule source tier (lowest-tier source wins), orthogonal REP/PVP
    // flags, and the rare-world-epic marker (§6: loot-attested BoE epics).
    ItemSourceTier sourceTier;
    uint8 sourceFlags;
    bool worldEpic;
};

typedef std::vector<WeightScaleStat> WeightScaleStats;
//typedef std::map<WeightScaleInfo, WeightScaleStats> WeightScaleList;

struct WeightScale
{
    WeightScaleInfo info;
    WeightScaleStats stats;
};

//typedef std::map<uint32, WeightScale> WeightScales;

class RandomItemPredicate
{
public:
    virtual ~RandomItemPredicate(){};
    virtual bool Apply(ItemPrototype const* proto) = 0;
};

typedef std::vector<uint32> RandomItemList;
typedef std::map<RandomItemType, RandomItemList> RandomItemCache;

class BotEquipKey
{
public:
    uint32 level;
    uint8 clazz;
    uint8 spec;
    uint8 slot;
    uint32 quality;
    uint64 key;

public:
    BotEquipKey() : level(0), clazz(0), spec(0), slot(0), quality(0), key(GetKey()) {}
    BotEquipKey(uint32 level, uint8 clazz, uint8 spec, uint8 slot, uint32 quality) : level(level), clazz(clazz), spec(spec), slot(slot), quality(quality), key(GetKey()) {}
    BotEquipKey(BotEquipKey const& other)  : level(other.level), clazz(other.clazz), spec(other.spec), slot(other.slot), quality(other.quality), key(GetKey()) {}

private:
    uint64 GetKey();

public:
    bool operator< (const BotEquipKey& other) const
    {
        return other.key < this->key;
    }
};

typedef std::map<BotEquipKey, RandomItemList> BotEquipCache;

class RandomItemMgr
{
    public:
        RandomItemMgr();
        virtual ~RandomItemMgr();
        static RandomItemMgr& instance()
        {
            static RandomItemMgr instance;
            return instance;
        }

	public:
        void Init();
        static bool HandleConsoleCommand(ChatHandler* handler, char const* args);
        RandomItemList Query(uint32 level, RandomItemType type, RandomItemPredicate* predicate);
        RandomItemList Query(uint32 level, uint8 clazz, uint8 specId, uint8 slot, uint32 quality);
        uint32 GetUpgrade(Player* player, std::string spec, uint8 slot, uint32 quality, uint32 itemId);
        std::vector<uint32> GetUpgradeList(Player* player, uint32 specId, uint8 slot, uint32 quality, uint32 itemId, uint32 amount = 1);
        bool HasStatWeight(uint32 itemId);
        bool CanBuyFromVendor(Player* player, uint32 itemId, uint32 creatureId);
        bool HasSameQuestRewards(Player* player, uint32 itemId);
        uint32 GetMinLevelFromCache(uint32 itemId);
        uint32 GetStatWeight(Player* player, uint32 itemId);
        uint32 GetLiveStatWeight(Player* player, uint32 itemId, uint32 specId = 0);
        uint32 GetStatWeight(uint32 itemId, uint32 specId);
        uint32 GetRandomItem(uint32 level, RandomItemType type, RandomItemPredicate* predicate = NULL);
        uint32 GetAmmo(uint32 level, uint32 subClass);
        // Best vendor-sold quiver/ammo pouch for the level (0 = none known).
        // Built with the ammo cache; see BuildAmmoCache. The subclassed
        // overload answers per weapon family (gun -> pouch, bow/crossbow ->
        // quiver); the plain overload keeps the quiver answer for callers
        // that do not know the weapon.
        uint32 GetQuiver(uint32 level, uint32 subClass);
        uint32 GetQuiver(uint32 level);
        uint32 GetRandomFood(uint32 level, uint32 category);
        uint32 GetFood(uint32 level, uint32 category);
        uint32 GetRandomTrade(uint32 level);
        uint32 CalculateRandomEnchantId(uint8 playerclass, uint8 spec, ItemPrototype const* proto);
        uint32 CalculateBestRandomEnchantId(uint8 playerclass, uint8 spec, uint32 itemId);
        uint32 CalculateEnchantWeight(uint8 playerclass, uint8 spec, uint32 enchantId);
        // Level-appropriate permanent enchants (ai_playerbot_enchant_candidates).
        // Best candidate for one equipped item: filters by tier/rep cap, bot
        // level, slot and item-type mask, then picks the highest stat weight
        // for the class/spec. Returns the enchant SPELL id (0 = none fits).
        // Quality ceiling (owner rule): grey -> 0; white -> non-premium rows
        // with min_level <= level-10; green -> non-premium rows with
        // min_level <= level-5; blue -> non-premium rows with min_level <=
        // level; epic+ -> anything allowed incl. premium.
        uint32 CalculateBestBotEnchantId(Player* bot, uint32 specId, Item* item);
        // Proc-enchant score used by the candidate picker above. Type-1
        // (on-hit proc) enchants score 0 in CalculateEnchantWeight; this
        // converts the proc into an equivalent stat weight (see .cpp).
        uint32 CalculateProcEnchantWeight(uint8 playerclass, uint8 spec, uint32 enchantId);
        // min_level of one candidate row (for the deterministic tie-break).
        uint8 GetBotEnchantMinLevel(uint32 spellId, uint8 slotId);
        uint32 CalculateRandomPropertyWeight(uint8 playerclass, uint8 spec, int32 randomPropertyId);
        uint32 CalculateStatWeight(uint8 playerclass, uint8 spec, ItemPrototype const* proto, ItemSpecType &itSpec);
        uint32 ItemStatWeight(Player* player, ItemQualifier& qualifier);
        uint32 ItemStatWeight(Player* player, Item* item);

        uint32 CalculateSingleStatWeight(uint8 playerclass, uint8 spec, std::string stat, int32 value);
        bool CanEquipArmor(uint8 clazz, uint8 spec, uint32 level, ItemPrototype const* proto);
        bool ShouldEquipArmorForSpec(uint8 playerclass, uint8 spec, ItemPrototype const* proto);
        bool CanEquipWeapon(uint8 clazz, ItemPrototype const* proto);
        // canDualWield=false: the bot cannot use an off-hand yet (no Dual Wield
        // spell), so a dual-wield spec may fall back to a two-hander it can
        // actually swing. Only fury's weapon set changes (see .cpp).
        bool ShouldEquipWeaponForSpec(uint8 playerclass, uint8 spec, ItemPrototype const* proto, bool canDualWield = true);
        bool CheckItemSpec(uint8 spec, ItemSpecType itSpec);
        // Fresh-seed provenance gates. Unknown items pass (fail-open): missing
        // world rows (custom items, sparse DBC) must never block gear; only
        // positively-identified raid loot is cut. IsRaidSourcedItem: true when
        // the item drops from a world-boss-rank template or from any
        // creature/gameobject whose spawns sit on a raid map (DBC-classified,
        // custom raid maps included) — corpse, pickpocket, skinning and chest
        // tables, one reference level. Backed by a one-time index
        // (BuildRaidSourceIndex), never a per-item scan: the seed gate calls it
        // once per candidate.
        bool IsRaidSourcedItem(uint32 itemId);
        // IsRaidQuestItem: true when any quest rewarding the item is a raid
        // quest (Type 62) or gated behind a raid map / raid-scale group
        // (SuggestedPlayers > 5). ZoneOrSort sign convention: positive =
        // area id, negative = QuestSort.dbc sort id.
        bool IsRaidQuestItem(uint32 itemId);
        std::vector<uint32> GetQuestIdsForItem(uint32 itemId);
        uint32 GetQuestIdForItem(uint32 itemId);
        // Source-tier classification (owner gear rules, roadmap #289).
        // Lowest-tier source wins; computed once in BuildItemInfoCache from
        // loot→spawn→map, recipe source and quest source, and persisted in
        // ai_playerbot_item_info_cache (source_tier, source_flags, world_epic).
        ItemSourceTier GetSourceTier(uint32 itemId);
        uint8 GetSourceFlags(uint32 itemId);
        bool IsWorldEpic(uint32 itemId);
        // Seed/hire gate: tier cap + REP/PVP flags from the new knobs.
        // Fail-open for uncached items (custom items, sparse DBC).
        bool PassesSourceTier(uint32 itemId);
        std::string GetPlayerSpecName(Player* player);
        uint32 GetPlayerSpecId(Player* player);
        // Issue #189 Phase 2: unknown-spec fallback. Spent talents decide
        // when present; otherwise a class-generic spec so gear never skips
        // entirely (fail-open scoring, still filtered by weapon rules).
        uint32 GetFallbackSpecId(uint8 playerclass);
    private:
        void BuildRandomItemCache();
        void BuildEquipCache();
        void BuildItemInfoCache();
        void BuildAmmoCache();
        void BuildFoodCache();
        void BuildPotionCache();
        void BuildTradeCache();
        void LoadRandomEnchantments();
        bool CanEquipItemNew(ItemPrototype const* proto);
        void AddItemStats(uint32 mod, uint8 &sp, uint8 &ap, uint8 &tank);
        bool CheckItemStats(uint8 clazz, uint8 sp, uint8 ap, uint8 tank);

    private:
        std::map<uint32, RandomItemCache> randomItemCache;
        std::map<RandomItemType, RandomItemPredicate*> predicates;
        BotEquipCache equipCache;
        std::map<EquipmentSlots, std::set<InventoryType> > viableSlots;
        std::map<uint32, std::map<uint32, uint32> > ammoCache;
        // (level-1)/10 bucket -> subclass -> best vendor-sold quiver/pouch entry.
        std::map<uint32, std::map<uint32, uint32> > quiverCache;
        std::map<uint32, std::map<uint32, std::vector<uint32> > > potionCache;
        std::map<uint32, std::map<uint32, std::vector<uint32> > > foodCache;
        std::map<uint32, std::vector<uint32> > tradeCache;
        std::map<uint32, WeightScale> m_weightScales;
        std::map<std::string, uint32 > weightStatLink;
        std::map<uint32, std::string > ItemStatLink;
        std::map<std::string, uint32 > weightRatingLink;
        std::map<uint32, ItemInfoEntry*> itemInfoCache;
        std::map<uint32, std::vector<uint32> > randomEnchantsCache;
        // Permanent-enchant candidate pool (ai_playerbot_enchant_candidates).
        // Loaded once from the world DB; rows never change without a restart.
        struct BotEnchantCandidate
        {
            uint32 spellId;
            uint8 slotId;
            uint8 minLevel;
            uint8 tier;
            uint8 rep;
            uint8 premium;
        };
        void LoadBotEnchantCandidates();
        bool botEnchantsLoaded = false;
        std::vector<BotEnchantCandidate> botEnchantCandidates;
        // Fresh-seed gate state: one-time raid provenance index (see
        // IsRaidSourcedItem) and the memoized quest reverse-lookup
        // (GetQuestIdsForItem). Both filled lazily, valid for the process
        // lifetime — quest and loot data never change after load.
        void BuildRaidSourceIndex();
        bool raidSourceIndexed = false;
        std::set<uint32> raidSourceItems;
        std::map<uint32, std::vector<uint32> > questIdsMemo;
        // Source-tier index (owner gear rules): loot→spawn→map minima.
        // Built once per process inside BuildItemInfoCache; valid for the
        // process lifetime like the raid index above.
        void BuildSourceTierIndex();
        bool sourceTierIndexed = false;
        // Creature template -> lowest map tier of its spawns (id..id4).
        std::map<uint32, ItemSourceTier> creatureSpawnTier;
        // GameObject template -> lowest map tier of its spawns.
        std::map<uint32, ItemSourceTier> gameObjectSpawnTier;
        // Inverted loot index: item id -> lowest tier over every loot table
        // that can yield it (one walk per owning template, entries + groups
        // + one reference level). Per-item lookups are O(1); a shared
        // generic table dropping in both a raid and the open world records
        // base — lowest-tier-source-wins.
        std::map<uint32, ItemSourceTier> itemLootTier;
        // Items in a *direct* corpse-loot row of a base-tier creature
        // template (reference-table-only rows do not attest). World-epic
        // attestation set for IsWorldDropEpic.
        std::set<uint32> itemDirectBaseLoot;
        // Crafted product -> lowest tier of its recipe sources; recipes gated
        // by reputation are also listed in repRecipeItems (REP flag).
        std::map<uint32, ItemSourceTier> recipeTier;
        std::set<uint32> repRecipeItems;
        // Lowest loot-table tier mentioning the recipe item (raid /
        // end-game-dungeon loot recipe raises the product; trainer, vendor
        // and world-loot recipes stay base). Fail-open base when the recipe
        // has no loot row (quest reward, deprecated).
        ItemSourceTier RecipeLootTier(uint32 recipeItemId);
        // Per-item owner-rule classification into the caller's ItemInfoEntry.
        void ClassifySourceTier(ItemPrototype const* proto, ItemInfoEntry* cacheInfo);
        // Raid-quest predicate shared by IsRaidQuestItem and the tier walk.
        bool IsRaidQuest(Quest const* quest);
        // True when the epic has a direct world-map (0/1) creature-loot row.
        bool IsWorldDropEpic(uint32 itemId);
};

#define sRandomItemMgr RandomItemMgr::instance()

#endif
