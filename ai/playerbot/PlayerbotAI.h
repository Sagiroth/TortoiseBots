#pragma once
#include "PlayerbotAIBase.h"
#include "strategy/AiObjectContext.h"
#include "strategy/ReactionEngine.h"
#include "strategy/ExternalEventHelper.h"
#include "ChatFilter.h"
#include "PlayerbotSecurity.h"
#include "PlayerbotTextMgr.h"
#include "BotState.h"
#include "PlayerTalentSpec.h"
#include <stack>
#include "strategy/IterateItemsMask.h"
#include "BotDiagnostics.h"
#include "playerbot/DeathClusterPolicy.h"
#include "playerbot/SurvivePolicy.h"
#include "../../runtime/BotManager.h"

class Player;
class ChatHandler;

using namespace ai;

enum DRUID_TABS
{
    DRUID_TAB_BALANCE,
    DRUID_TAB_FERAL,
    DRUID_TAB_RESTORATION,
};

enum MAGE_TABS
{
    MAGE_TAB_ARCANE,
    MAGE_TAB_FIRE,
    MAGE_TAB_FROST,
};

enum PALADIN_TABS
{
    PALADIN_TAB_HOLY,
    PALADIN_TAB_PROTECTION,
    PALADIN_TAB_RETRIBUTION,
};

enum SHAMAN_TABS
{
    SHAMAN_TAB_ELEMENTAL,
    SHAMAN_TAB_ENHANCEMENT,
    SHAMAN_TAB_RESTORATION,
};

bool IsAlliance(uint8 race);

class PlayerbotChatHandler: protected ChatHandler
{
public:
    explicit PlayerbotChatHandler(Player* pMasterPlayer) : ChatHandler(pMasterPlayer->GetSession()) {}
    void sysmessage(std::string str) { SendSysMessage(str.c_str()); }
    uint32 extractQuestId(std::string str);
    uint32 extractSpellId(std::string str)
    {
        char* source = (char*)str.c_str();
        return ExtractSpellIdFromLink(&source);
    }
};

class ChannelAcces
{
public:
    struct PlayerInfo
    {
        ObjectGuid player;
        uint8 flags;
    };

    typedef std::map<ObjectGuid, PlayerInfo> PlayerList;

    bool IsOn(ObjectGuid who) const { return m_players.find(who) != m_players.end(); }
    std::string                 m_name;
    std::string                 m_password;
    ObjectGuid                  m_ownerGuid;
    PlayerList                  m_players;
    GuidSet                     m_banned;
    const ChatChannelsEntry* m_entry = nullptr;
    bool                        m_announcements = false;
    bool                        m_moderation = false;
    uint8                       m_flags = 0x00;
    // Custom features:
    bool                        m_static = false;
    bool                        m_realmzone = false;
};

namespace ai
{
    class WorldPosition;
    class GuidPosition;
    class IterateItemsVisitor;
    class FindItemVisitor;

	class MinValueCalculator {
	public:
		MinValueCalculator(float def = 0.0f) {
			param = NULL;
			minValue = def;
		}

	public:
		void probe(float value, void* p) {
			if (!param || minValue >= value) {
				minValue = value;
				param = p;
			}
		}

	public:
		void* param;
		float minValue;
	};
};

enum ImportantAreaId
{
    CITY = 3459
};

enum ChatChannelId
{
    GENERAL = 1,
    TRADE = 2,
    LOCAL_DEFENSE = 22,
    WORLD_DEFENSE = 23,
    //Yes, for 1.12 it is 24
    LOOKING_FOR_GROUP = 24,
    GUILD_RECRUITMENT = 25,
};

enum ChatChannelSource
{
    SRC_GUILD,
    SRC_WORLD,
    SRC_GENERAL,
    SRC_TRADE,
    SRC_LOOKING_FOR_GROUP,
    SRC_LOCAL_DEFENSE,
    SRC_WORLD_DEFENSE,
    SRC_GUILD_RECRUITMENT,

    SRC_SAY,
    SRC_WHISPER,
    SRC_EMOTE,
    SRC_TEXT_EMOTE,
    SRC_YELL,

    SRC_PARTY,
    SRC_RAID,

    SRC_UNDEFINED
};

enum HealingItemDisplayId
{
   HEALTHSTONE_DISPLAYID = 8026,
   MAJOR_HEALING_POTION = 24152,
   WHIPPER_ROOT_TUBER = 21974,
   NIGHT_DRAGON_BREATH = 21975,
   LIMITED_INVULNERABILITY_POTION = 24213,
   GREATER_DREAMLESS_SLEEP_POTION = 17403,
   SUPERIOR_HEALING_POTION = 15714,
   CRYSTAL_RESTORE = 2516,
   DREAMLESS_SLEEP_POTION = 17403,
   GREATER_HEALING_POTION = 15713,
   HEALING_POTION = 15712,
   LESSER_HEALING_POTION = 15711,
   DISCOLORED_HEALING_POTION = 15736,
   MINOR_HEALING_POTION = 15710,
   VOLATILE_HEALING_POTION = 24212,
};

enum RoguePoisonDisplayId
{
   DEADLY_POISON_DISPLAYID = 13707,
   CRIPPLING_POISON_DISPLAYID = 13708,
   CRIPPLING_POISON_DISPLAYID_II = 2947,
   MIND_POISON_DISPLAYID = 13709,
   INSTANT_POISON_DISPLAYID = 13710,
   WOUND_POISON_DISPLAYID = 13708
};

enum SharpeningStoneDisplayId
{
   ROUGH_SHARPENING_DISPLAYID = 24673,
   COARSE_SHARPENING_DISPLAYID = 24674,
   HEAVY_SHARPENING_DISPLAYID = 24675,
   SOLID_SHARPENING_DISPLAYID = 24676,
   DENSE_SHARPENING_DISPLAYID = 24677,
   CONSECRATED_SHARPENING_DISPLAYID = 24674,    // will not be used because bot can not know if it will face undead targets
   ELEMENTAL_SHARPENING_DISPLAYID = 21072,
};

enum WeightStoneDisplayId
{
   ROUGH_WEIGHTSTONE_DISPLAYID = 24683,
   COARSE_WEIGHTSTONE_DISPLAYID = 24684,
   HEAVY_WEIGHTSTONE_DISPLAYID = 24685,
   SOLID_WEIGHTSTONE_DISPLAYID = 24686,
   DENSE_WEIGHTSTONE_DISPLAYID = 24687,
};

// m_zero
enum WizardOilDisplayId
{
    MINOR_WIZARD_OIL = 33194,
    LESSER_WIZARD_OIL = 33450,
    BRILLIANT_WIZARD_OIL = 33452,
    WIZARD_OIL = 33451,
    /// Blessed Wizard Oil is not part of the active consumable set.
};
// m_zero
enum ManaOilDisplayId
{
    MINOR_MANA_OIL = 33453,
    LESSER_MANA_OIL = 33454,
    BRILLIANT_MANA_OIL = 33455,
};

enum class BotTypeNumber : uint8
{
    ACTIVITY_TYPE_NUMBER = 1,
    GROUPER_TYPE_NUMBER = 2,
    GUILDER_TYPE_NUMBER = 3,
    CHATFILTER_NUMBER = 4 ,
    DUMMY_ATTACK_NUMBER = 5,
    RPG_PHASE_NUMBER = 6,
    RPG_STYLE_NUMBER = 7,
    WORLD_PVP_LOCATION = 8
};

enum class GrouperType : uint8
{
    SOLO = 0,
    MEMBER = 1,
    LEADER_2 = 2,
    LEADER_3 = 3,
    LEADER_4 = 4,
    LEADER_5 = 5,
    RAIDER_20 = 20,
    RAIDER_MAX = 40
};

enum class GuilderType : uint8
{
    SOLO = 0,
    TINY = 30,
    SMALL = 50,
    MEDIUM = 70,
    LARGE = 120,
    MASSIVE = 250
};

enum class ActivePiorityType : uint8
{
    IS_REAL_PLAYER = 0,
    HAS_REAL_PLAYER_MASTER = 1,
    IN_GROUP_WITH_REAL_PLAYER,
    IS_RUNNING_TEST,
    IN_BATTLEGROUND,
    IN_INSTANCE,
    VISIBLE_FOR_PLAYER,
    IS_ALWAYS_ACTIVE,
    IN_COMBAT,
    IN_BG_QUEUE,
    NEARBY_PLAYER,
    PLAYER_FRIEND,
    PLAYER_GUILD,
    NO_PATH,
    IN_ACTIVE_AREA,
    IN_ACTIVE_MAP,
    IN_INACTIVE_MAP,
    IN_EMPTY_SERVER,
    MAX_TYPE
};

enum ActivityType
{
    GRIND_ACTIVITY = 1,
    RPG_ACTIVITY = 2,
    TRAVEL_ACTIVITY = 3,
    OUT_OF_PARTY_ACTIVITY = 4,
    PACKET_ACTIVITY = 5,
    DETAILED_MOVE_ACTIVITY = 6,
    PARTY_ACTIVITY = 7,
    REACT_ACTIVITY = 8,
    ALL_ACTIVITY = 9,
    MAX_ACTIVITY_TYPE
};

class PacketHandlingHelper
{
public:
    void AddHandler(uint16 opcode, std::string handler, bool shouldDelay = false);
    void Handle(ExternalEventHelper &helper);
    void AddPacket(const WorldPacket& packet);
    // Drop every queued packet with this opcode, keeping the rest in order.
    // Used by hire provisioning (issue #382): the provision jump's
    // SMSG_LEVELUP_INFO must not fire the levelup automation echo later.
    void DropQueuedOpcode(uint16 opcode);

private:
    std::map<uint16, std::string> handlers;
    std::map<uint16, bool> delay;
    // Penqle WorldPacket is move-only; stack of values fails copy-assign.
    // Use unique_ptr to keep the stack copyable-by-value-of-element.
    std::stack<std::unique_ptr<WorldPacket>> queue;
    std::mutex m_botPacketMutex;
};

class ChatCommandHolder
{
public:
    ChatCommandHolder(std::string command, Player* owner = NULL, uint32 type = CHAT_MSG_WHISPER, time_t time = 0) : command(command), owner(owner), type(type), time(time) {}
    ChatCommandHolder(ChatCommandHolder const& other)
    {
        this->command = other.command;
        this->owner = other.owner;
        this->type = other.type;
        this->time = other.time;
    }

public:
    std::string GetCommand() { return command; }
    Player* GetOwner() { return owner; }
    uint32 GetType() { return type; }
    time_t GetTime() { return time; }

private:
    std::string command;
    Player* owner;
    uint32 type;
    time_t time;
};

class PlayerbotAI : public PlayerbotAIBase
{
public:
	PlayerbotAI();
	PlayerbotAI(Player* bot);
	virtual ~PlayerbotAI();

    virtual void UpdateAI(uint32 elapsed, bool minimal = false);

    void HandleCommands();
private:
    void UpdateAIInternal(uint32 elapsed, bool minimal = false) override;
public:
    static std::string BotStateToString(BotState state);
    std::string GetDefaultMovementStrategy();
    void SetMovementStrategy(const std::string& movement);
    bool HasActiveMovementStrategy();
    void EnsureDefaultMovementStrategy(Player* requester = nullptr);
    // Drop a pull's anchor hold ("pull hold" + the stay it owns) and go back to
    // the bot's default movement. A no-op when no hold is placed, and never
    // touches a stay the player asked for (that drops the hold marker first).
    void ReleasePullHold();
	std::string HandleRemoteCommand(std::string command);
    void HandleCommand(uint32 type, const std::string& text, Player& fromPlayer, const uint32 lang = LANG_UNIVERSAL);
    void QueueChatResponse(uint32 msgType, ObjectGuid guid1, ObjectGuid guid2, std::string message, std::string chanName, std::string name, bool noDelay = false);
	void HandleBotOutgoingPacket(const WorldPacket& packet);
    // Issue #382: drop a queued outgoing packet (e.g. the provisioning level-up echo).
    void DropQueuedOutgoingOpcode(uint16 opcode) { botOutgoingPacketHandlers.DropQueuedOpcode(opcode); }
    void HandleMasterIncomingPacket(const WorldPacket& packet);
    void HandleMasterOutgoingPacket(const WorldPacket& packet);
	void HandleTeleportAck();
    void ChangeEngine(BotState type);
    // Run one engine decision. Module-owned human commands can force this
    // single decision to use the full activity path without changing the
    // normal autonomous/random-bot throttle.
    void DoNextAction(bool minimal = false, bool forceActivity = false);
    bool CanDoSpecificAction(const std::string& name, bool isUseful = true, bool isPossible = true);
    virtual bool DoSpecificAction(const std::string& name, ai::Event event = ai::Event(), bool silent = false);
    void ChangeStrategy(const std::string& name, BotState type);
    void PrintStrategies(Player* requester, BotState type);
    void ClearStrategies(BotState type);
    std::list<std::string_view> GetStrategies(BotState type);
    bool ContainsStrategy(StrategyType type);
    bool HasStrategy(const std::string& name, BotState type);
    template<class T>
    T* GetStrategy(const std::string& name, BotState type);
    BotState GetState() { return currentState; };
    void ResetStrategies(bool autoLoad = true);
    void ReInitCurrentEngine();
    void Reset(bool full = false);
    static bool IsTank(Player* player, bool inGroup = true);
    static bool IsHeal(Player* player, bool inGroup = true);
    bool IsRanged(Player* player, bool inGroup = true);
    bool IsMelee(Player* player, bool inGroup = true);
    Creature* GetCreature(ObjectGuid guid) const;
    Creature* GetAnyTypeCreature(ObjectGuid guid) const;
    Unit* GetUnit(ObjectGuid guid);
    static Unit* GetUnit(CreatureDataPair const* creatureDataPair);
    GameObject* GetGameObject(ObjectGuid guid);
    static GameObject* GetGameObject(GameObjectDataPair const* gameObjectDataPair);
    WorldObject* GetWorldObject(ObjectGuid guid);
    std::vector<Player*> GetPlayersInGroup();
    void DropQuest(uint32 questId);
    std::vector<const Quest*> GetAllCurrentQuests();
    std::vector<const Quest*> GetCurrentIncompleteQuests();
    std::set<uint32> GetAllCurrentQuestIds();
    std::set<uint32> GetCurrentIncompleteQuestIds();
    const Quest* GetCurrentIncompleteQuestWithId(uint32 questId);
    bool HasCurrentIncompleteQuestWithId(uint32 questId);
    std::vector<std::pair<const Quest*, uint32>> GetCurrentQuestsRequiringItemId(uint32 itemId);
    const AreaTableEntry* GetCurrentArea();
    const AreaTableEntry* GetCurrentZone();
    std::string GetLocalizedAreaName(const AreaTableEntry* entry);
    bool IsInCapitalCity();
    ChatChannelSource GetChatChannelSource(Player* bot, uint32 type, std::string channelName);
    bool SayToGuild(std::string msg, bool likePlayer = false);
    bool SayToWorld(std::string msg);
    bool SayToGeneral(std::string msg);
    bool SayToTrade(std::string msg);
    bool SayToLFG(std::string msg);
    bool SayToLocalDefense(std::string msg);
    bool SayToWorldDefense(std::string msg);
    bool SayToGuildRecruitment(std::string msg);
    bool SayToParty(std::string msg, bool likePlayer = false);
    bool SayToRaid(std::string msg);
    bool Yell(std::string msg, bool likePlayer = false);
    bool Say(std::string msg, bool likePlayer = false);
    bool Whisper(std::string msg, std::string receiverName, bool likePlayer = false);
    bool TellPlayer(Player* player, std::ostringstream &stream, PlayerbotSecurityLevel securityLevel = PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, bool isPrivate = true, bool ignoreSilent = false) { return TellPlayer(player, stream.str(), securityLevel, isPrivate, ignoreSilent); }
    bool TellPlayer(Player* player, std::string text, PlayerbotSecurityLevel securityLevel = PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, bool isPrivate = true, bool ignoreSilent = false);
    bool TellPlayerNoFacing(Player* player, std::ostringstream& stream, PlayerbotSecurityLevel securityLevel = PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, bool isPrivate = true, bool noRepeat = true, bool ignoreSilent = false) { return TellPlayerNoFacing(player, stream.str(), securityLevel, isPrivate, noRepeat, ignoreSilent); }
    bool TellPlayerNoFacing(Player* player, std::string text, PlayerbotSecurityLevel securityLevel = PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, bool isPrivate = true, bool noRepeat = true, bool ignoreSilent = false);
    bool TellDebug(Player* player, std::string text, std::string strategy = "debug", BotState state = BotState::BOT_STATE_NON_COMBAT){ if (HasStrategy(strategy, state)) return TellPlayerNoFacing(player, text, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true, false); return false;}
    bool TellError(Player* player, std::string text, PlayerbotSecurityLevel securityLevel = PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, bool ignoreSilent = false);
    void SpellInterrupted(uint32 spellid);
    int32 CalculateGlobalCooldown(uint32 spellid);
    void InterruptSpell(bool withMeleeAndAuto = true);
    bool RemoveAura(const std::string& name);
    void RemoveShapeshift();
    void WaitForSpellCast(Spell *spell);
    bool PlaySound(uint32 emote);
    bool PlayEmote(uint32 emote);
    bool PlayAttackEmote(float chanceDivider);
    void Ping(float x, float y);
    void Poi(float x, float y, std::string icon_name = "This way", Player* player = nullptr, uint32 flags = 99, uint32 icon = 6 /* red flag */, uint32 icon_data = 0);
    Item * FindPoison() const;
    Item * FindConsumable(uint32 displayId) const;
    Item * FindBandage() const;
    Item* FindStoneFor(Item* weapon) const;
    Item* FindOilFor(Item* weapon) const;
    void ImbueItem(Item* item, uint16 targetFlag, ObjectGuid targetGUID);
    void ImbueItem(Item* item, uint8 targetInventorySlot);
    void ImbueItem(Item* item, Unit* target);
    void ImbueItem(Item* item);
    void EnchantItemT(uint32 spellid, uint8 slot, Item* item = nullptr);
    uint32 GetBuffedCount(Player* player, std::string spellname);

    bool GetSpellRange(std::string name, float* maxRange, float* minRange = nullptr);
    uint32 GetSpellCastDuration(Spell* spell);

    virtual bool HasAura(std::string spellName, Unit* player, bool maxStack = false, bool checkIsOwner = false, int maxAmount = -1, bool hasMyAura = false, int minDuration = 0, int auraTypeId = TOTAL_AURAS);
    virtual bool HasAnyAuraOf(Unit* player, ...);
    virtual bool HasMyAura(std::string spellName, Unit* player) { return HasAura(spellName, player, false, false, -1, true); }
    uint8 GetHealthPercent(const Unit& target) const;
    uint8 GetHealthPercent() const;
    uint8 GetManaPercent(const Unit& target) const;
    uint8 GetManaPercent() const;

    virtual bool IsInterruptableSpellCasting(Unit* player, std::string spell, uint8 effectMask);
    virtual bool HasAuraToDispel(Unit* player, uint32 dispelType);
    bool canDispel(const SpellEntry* entry, uint32 dispelType);
    static bool IsHealSpell(const SpellEntry* entry);

    static bool IsMiningNode(const GameObject* go);
    static bool IsHerb(const GameObject* go);

    bool HasSpell(std::string name) const;
    bool HasSpell(uint32 spellid) const;
    bool HasAura(uint32 spellId, Unit* player, bool checkOwner = false);
    Aura* GetAura(uint32 spellId, Unit* player, bool checkOwner = false);
    Aura* GetAura(std::string spellName, Unit* player, bool checkOwner = false);
    std::vector<Aura*> GetAuras(Unit* player, bool allAuras = true, bool positive = false);

    bool HasSpellItems(uint32 spellId, const Item* castItem) const;
    void DurabilityLoss(Item* item, double percent);

    SpellCastResult CheckSpellTargetAlignment(SpellEntry const* spellInfo, Unit* target);

    virtual bool CanCastSpell(std::string name, Unit* target, uint8 effectMask = 0, Item* itemTarget = nullptr, bool ignoreRange = false, bool ignoreInCombat = false, bool ignoreMount = false, SpellCastResult* checkResult = nullptr);
    bool CanCastSpell(uint32 spellid, Unit* target, uint8 effectMask = 0, bool checkHasSpell = true, Item* itemTarget = nullptr, bool ignoreRange = false, bool ignoreInCombat = false, bool ignoreMount = false, SpellCastResult* checkResult = nullptr);
    bool CanCastSpell(uint32 spellid, GameObject* goTarget, uint8 effectMask, bool checkHasSpell = true, bool ignoreRange = false, bool ignoreInCombat = false, bool ignoreMount = false, SpellCastResult* checkResult = nullptr);
    bool CanCastSpell(uint32 spellid, float x, float y, float z, uint8 effectMask, bool checkHasSpell = true, Item* itemTarget = nullptr, bool ignoreRange = false, bool ignoreInCombat = false, bool ignoreMount = false, SpellCastResult* checkResult = nullptr);

    virtual bool CastSpell(std::string name, Unit* target, Item* itemTarget = nullptr, bool waitForSpell = true, uint32* outSpellDuration = nullptr);
    bool CastSpell(uint32 spellId, Unit* target, Item* itemTarget = nullptr, bool waitForSpell = true, uint32* outSpellDuration = nullptr);
    bool CastSpell(uint32 spellId, GameObject* goTarget, Item* itemTarget = nullptr, bool waitForSpell = true, uint32* outSpellDuration = nullptr);
    bool CastSpell(uint32 spellId, float x, float y, float z, Item* itemTarget = nullptr, bool waitForSpell = true, uint32* outSpellDuration = nullptr);
    bool CastPetSpell(uint32 spellId, Unit* target);
    uint32 GetEquipGearScore(Player* player, bool withBags, bool withBank);
    uint32 GetEquipStatsValue(Player* player);
    bool HasSkill(SkillType skill);
    bool IsAllowedCommand(std::string text);
    // Issue #294: single choke point for tiered whisper/`.bot command`
    // authorization. Tier keyed by the command's first word (plus two-word
    // prefixes for `ah bid`, `ah cancel`, `set value`); unknown commands
    // default to owner-or-GM so new debug/economy surface fails closed.
    enum class BotCommandAuthTier : uint8 { Tactical = 0, OwnerOrGm = 1, GmOnly = 2 };
    BotCommandAuthTier AuthTierForCommand(std::string const& command) const;
    bool IsBotOwnerOrGm(Player const& fromPlayer) const;
    bool CheckBotCommandAuth(Player& fromPlayer, std::string const& command, bool silentDeny);
    float GetRange(std::string type);

    static ReputationRank GetFactionReaction(FactionTemplateEntry const* thisTemplate, FactionTemplateEntry const* otherTemplate);
    // cmangos uses sFactionTemplateStore.LookupEntry(N); Penqle uses sObjectMgr.GetFactionTemplateEntry(N).
    static bool friendToAlliance(FactionTemplateEntry const* templateEntry) { return GetFactionReaction(templateEntry, sObjectMgr.GetFactionTemplateEntry(1)) >= REP_NEUTRAL; }
    static bool friendToHorde(FactionTemplateEntry const* templateEntry) { return GetFactionReaction(templateEntry, sObjectMgr.GetFactionTemplateEntry(2)) >= REP_NEUTRAL; }
    bool IsFriendlyTo(FactionTemplateEntry const* templateEntry) { return GetFactionReaction(bot->GetFactionTemplateEntry(), templateEntry) >= REP_NEUTRAL; }
    bool IsFriendlyTo(uint32 faction) { return GetFactionReaction(bot->GetFactionTemplateEntry(), sObjectMgr.GetFactionTemplateEntry(faction)) >= REP_NEUTRAL; }
    static bool AddAura(Unit* unit, uint32 spellId);
    ReputationRank getReaction(FactionTemplateEntry const* factionTemplate) { return GetFactionReaction(bot->GetFactionTemplateEntry(), factionTemplate);}

    void InventoryIterateItems(IterateItemsVisitor* visitor, IterateItemsMask mask);
    void InventoryTellItems(Player* player, std::map<uint32, int> items, std::map<uint32, bool> soulbound);
    void InventoryTellItem(Player* player, ItemPrototype const* proto, int count, bool soulbound);
    std::list<Item*> InventoryParseItems(std::string text, IterateItemsMask mask);
    uint32 InventoryGetItemCount(FindItemVisitor* visitor, IterateItemsMask mask);
    std::string InventoryParseOutfitName(std::string outfit);
    ItemIds InventoryParseOutfitItems(std::string outfit);
    ItemIds InventoryFindOutfitItems(std::string name);

    void AccelerateRespawn(Creature* creature, float accelMod = 0);
    void AccelerateRespawn(ObjectGuid guid, float accelMod = 0) { Creature* creature = GetCreature(guid); if (creature) AccelerateRespawn(creature,accelMod); }

    std::list<Unit*> GetAllHostileUnitsAroundWO(WorldObject* wo, float distanceAround);
    std::list<Unit*> GetAllHostileNPCNonPetUnitsAroundWO(WorldObject* wo, float distanceAround);

    static void SendDelayedPacket(WorldSession* session, std::future<std::vector<std::pair<WorldPacket, uint32>>> futurePacket);
    // Drain packets produced by optional asynchronous work on the world
    // thread. The queue is keyed by durable bot GUID, never by a raw session
    // captured by a worker thread.
    static void ProcessDelayedPackets();
 public:
    std::vector<Bag*> GetEquippedAnyBags();
    std::vector<Bag*> GetEquippedQuivers();
    std::vector<Item*> GetInventoryAndEquippedItems();
    std::vector<Item*> GetInventoryItems();
    uint32 GetInventoryItemsCountWithId(uint32 itemId);
    bool HasItemInInventory(uint32 itemId);
    bool HasNotFullStacksInBagsForLootItems(LootItemList const& questLootItemList);
    bool HasQuestItemsInLootList(LootItemList const& questLootItemList);
    bool HasQuestItemsInWOLootList(WorldObject* wo);
    bool CanLootSomethingFromWO(WorldObject* wo);
private:
    void InventoryIterateItemsInBags(IterateItemsVisitor* visitor);
    void InventoryIterateItemsInEquip(IterateItemsVisitor* visitor);
    void InventoryIterateItemsInBank(IterateItemsVisitor* visitor);
    void InventoryIterateItemsInBuyBack(IterateItemsVisitor* visitor);

private:
    void _fillGearScoreData(Player *player, Item* item, std::vector<uint32>* gearScore, uint32& twoHandScore);
    bool IsTellAllowed(Player* player, PlayerbotSecurityLevel securityLevel = PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL);

public:
	Player* GetBot() { return bot; }
    Player* GetMaster() { return GetLiveMaster(); }

    // accessor for the active engine so
    // cpp can build heartbeat / debug payloads without being
    // a friend class. Read-only.
    Engine* GetCurrentEngine() { return currentEngine; }

    // Registers an engine in the per-state array. The factory that builds an
    // engine calls this before it attaches that engine's default strategies:
    // a strategy's OnStrategyAdded reaches its engine through
    // PlayerbotAI::ChangeStrategy, which walks this array, and a change aimed at
    // an engine that is not in it yet is silently dropped. That is how "rpg"
    // ended up without its children ("rpg quest" and the rest, added only by
    // RpgStrategy::OnStrategyAdded), which in turn disabled quest-giver/taker
    // destinations and the quest-travel request rows.
    void SetEngine(BotState state, Engine* engine) { engines[(uint8)state] = engine; }

    // Heartbeat-cadence accumulator used by ::TickHeartbeat.
    // Public so the helper can advance it in-place every UpdateAI tick;
    // there's no behavior risk since it's a pure accumulator (no class
    // invariant tied to its value beyond "nonnegative").
    uint32 scboteHeartbeatAcc = 0;

    // One-shot "needs level/gear sync" flag. Set by OnBotSummoned
    // when the bot logs in; cleared by TickHeartbeat after the sync runs.
    // Reason for the deferral: at OnBotLogin time the bot's master link is
    // not yet established (cmangos sets it later in the .bot create flow),
    // so the sync code can't tell what level to match. We watch from the
    // tick path and run once master becomes available.
    bool scboteLevelSyncPending = false;

    // One-shot "teleport bot to master on first available tick" flag. Set
    // by OnBotSummoned; cleared by TickHeartbeat after the teleport runs.
    // Same deferral rationale as scboteLevelSyncPending: master link is
    // typically not set at OnBotLogin time. Without this, bots summoned
    // via `.bot add` come online at their last logout position — often a
    // different continent than the master, so they can't be invited to
    // group or interacted with normally. .
    bool scboteTeleportPending = false;

    // Last raid-encounter creature ID we applied strategies for. Used by
    // TickEncounter to detect target-change transitions
    // and add/remove the per-boss strategy bundles. 0 means "no encounter
    // strategies applied". A non-zero value that doesn't match the current
    // target's creature_id triggers a strategy reset for the new target.
    uint32 scboteLastEncounterCreatureId = 0;

    // BotActionLog: track last combat state so we only log STATE snapshots
    // on actual combat-state transitions (combat-enter / combat-exit).
    // Without this we'd spam the log with "still in combat" lines every
    // tick. -1 means "uninitialized" so the first observation always logs.
    int scboteLastCombatLogged = -1;

    // A network transport, rather than an address or socket sentinel, identifies
    // a real client. Headless sessions intentionally have no network transport.
    bool IsRealPlayer()
    {
        return bot && bot->GetSession() && bot->GetSession()->HasNetworkTransport();
    }
    bool IsRealPlayer(Unit* unit)
    {
        if (!unit->IsPlayer()) return false;
        Player* pl = (Player*)unit;
        return pl->GetSession() && pl->GetSession()->HasNetworkTransport();
    }
    bool IsSelfMaster() { return master ? (master == bot) : false; }
    // Resolve the cached master against the live object accessor. The core
    // updates maps on worker threads, so the cached pointer can be freed
    // between a null check and the deref. Revalidating here clears dangling
    // pointers before any GetSession()/deref.
    Player* GetLiveMaster();
    //Bot has a master that is a player.
    bool HasRealPlayerMaster() { Player* m = GetLiveMaster(); return m && m->GetSession() && m->GetSession()->HasNetworkTransport(); }
    //Bot has a master that is actively playing.
    bool HasActivePlayerMaster() { Player* m = GetLiveMaster(); return m && m->GetSession() && m->GetSession()->HasNetworkTransport(); }
    //Checks if the bot is summoned as alt of a player
    bool IsAlt() { return HasRealPlayerMaster() && !TortoiseBots::BotManager::Instance().IsRandomBot(bot->GetObjectGuid()); }
    // Module-owned characters keep their saved build and class defaults. This
    // identity is available before the adapter binds the transient master
    // pointer, which is important while PlayerbotAI constructs its engines.
    bool IsOwnedBot() const;
    // Issue #84 (P2): module-owned teleport signal. Bumped on every
    // HandleTeleportAck; engines consume it to drain stale queues even when
    // the ack tick skips AI updates (short same-map teleports included).
    uint64_t GetTransitionGeneration() const { return transitionGeneration; }
    //Get the group leader or the master of the bot.
    Player* GetGroupMaster();

    bool IsGroupLeader() { return bot->GetGroup() && bot->GetGroup()->GetLeaderGuid() == bot->GetObjectGuid(); }

    //Check if player is safe to use.
    static bool IsSafe(Player* player, WorldObject* obj) {return obj && obj->GetMapId() == player->GetMapId() && obj->GetInstanceId() == player->GetInstanceId() && (!obj->IsPlayer() || !((Player*)obj)->IsBeingTeleported() || !((Player*)obj)->GetSession()->GetPlayer()); }
    bool IsSafe(WorldObject* obj) { return IsSafe(bot, obj); }
    bool IsSafe(Player* player) { return IsSafe(bot, player); }

    //Returns a semi-random (cycling) number that is fixed for each bot.
    uint32 GetFixedBotNumber(BotTypeNumber typeNumber, uint32 maxNum = 100, float cyclePerMin = 1, bool ignoreGuid = false);

    GrouperType GetGrouperType();
    GuilderType GetGuilderType();
    uint32 GetMaxPreferedGuildSize();

    bool HasPlayerNearby(WorldPosition pos, float range);
    bool HasPlayerNearby(float range = sPlayerbotAIConfig.reactDistance);
    bool ChannelHasRealPlayer(std::string channelName);


    ActivePiorityType GetPriorityType();
    std::pair<uint32,uint32> GetPriorityBracket(ActivePiorityType type);
    bool AllowActive(ActivityType activityType);
    bool AllowActivity(ActivityType activityType = ALL_ACTIVITY, bool checkNow = false);

    bool HasCheat(BotCheatMask mask) const;
    BotCheatMask GetCheat() { return cheatMask; }
    void SetCheat(BotCheatMask mask) { cheatMask = mask; }

    // Cache master's GUID alongside the raw pointer so we can revalidate
    // the pointer each tick against ObjectAccessor — if the master Player
    // was destroyed (logout / disconnect) since SetMaster() was called,
    // FindPlayer(masterGuid) returns nullptr (or a different live Player)
    // and we null `master` BEFORE any deref. See RevalidateMasterPointer.
    // 2026-05-05: bot crashed with
    // EXECUTE at 0x0 in UpdateAI's logout-cancel block right after the
    // master (the master) disconnected — `if (master && IsInCombat(master))`
    // dereferenced a freed Player. The `master &&` guard catches null
    // but not dangling.
    void SetMaster(Player* m)
    {
        this->master = m;
        this->masterGuid = m ? m->GetObjectGuid() : ObjectGuid();
    }
    // Clear only the raw pointer after a human logout. Keep masterGuid so the
    // module can rebind the same owned bot when that character reconnects.
    void ClearMasterPointer() { this->master = nullptr; }
    AiObjectContext* GetAiObjectContext() { return aiObjectContext; }
    void SetAiObjectContext(AiObjectContext* aiObjectContext) { this->aiObjectContext = aiObjectContext; }
    ChatHelper* GetChatHelper() { return &chatHelper; }
    bool IsOpposing(Player* player);
    static bool IsOpposing(uint8 race1, uint8 race2);
    PlayerbotSecurity* GetSecurity() { return &security; }

    WorldPosition GetJumpDestination() { return jumpDestination; }
    void SetJumpDestination(const WorldPosition& pos) { jumpDestination = pos; }
    void ResetJumpDestination() { jumpDestination = WorldPosition(); }

    bool IsJumping() { return jumpTime; }
    void SetFallAfterJump() { fallAfterJump = true; }
    void SetJumpTime(uint32 time) { jumpTime = time; }
    bool CanMove();
    void StopMoving();
    bool IsInRealGuild();
    void SetPlayerFriend(bool isFriend) {isPlayerFriend = isFriend;}
    bool IsPlayerFriend() { return isPlayerFriend; }
    bool HasPlayerRelation();

    // Phase 2 guarded spatial-scan cadence (Issue #175): the Cell grid scan
    // behind "possible targets" is the hottest world-thread cost. Outside
    // combat each bot rescans at most once per second, with a per-bot phase
    // so 1000 bots never scan on the same tick. Whole-bot decisions are never
    // frozen: combat, death, damage, revive/teleport grace, nearby humans and
    // a 1.5s staleness cap all force full rate.
    static constexpr uint32 kReviveTeleportGraceMs = 15000;
    // Revive grace is longer than the teleport grace: a fresh revive at 50% HP
    // standing on its death spot re-dies fast live (revive->death median 129 s,
    // 57 of 257 within 60 s), so the first minute after any revive runs the
    // full-rate scan and lets the bot feel the same pack coming back.
    static constexpr uint32 kReviveGraceMs = 60000;
    // Inside the minute after a revive (corpse, spirit healer or sweep).
    bool InReviveGrace() const { return m_reviveGraceUntilMs && int32(m_reviveGraceUntilMs - WorldTimer::getMSTime()) > 0; }
    bool IsSpatialScanIdle() const;
    bool ShouldReuseSpatialScan();
    void NoteSpatialScan() { m_lastSpatialScanMs = WorldTimer::getMSTime(); }

    bool IsStateActive(BotState state) const;
    time_t GetCombatStartTime() const;

    void OnCombatStarted();
    void OnCombatEnded();
    void OnDeath();
    void OnResurrected();

    struct LastKillerInfo
    {
        std::string name;
        uint32 level = 0;
        bool isEnvironment = false;
        uint32 time = 0;
        uint32 entry = 0;       // creature entry, 0 for players and the environment
        float healthPct = 100.0f; // killer's health at the killing blow (deaths.csv fight-back proxy)
    };
    // Live attacker snapshot for the deaths.csv 'adds' column: sampled from
    // bot->GetAttackers() while fighting (NoteFightAttackers, throttled),
    // read at death time. The old victim-filtered "all targets" loop was
    // structurally always 0 - SetDeathState drains the attacker set and
    // clears victim pointers before OnDeath runs - so the snapshot is what
    // makes multi-attacker deaths measurable. Capped at 8 entries, fresh
    // for 30 s (see SurvivePolicy.h).
    void NoteFightAttackers();
    // Per-tick top-up of the level-tier food/drink stack (owner addendum):
    // the cheat-block sibling of the ammo refill, pool-only.
    void RefillPoolRations();
    // Owner rule (auto-tools kit): cheat-gated item/skill ensure. Mint one
    // stack of the behaviour item the bot needs right now (rogue tools,
    // Hourglass Sand on Bronze, Quintessence in MC); lockpicking rides at
    // max-for-level. All branches no-op without the item cheat.
    bool EnsureCheatItem(uint32 itemId, uint32 count);
    void EnsureAutoToolsKit();
    std::vector<ai::DeathAttackerEntry> deathAttackers_;
    uint32 deathAttackersMs_ = 0; // WorldTimer ms of the last sample, 0 = none
    uint32 prevKillerEntry_ = 0; // the kind that killed the bot before (lethal-kind rule in OnDeath)
    uint32 prevKillerMs_ = 0;
    // Death-cluster escape (see OnDeath): deaths inside one small area within a
    // window, whatever killed the bot, and where that area is.
    uint32 deathClusterCount_ = 0;
    uint32 deathClusterMs_ = 0;
    uint32 deathClusterMapId_ = 0;
    float deathClusterX_ = 0.0f;
    float deathClusterY_ = 0.0f;
    // Death-spot avoidance (issue #398): the camps the bot keeps dying in, and
    // until when (WorldTimer ms) each stays off limits for grind and quest
    // picks. Set on the second cluster escape inside the avoidance window; an
    // expiry of 0 means the slot is free. Owned bots and bots with a real
    // player master never set it (OnDeath guards them out). A ding clears the
    // list (AutoLearnSpellAction): the band that killed the bot no longer
    // applies at the new level.
    uint32 deathEscapeCount_ = 0;
    uint32 deathLastEscapeMs_ = 0;
    ai::DeathAvoidSpot deathAvoidSpots_[ai::kDeathAvoidSpots] = {};
    void ClearDeathAvoidance() { for (auto& spot : deathAvoidSpots_) spot = ai::DeathAvoidSpot(); }
    // A death-spot query needs only plain data (map + coordinates), so the
    // travel layer can ask without pulling in the policy header.
    bool IsDeathSpotAvoided(uint32 mapId, float x, float y, uint32 nowMs) const;
    // A player (usually another random bot) that killed this bot recently: name +
    // expiry. While set, the victim does not proactively engage that killer
    // (EnemyPlayersValue::IsValid refuses it) so a same-spot trade-kill loop
    // breaks after the first death instead of every ~60 s. Retaliation still
    // works: an actual attacker stays valid via the threat/victim path.
    std::string avoidPlayerKiller_;
    uint32 avoidPlayerKillerMs_ = 0;
    void SetLastKiller(Unit* killer);
    const LastKillerInfo& GetLastKiller() const { return lastKiller_; }
    void ClearLastKiller() { lastKiller_ = LastKillerInfo(); lastDamager_ = LastKillerInfo(); }
    // Last non-self damager, fed by the UNITHOOK_ON_DAMAGE hook: the death
    // hook alone cannot name the killer when the core self-kills (Spirit of
    // Redemption expiry, spell 27965). Read only when the death hook reports
    // no killer or the bot itself; cleared on resurrect with the killer.
    void NoteDamager(Unit* damager);
    // 10-minute avoidance window after a player kill (matches the lethal-kind
    // 15-min/30-min shape, shorter because the killer is one named player).
    void SetAvoidPlayerKiller(std::string const& name) { avoidPlayerKiller_ = name; avoidPlayerKillerMs_ = WorldTimer::getMSTime(); }
    bool ShouldAvoidPlayerKiller(std::string const& name) const;

    void SetActionDuration(const Action* action);
    void SetActionDuration(uint32 duration);

    // Human orders act now: drop the bot's own action clock instead of letting
    // a leftover delay (teleport grace, failed cast, blocked tick) stall the
    // command's next step. The pull commands need it - they force one tick
    // inline, but the pull itself continues on the bot's own ticks.
    void ClearActionClock() { ResetAIInternalUpdateDelay(); }

    const Action* GetLastExecutedAction(BotState state) const;

    bool IsImmuneToSpell(uint32 spellId) const;

    bool IsInPve();
    bool IsInPvp();
    bool IsInRaid();

    void SetMoveToTransport(bool flag = true) { isMovingToTransport = flag; }
    bool GetMoveToTransport() { return isMovingToTransport; }

    void SetShouldLogOut(bool val = true) { shouldLogOut = val; }
    bool GetShouldLogOut() { return shouldLogOut; }

    PlayerTalentSpec GetTalentSpec();

        // Role handed out by the dungeon finder (LFT_ROLE_* bits: 1 tank,
        // 2 heal, 4 damage; 0 = none). Overrides the talent-derived combat
        // strategy in AiFactory, and survives ResetStrategies() - which is the
        // whole point, since that runs whenever the master changes, i.e. right
        // when the bot joins the player's group.
        void SetForcedRole(uint8 role) { m_forcedRole = role; }
        uint8 GetForcedRole() const { return m_forcedRole; }
    void UpdateTalentSpec(PlayerTalentSpec spec = PlayerTalentSpec::TALENT_SPEC_INVALID);

    bool CanEnterArea(const AreaTrigger* area);
    void Unmount();

    void QueuePacket(WorldPacket& pkt);

    float GetLevelFloat() const;

    void SetLastEvent(ai::Event& event) { lastEvent = event; }
    ai::Event& GetLastEvent() { return lastEvent; }

#ifdef BUILD_ELUNA
    MaNGOS::unique_weak_ptr<PlayerbotAI> GetWeakPtr() const { return m_weakRef; }
    void SetWeakPtr(MaNGOS::unique_weak_ptr<PlayerbotAI> weakRef) { m_weakRef = std::move(weakRef); }
#endif

private:
    bool UpdateAIReaction(uint32 elapsed, bool minimal, bool isStunned);
    void UpdateFaceTarget(uint32 elapsed, bool minimal);

protected:
	Player* bot;
	Player* master;
	uint8 m_forcedRole = 0;
	time_t m_dungeonCatchUpAt = 0;
	// GUID-shadow of `master` so we can verify the pointer is still
	// alive each tick without dereferencing it. Set in SetMaster().
	// Used by RevalidateMasterPointer() at the top of UpdateAI.
	ObjectGuid masterGuid;
	uint32 accountId;
    AiObjectContext* aiObjectContext;
    Engine* currentEngine;
    ReactionEngine* reactionEngine;
    Engine* engines[(uint8)BotState::BOT_STATE_ALL];
    BotState currentState;
    ChatHelper chatHelper;
    std::queue<ChatCommandHolder> chatCommands;
    std::queue<ChatQueuedReply> chatReplies;
    std::mutex chatRepliesMutex;
    PacketHandlingHelper botOutgoingPacketHandlers;
    PacketHandlingHelper masterIncomingPacketHandlers;
    PacketHandlingHelper masterOutgoingPacketHandlers;
    CompositeChatFilter chatFilter;
    PlayerbotSecurity security;
    std::map<std::string, time_t> whispers;
    std::pair<ChatMsg, time_t> currentChat;
    static std::set<std::string> unsecuredCommands;
    bool allowActive[MAX_ACTIVITY_TYPE];
    time_t allowActiveCheckTimer[MAX_ACTIVITY_TYPE];
    bool explicitActivityOverride = false;
    bool inCombat = false;
    bool isMoving = false;
    bool isWaiting = false;
    BotCheatMask cheatMask = BotCheatMask::none;
    WorldPosition jumpDestination;
    uint32 jumpTime;
    bool fallAfterJump;
    // Issue #84 (P2): bumped by HandleTeleportAck, consumed by engines.
    uint64_t transitionGeneration = 0;
    uint32 faceTargetUpdateDelay = 0;
    // Phase 2 spatial-scan cadence (ms clock + grace windows).
    uint32 m_lastSpatialScanMs = 0;
    uint32 m_reviveGraceUntilMs = 0;
    uint32 m_teleportGraceUntilMs = 0;
    // Last ms clock InitAmmo ran from the per-tick ammo refill (#401): a
    // failed resync (bags full) retries at most once a minute, never busy-loops.
    uint32 m_lastAmmoResyncMs = 0;
    // Last ms clock the pool ration top-up ran; it walks the bags, so once per
    // 30 s per bot is plenty for a stack that is never eaten down.
    uint32 m_lastRationRefillMs = 0;
    // Last ms clock the auto-tools kit ensure ran; same bag-walking reason.
    uint32 m_lastAutoToolsKitMs = 0;
    // First tick (ms clock) the core reported alive while the engine was still DEAD;
    // 0 = not currently mismatched. Backs the 5 s alive-but-DEAD self-heal window.
    uint32 m_aliveWhileDeadSinceMs = 0;
    uint32 m_lastMana = 0;
    bool isPlayerFriend = false;
    bool isMovingToTransport = false;
    bool shouldLogOut = false;
    bool m_recordMessages = false;
    bool m_recordIncommingMessages = false;
    std::vector<std::string> m_recordedMessages;
    ai::Event lastEvent;
    LastKillerInfo lastKiller_;
    LastKillerInfo lastDamager_; // last non-self damager (UNITHOOK_ON_DAMAGE); SoR fallback for SetLastKiller
    bool deathHandled_ = false; // this death was logged and counted; cleared when the bot is seen alive again

    // Evade probe (diagnostics only): samples the health and evade state of the creature
    // this bot is fighting, and logs the cases where the mob evades or regenerates faster
    // than the bot damages it. Writes bot_events.csv rows "EvadeProbe" / "EvadeProbeCounter";
    // sets no action, value or movement. Defined in BotDiagnostics.cpp, see there.
    botdiag::EvadeProbeState evadeProbe;
    void UpdateEvadeProbe();

public:
    void RecordMessages(bool record, bool incomming = false) { m_recordMessages = record; m_recordIncommingMessages = incomming; if (!record) m_recordedMessages.clear(); }
    bool IsRecordingMessages() const { return m_recordMessages; }
    bool IsRecordingIncommingMessages() const { return m_recordIncommingMessages; }
    std::vector<std::string> GetRecordedMessages() { m_recordMessages = false; m_recordIncommingMessages= false; auto msgs = m_recordedMessages; m_recordedMessages.clear(); return msgs; }
    void ClearRecordedMessages() { m_recordedMessages.clear(); m_recordMessages = false; m_recordIncommingMessages = false;}

#ifdef BUILD_ELUNA
    MaNGOS::unique_weak_ptr<PlayerbotAI> m_weakRef;
#endif
};

template<typename T>
T* PlayerbotAI::GetStrategy(const std::string& name, BotState type)
{
    return  dynamic_cast<T*>(engines[(uint8)type]->GetStrategy(name));
}
