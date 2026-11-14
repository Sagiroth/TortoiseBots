"""Behaviour tests for an explicit loot order on a chest that holds only common loot.

An order has to survive the two places that decide what a bot may pick up:
the "add all loot" run builds its LootObject with the order, and the loot
queue's selection rebuilds each queued entry with the order it was queued
with. Otherwise Refresh clears the chest's guid and GetLoot never sees it.

Like tools/test_loot_content.py, these build the production method bodies
against stub core types and run them: the real LootObject constructor,
Refresh, GetWorldObject and copy constructor, the real LootTarget queue, the
real LootObjectStack Add/GetLoot/OrderByDistance, and the real
AddAllLootAction Execute/AddLoot. Only core-side pieces (creature loot, the
AI value map, config) are stubbed.
"""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STACK = (ROOT / "ai/playerbot/LootObjectStack.cpp").read_text()
ADD_LOOT = (ROOT / "ai/playerbot/strategy/actions/AddLootAction.cpp").read_text()


def between(text, start, end=None):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))] if end else text[i:]


def production_methods():
    """The production bodies under test, copied verbatim from their sources."""
    chunks = [
        between(STACK, "LootTarget::LootTarget(ObjectGuid guid, bool playerOrder)",
                "LootObject::LootObject(Player* bot"),
        between(STACK, "LootObject::LootObject(Player* bot, ObjectGuid guid, bool playerOrder)",
                "// May this bot open the corpse"),
        between(STACK, "static bool CollectCommonChestLoot(", "// An open chest holding only common"),
        between(STACK, "static bool IsCommonWorldProp(", "void LootObject::Refresh("),
        between(STACK, "void LootObject::Refresh(", "WorldObject* LootObject::GetWorldObject("),
        between(STACK, "WorldObject* LootObject::GetWorldObject(",
                "LootObject::LootObject(const LootObject& other)"),
        between(STACK, "LootObject::LootObject(const LootObject& other)",
                "// Is there anywhere at all for loot to go?"),
        between(STACK, "bool LootObjectStack::Add(ObjectGuid guid, bool playerOrder)",
                "void LootObjectStack::Remove("),
        between(STACK, "void LootObjectStack::Remove(ObjectGuid guid)",
                "void LootObjectStack::Clear("),
        between(STACK, "LootObject LootObjectStack::GetLoot(",
                "bool LootObjectStack::IsWithinMasterLootRange("),
        between(STACK, "std::vector<LootObject> LootObjectStack::OrderByDistance("),
    ]
    return "\n".join(chunks)


FIXTURE_HEAD = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

using uint8 = uint8_t;
using uint32 = uint32_t;
using int32 = int32_t;
using SkillType = uint32;
using LockType = int;

#define GAMEOBJECT_TYPE_CHEST 3
#define GO_READY 0
#define GO_STATE_ACTIVE 1
#define CORPSE 7
#define SKILL_NONE 0
#define SKILL_SKINNING 393
#define LOCK_KEY_ITEM 1
#define LOCK_KEY_SKILL 2
#define LOCK_KEY_NONE 0
#define UNIT_FIELD_FLAGS 0
#define UNIT_FLAG_SKINNABLE 0x20
#define UNIT_DYNAMIC_FLAGS 1
#define UNIT_DYNFLAG_LOOTABLE 0x1
#define ITEM_QUALITY_NORMAL 1
#define INTERACTION_DISTANCE 5.0f
#define MAX_LOOT_OBJECT_COUNT 10
#define LOOT_OBJECT_TTL_SECONDS 180

struct ObjectGuid
{
    uint64_t value = 0;
    ObjectGuid() {}
    explicit ObjectGuid(uint64_t v) : value(v) {}
    uint64_t GetRawValue() const { return value; }
    bool IsGameObject() const { return (value & 0x8000000000000000ULL) != 0; }
    bool IsCreature() const { return value != 0 && !IsGameObject(); }
    bool IsCreatureOrPet() const { return IsCreature(); }
    bool IsEmpty() const { return value == 0; }
    operator bool() const { return value != 0; }
    bool operator!() const { return value == 0; }
    bool operator<(ObjectGuid const& other) const { return value < other.value; }
    bool operator==(ObjectGuid const& other) const { return value == other.value; }
    bool operator!=(ObjectGuid const& other) const { return value != other.value; }
};

struct Player;
struct Creature;
struct GameObject;
struct LootObjectStack;
struct Group;
struct Unit { std::string GetName() const { return "unit"; } uint32 GetLevel() const { return 1; } };

struct WorldObject
{
    virtual ~WorldObject() {}
    virtual float getPositionZ() const { return 0.0f; }
    virtual std::string GetName() const { return "object"; }
};

struct LootItem { bool is_looted = false; bool freeforall = false; uint32 conditionId = 0; };
struct QuestItem { uint8 index = 0; bool is_looted = false; };
using QuestItemList = std::vector<QuestItem>;
using QuestItemMap = std::map<uint32, QuestItemList*>;

struct Loot
{
    std::vector<LootItem> items, m_questItems;
    QuestItemMap quests, ffa, conditional;
    uint64_t roundRobinPlayer = 0;
    bool hasItemFor(Player*) const { return false; }
    bool IsAllowedLooter(ObjectGuid, bool) const { return false; }
    bool isLooted() const { return true; }
    QuestItemMap const& GetPlayerQuestItems() const { return quests; }
    QuestItemMap const& GetPlayerFFAItems() const { return ffa; }
    QuestItemMap const& GetPlayerNonQuestNonFFAConditionalItems() const { return conditional; }
};

struct Creature : WorldObject
{
    Loot loot;
    bool HasFlag(int, uint32) const { return false; }
    bool IsSkinnableBy(Player*) const { return false; }
    uint32 GetLevel() const { return 1; }
};

struct GOInfo
{
    uint32 id = 0;
    uint32 lootId = 0;
    uint32 lockId = 0;
    uint32 GetLootId() const { return lootId; }
    uint32 GetLockId() const { return lockId; }
};

struct GameObject : WorldObject
{
    GOInfo info;
    float distance = 1.0f;
    uint32 GetEntry() const { return info.id; }
    GOInfo const* GetGOInfo() const { return &info; }
    int GetGoType() const { return GAMEOBJECT_TYPE_CHEST; }
    int getLootState() const { return GO_READY; }
    bool ActivateToQuest(Player*) { return false; }
};

inline std::map<uint64_t, GameObject*>& GameObjectRegistry()
{
    static std::map<uint64_t, GameObject*> registry;
    return registry;
}

struct Player
{
    ObjectGuid guid;
    std::string name = "bot";
    ObjectGuid GetObjectGuid() const { return guid; }
    uint32 GetGUIDLow() const { return uint32(guid.GetRawValue()); }
    ObjectGuid GetGUID() const { return guid; }
    uint32 GetSkillValue(uint32) const { return 0; }
    bool HasItemCount(uint32, uint32) const { return false; }
    bool IsAllowedToLoot(Creature*) const { return false; }
    std::string GetName() const { return name; }
    float getPositionZ() const { return 0.0f; }
    Group* GetGroup() const { return nullptr; }
    bool IsMounted() const { return false; }
    float GetDistance(WorldObject* wo) const
    {
        GameObject* go = dynamic_cast<GameObject*>(wo);
        return go ? go->distance : 1.0f;
    }
};

struct ServerFacade
{
    int GetDeathState(Creature*) const { return 0; }
    bool isSpawned(GameObject*) const { return true; }
    float getDistance2d(Player*, WorldObject*) const { return 1.0f; }
    bool IsDistanceGreaterThan(float a, float b) const { return a > b; }
    bool IsDistanceLessOrEqualThan(float a, float b) const { return a <= b; }
};
inline ServerFacade sServerFacade;

struct ItemPrototype { uint32 Quality = ITEM_QUALITY_NORMAL; };
struct ObjectMgr
{
    bool IsGameObjectForQuests(uint32) const { return false; }
    ItemPrototype const* GetItemPrototype(uint32) const { static ItemPrototype proto; return &proto; }
};
inline ObjectMgr sObjectMgr;

struct LootStoreItem { int32 mincountOrRef = 0; uint32 itemid = 0; };
struct LootLootGroupAccess
{
    std::vector<LootStoreItem> ExplicitlyChanced, EqualChanced;
};
struct LootTemplate
{
    std::vector<LootStoreItem> Entries;
    std::vector<LootLootGroupAccess> Groups;
};
using LootTemplateAccess = LootTemplate;

struct LootStore
{
    LootTemplate const* GetLootFor(uint32) const
    {
        // Every chest in this world holds one common key (the Deadmines gunpowder chest).
        static LootTemplate const chest = [] {
            LootTemplate t;
            t.Entries.push_back({0, 5397});
            return t;
        }();
        return &chest;
    }
};
inline LootStore LootTemplates_Gameobject;
inline LootStore LootTemplates_Reference;

struct LockEntry
{
    int Type[8] = {};
    uint32 Index[8] = {};
    uint32 Skill[8] = {};
};
struct LockStore
{
    LockEntry const* LookupEntry(uint32) const { static LockEntry lock; return &lock; }
};
inline LockStore sLockStore;
inline int SkillByLockType(LockType) { return 0; }

inline bool MayLootCorpse(Player*, Creature*) { return false; }

namespace ai
{
    inline bool CorpseHasBodyLootFor(Player*, Creature*) { return false; }
    inline bool HasVisiblePersonalItemFor(Loot const&, Player*) { return false; }
    inline std::string CorpseRefusalReason(bool, bool, bool, bool, bool) { return ""; }
    static const uint32 kBoneChewToyGoEntry = 1000380;

    struct ItemUsageValue
    {
        static bool IsNeededForQuest(Player*, uint32) { return false; }
    };
}

struct ValueBase { virtual ~ValueBase() {} };

template <class T>
struct Holder
{
    T value{};
    T& Get() { return value; }
};

template <class T>
struct Holder<T&>
{
    T value{};
    T& Get() { return value; }
};

struct AiObjectContext
{
    // Keyed by name: "nearest game objects no los" and "nearest corpses" share a type,
    // so one static per type would let the second assignment clear the first.
    template <class T>
    Holder<T>* GetValue(std::string const& name)
    {
        static std::map<std::string, Holder<T> > holders;
        return &holders[name];
    }
};

struct PlayerbotAI
{
    Player* master = nullptr;
    LootObjectStack* lootStack = nullptr;
    Player* GetMaster() { return master; }
    Creature* GetCreature(ObjectGuid) { return nullptr; }
    GameObject* GetGameObject(ObjectGuid guid)
    {
        auto i = GameObjectRegistry().find(guid.GetRawValue());
        return i == GameObjectRegistry().end() ? nullptr : i->second;
    }
    WorldObject* GetWorldObject(ObjectGuid guid)
    {
        return GameObjectRegistry()[guid.GetRawValue()];
    }
    void TellDebug(Player*, std::string const&, std::string const&) {}
    bool HasStrategy(std::string const&, int) { return false; }
    bool HasSkill(SkillType) { return true; }
    bool IsGroupLeader() { return false; }
    bool HasActivePlayerMaster() { return false; }
    bool CanLootSomethingFromWO(WorldObject*) { return false; }
    bool HasQuestItemsInWOLootList(WorldObject*) { return false; }
    bool DoSpecificAction(std::string const&) { return false; }
    void TellPlayer(Player*, std::string const&, int, bool) {}
    void TellError(Player*, std::string const&) {}
    std::list<Unit*> GetAllHostileNPCNonPetUnitsAroundWO(WorldObject*, float) { return {}; }
    AiObjectContext* GetAiObjectContext() { static AiObjectContext context; return &context; }
    template <class T>
    T Value(std::string const&)
    {
        if constexpr (std::is_same<T, LootObjectStack*>::value)
            return lootStack;
        else
            return T();
    }
};

struct PlayerbotAIStorage
{
    static PlayerbotAIStorage& Instance() { static PlayerbotAIStorage storage; return storage; }
    PlayerbotAI* GetAI(Player*) { static PlayerbotAI ai; return &ai; }
};

struct Logger
{
    template <class... Args>
    void outDebug(char const*, Args&&...) {}
};
inline Logger sLog;

struct AIConfig
{
    float lootDistance = 5.0f;
    float groupMemberLootDistance = 5.0f;
    float gatheringDistance = 5.0f;
    bool hireLoot = true;
};
inline AIConfig sPlayerbotAIConfig;

struct BotState
{
    static const int BOT_STATE_NON_COMBAT = 0;
    static const int BOT_STATE_COMBAT = 1;
};

struct PlayerbotSecurityLevel
{
    static const int PLAYERBOT_SECURITY_ALLOW_ALL = 0;
};

namespace TortoiseBots
{
    inline bool HireMayLoot(bool, bool) { return true; }
    struct HireLifecycle
    {
        static HireLifecycle& Instance() { static HireLifecycle lifecycle; return lifecycle; }
        bool IsHired(ObjectGuid) const { return false; }
    };
    struct BotCommands
    {
        static bool IsInCommandedGroup(Player*) { return false; }
    };
    struct RemoteCommander
    {
        static bool OwnLootTakesObject(bool, bool, bool) { return true; }
    };
}

namespace ChatHelper
{
    inline std::list<ObjectGuid> parseGameobjects(std::string const&) { return {}; }
    inline std::string formatWorldobject(WorldObject*) { return "object"; }
}

#define AI_VALUE(type, name) ai->Value<type>(name)

using namespace ai;

#include "GAMEOBJECT_PICKUP_POLICY"

class LootObject
{
public:
    LootObject() : skillId(0), reqSkillValue(0), reqItem(0) {}
    LootObject(Player* bot, ObjectGuid guid, bool playerOrder);
    LootObject(const LootObject& other);

    bool IsEmpty() { return !guid; }
    bool IsLootPossible(Player* bot);

    void Refresh(Player* bot, ObjectGuid guid, bool debug = false);
    WorldObject* GetWorldObject(Player* bot);

    ObjectGuid guid;
    bool playerOrder = false;
    uint32 skillId;
    uint32 reqSkillValue;
    uint32 reqItem;
};

class LootTarget
{
public:
    LootTarget(ObjectGuid guid, bool playerOrder = false);
    LootTarget(LootTarget const& other);
    LootTarget& operator=(LootTarget const& other);
    bool operator< (const LootTarget& other) const;

    ObjectGuid guid;
    time_t asOfTime;
    bool playerOrder;
};

class LootTargetList : public std::set<LootTarget>
{
public:
    void shrink(time_t fromTime);
};

class LootObjectStack
{
public:
    LootObjectStack(Player* bot) : bot(bot) {}
    bool Add(ObjectGuid guid, bool playerOrder = false);
    void Remove(ObjectGuid guid);
    LootObject GetLoot(float maxDistance = 0);
    std::vector<LootObject> OrderByDistance(float maxDistance = 0);
    bool IsWithinMasterLootRange(LootObject&) { return true; }
    bool IsAbandoned(ObjectGuid) { return false; }
    void NotePlayerOrder(ObjectGuid) {}
    void Clear() { availableLoot.clear(); approachFailures.clear(); playerOrders.clear(); }

private:
    Player* bot;
    LootTargetList availableLoot;
    std::map<ObjectGuid, std::pair<uint32, time_t> > approachFailures;
    std::map<ObjectGuid, time_t> playerOrders;
};

// The two stubs: what the core side would answer for a lootable chest.
bool LootObject::IsLootPossible(Player* bot)
{
    return !IsEmpty() && GetWorldObject(bot) != nullptr;
}

'''

ADD_ALL_FIXTURE = r'''
struct Event
{
    std::string source;
    Player* owner;
    std::string param;
    Event(std::string const& source, Player* owner, std::string const& param = "")
        : source(source), owner(owner), param(param) {}
    std::string const& getSource() const { return source; }
    Player* GetOwner() const { return owner; }
    std::string const& GetParam() const { return param; }
};

struct AddAllLootAction
{
    Player* bot;
    PlayerbotAI* ai;
    AiObjectContext* context;
    bool playerOrder = false;

    Player* GetMaster() { return bot; }
    bool AddLoot(Player* requester, ObjectGuid guid);
    bool Execute(Event& event);
    void TellBlocked(Player*, ObjectGuid, std::string const&) {}
};

#include "LOOT_WINDOW_POLICY"

'''

ADD_ALL_METHODS = "\n".join([
    between(ADD_LOOT, "bool AddAllLootAction::Execute(Event& event)",
            "bool AddLootAction::isUseful("),
    between(ADD_LOOT, "bool AddAllLootAction::AddLoot(Player* requester, ObjectGuid guid)",
            "bool AddGatheringLootAction::isUseful("),
])


def build_source():
    source = FIXTURE_HEAD + production_methods() + "\n" + ADD_ALL_FIXTURE + ADD_ALL_METHODS + "\n" + MAIN
    source = source.replace('"LOOT_WINDOW_POLICY"',
                            '"' + str(ROOT / "ai/playerbot/policy/loot/LootWindowPolicy.h") + '"')
    source = source.replace('"GAMEOBJECT_PICKUP_POLICY"',
                            '"' + str(ROOT / "ai/playerbot/policy/loot/GameObjectPickupPolicy.h") + '"')
    return source


MAIN = r'''
static GameObject orderedChest, automaticChest;
static Player bot;

static void World()
{
    bot.guid = ObjectGuid(1);
    orderedChest.info = GOInfo{17155, 2882, 0};
    orderedChest.distance = 1.0f;
    automaticChest.info = GOInfo{17155, 2882, 0};
    automaticChest.distance = 2.0f;
    GameObjectRegistry()[0x8000000000000001ULL] = &orderedChest;
    GameObjectRegistry()[0x8000000000000002ULL] = &automaticChest;
}

static void CheckStack()
{
    LootObjectStack stack(&bot);
    stack.Add(ObjectGuid(0x8000000000000001ULL), true);
    stack.Add(ObjectGuid(0x8000000000000002ULL), false);

    std::vector<LootObject> ordered = stack.OrderByDistance();
    assert(ordered.size() == 1);
    assert(ordered[0].guid == ObjectGuid(0x8000000000000001ULL));
    assert(ordered[0].playerOrder);
    assert(ordered[0].GetWorldObject(&bot) == &orderedChest);

    LootObject loot = stack.GetLoot();
    assert(loot.guid == ObjectGuid(0x8000000000000001ULL));
    assert(loot.GetWorldObject(&bot) == &orderedChest);

    stack.Remove(ObjectGuid(0x8000000000000001ULL));
    assert(stack.GetLoot().IsEmpty());
    assert(stack.OrderByDistance().empty());
}

static void CheckAddAllLoot()
{
    PlayerbotAI ai;
    LootObjectStack stack(&bot);
    ai.lootStack = &stack;
    AddAllLootAction action{&bot, &ai, ai.GetAiObjectContext()};

    // A player's whispered "loot"/"add all loot": the run owns the order.
    ai.GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("nearest game objects no los")->Get() =
        {ObjectGuid(0x8000000000000001ULL)};
    ai.GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("nearest corpses")->Get() = {};
    Event order("add all loot", &bot);
    assert(action.Execute(order));
    LootObject loot = stack.GetLoot();
    assert(loot.guid == ObjectGuid(0x8000000000000001ULL));
    assert(loot.GetWorldObject(&bot) == &orderedChest);

    // The automatic sweep ("often", no owner) still applies the common-loot filter.
    stack.Clear();
    ai.GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("nearest game objects no los")->Get() =
        {ObjectGuid(0x8000000000000001ULL)};
    Event automatic("often", nullptr);
    assert(!action.Execute(automatic));
    assert(stack.GetLoot().IsEmpty());
}

int main()
{
    World();
    CheckStack();
    CheckAddAllLoot();
    std::cout << "OK\n";
    return 0;
}
'''


class LootOrderBehaviourTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "g++ unavailable")
    def test_ordered_common_chest_survives_queue_and_add_all_loot(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = pathlib.Path(tmp) / "test.cpp"
            exe = pathlib.Path(tmp) / "test"
            src.write_text(build_source())
            compile = subprocess.run(
                ["g++", "-std=c++17", "-Wall", "-Wextra", "-o", str(exe), str(src)],
                capture_output=True, text=True)
            self.assertEqual(compile.returncode, 0, compile.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertIn("OK", run.stdout)


if __name__ == "__main__":
    unittest.main()
