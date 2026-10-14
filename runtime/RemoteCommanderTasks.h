#pragma once

// Remote commander (issue #884): the per-bot player orders `cmd loot`, `cmd open`
// and `cmd use`, and `cmd lootmethod`. A bound commander can do with its bots
// what a player does at a corpse, a chest, a lever or a bag: walk to it, open
// it, take the loot, use an item.
//
// The orders that need a walk or a cast run as a short task per bot: the glue in
// commands/BotCommands.cpp accepts the order (synchronous refusals answer at once),
// walks the bot to the object, does the one action and answers later with the
// final `TBM:ACTION_ACK` or `TBM:ACTION_ERR` of the same verb. Everything that
// decides is a pure function here, so tools/test_remote_commander_tasks.cpp
// exercises it standalone: the walk and act state machine with its budgets, the
// standing point, the lock verdict, the loot ledger the loot window reports into,
// the cast-failure mapping and the loot-method names.
//
// Plain data and the standard library only. World thread only, like the rest of
// the commander.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace TortoiseBots {
namespace RemoteCommander {

// How often the task sweep looks at the running tasks (never while none runs).
constexpr uint32_t kTaskTickMs = 250;
// The bot stands this far short of the object, on the side it comes from, so
// the walk ends inside every interaction range (loot, game object use).
constexpr float kApproachBackoff = 1.5f;
// A game object is used from inside INTERACTION_DISTANCE minus this margin, so the
// action's own range check passes after the last step.
constexpr float kInteractMargin = 0.5f;
// Walk budget: a floor plus the distance at a pessimistic walking speed, capped.
constexpr uint32_t kMinWalkMs = 10000;
constexpr uint32_t kMaxWalkMs = 60000;
constexpr float kAssumedWalkYardsPerSecond = 4.0f;
// No closer approach by at least kProgressYards for this long: the bot is stuck.
constexpr uint32_t kStallMs = 8000;
constexpr float kProgressYards = 1.0f;
// After the open: how long the loot window, the object's reaction or the cast
// may take to show before the order reports a timeout.
constexpr uint32_t kLootWindowMs = 8000;
constexpr uint32_t kOpenEffectMs = 4000;
constexpr uint32_t kUseCastMs = 15000;
// A nudge (a direct path step) is repeated at most this often while the bot stands still short of the object.
constexpr uint32_t kNudgeMs = 1000;

// ---------------------------------------------------------------------------
// Result codes (the second field of TBM:ACTION_ERR). The first group is the
// documented set; the rest are shared with the older commander orders.
// ---------------------------------------------------------------------------

constexpr char const* kCodeNoBot = "no-bot";
constexpr char const* kCodeTooFar = "too-far";
constexpr char const* kCodeNoItem = "no-item";
constexpr char const* kCodeLocked = "locked";
constexpr char const* kCodeBusy = "busy";
constexpr char const* kCodeInCombat = "in-combat";
constexpr char const* kCodeInvalid = "invalid";
constexpr char const* kCodeFailed = "failed";
constexpr char const* kCodeInvalidTarget = "invalid-target";
constexpr char const* kCodeUnavailable = "unavailable";
constexpr char const* kCodeUnreachable = "unreachable";
constexpr char const* kCodeTimeout = "timeout";
constexpr char const* kCodeCancelled = "cancelled";

// ---------------------------------------------------------------------------
// Standing point and budgets
// ---------------------------------------------------------------------------

struct Point3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// The point `backoff` yards short of the object on the line from the object to
// the bot (2D), at the object's height. A bot already closer than that stands
// where it is (its own x/y at the object's height).
inline Point3 StandingPoint(float botX, float botY, float objX, float objY, float objZ,
    float backoff = kApproachBackoff)
{
    Point3 point;
    point.z = objZ;
    float const dx = botX - objX;
    float const dy = botY - objY;
    float const length = std::sqrt(dx * dx + dy * dy);
    if (length <= backoff || length < 0.001f)
    {
        point.x = botX;
        point.y = botY;
        return point;
    }
    point.x = objX + dx / length * backoff;
    point.y = objY + dy / length * backoff;
    return point;
}

inline uint32_t WalkBudgetMs(float distance)
{
    float const walking = distance > 0.0f ? distance / kAssumedWalkYardsPerSecond * 1000.0f : 0.0f;
    uint64_t const budget = static_cast<uint64_t>(kMinWalkMs) + static_cast<uint64_t>(walking);
    return static_cast<uint32_t>(std::min<uint64_t>(budget, kMaxWalkMs));
}

// ---------------------------------------------------------------------------
// Task state machine
// ---------------------------------------------------------------------------

enum class TaskKind : uint8_t
{
    Loot,
    Open,
    Use,
};

enum class TaskStage : uint8_t
{
    Walk, // closing in on the object
    Act,  // the one action is under way; waiting for its result
};

struct TaskState
{
    TaskKind kind = TaskKind::Loot;
    TaskStage stage = TaskStage::Walk;
    uint32_t elapsedMs = 0;
    uint32_t stageMs = 0;
    uint32_t stalledMs = 0;
    float bestDistance = 1.0e9f;
    uint32_t walkBudgetMs = kMinWalkMs;
    uint32_t actBudgetMs = kLootWindowMs;
};

inline uint32_t ActBudgetMs(TaskKind kind)
{
    switch (kind)
    {
        case TaskKind::Loot: return kLootWindowMs;
        case TaskKind::Open: return kOpenEffectMs;
        case TaskKind::Use: return kUseCastMs;
    }
    return kLootWindowMs;
}

inline TaskState MakeTask(TaskKind kind, float startDistance, bool needsWalk)
{
    TaskState state;
    state.kind = kind;
    state.stage = needsWalk ? TaskStage::Walk : TaskStage::Act;
    state.walkBudgetMs = WalkBudgetMs(startDistance);
    state.actBudgetMs = ActBudgetMs(kind);
    return state;
}

struct TaskFacts
{
    bool botAvailable = true;   // in world, alive, not teleporting, on the target's map
    bool teleporting = false;   // alive but in a teleport or between maps (an item's teleport)
    bool actStarted = false;    // the action itself has been started (a cast is under way or over)
    bool inCombat = false;
    bool targetPresent = true;  // the object is still there (checked while walking)
    bool inRange = false;       // the bot stands inside the interaction range
    float distance = 0.0f;      // bot to object
    bool actDone = false;       // the action's result is in (window closed, effect seen, cast over)
};

enum class TaskStep : uint8_t
{
    Wait,
    BeginAct, // the walk is over: do the action now
    Succeed,
    Fail,
};

struct TaskVerdict
{
    TaskStep step = TaskStep::Wait;
    char const* code = "";
    char const* message = "";
};

inline TaskVerdict TaskFail(char const* code, char const* message)
{
    TaskVerdict verdict;
    verdict.step = TaskStep::Fail;
    verdict.code = code;
    verdict.message = message;
    return verdict;
}

// One look at a running task, `diffMs` after the last. Walk: refuse when the
// bot cannot act any more, a fight starts, the object is gone, the bot stalls or
// the budget runs out; begin the action when it stands in range. Act: succeed
// when the result is in, time out otherwise.
inline TaskVerdict AdvanceTask(TaskState& state, TaskFacts const& facts, uint32_t diffMs)
{
    state.elapsedMs += diffMs;
    state.stageMs += diffMs;
    TaskVerdict verdict;
    if (!facts.botAvailable)
    {
        // An item that teleports the bot (a hearthstone) ends with the bot between maps:
        // its cast completed, that is the result.
        if (facts.teleporting && facts.actStarted && state.kind == TaskKind::Use && state.stage == TaskStage::Act)
        {
            verdict.step = TaskStep::Succeed;
            return verdict;
        }
        return TaskFail(kCodeUnavailable, "The bot died, left the map or is teleporting.");
    }

    if (state.stage == TaskStage::Walk)
    {
        if (!facts.targetPresent)
            return TaskFail(kCodeInvalidTarget, "The object is gone.");
        if (facts.inCombat)
            return TaskFail(kCodeInCombat, "The bot is in combat and stopped.");
        if (facts.inRange)
        {
            state.stage = TaskStage::Act;
            state.stageMs = 0;
            verdict.step = TaskStep::BeginAct;
            return verdict;
        }
        if (facts.distance < state.bestDistance - kProgressYards)
        {
            state.bestDistance = facts.distance;
            state.stalledMs = 0;
        }
        else
            state.stalledMs += diffMs;
        if (state.stalledMs >= kStallMs)
            return TaskFail(kCodeUnreachable, "The bot stopped short of the object and cannot get closer.");
        if (state.elapsedMs >= state.walkBudgetMs)
            return TaskFail(kCodeTimeout, "The bot did not reach the object in time.");
        return verdict;
    }

    if (facts.actDone)
    {
        verdict.step = TaskStep::Succeed;
        return verdict;
    }
    if (state.stageMs >= state.actBudgetMs)
        return TaskFail(kCodeTimeout, "No result came back in time.");
    return verdict;
}

// ---------------------------------------------------------------------------
// Range checks
// ---------------------------------------------------------------------------

// The order is refused up front when the object is farther from the bot than the
// react distance (the same bound `cmd move` keeps).
inline bool WithinReactDistance(float distance, float reactDistance)
{
    return reactDistance > 0.0f && distance <= reactDistance;
}

// A game object is used from inside this distance (interactionDistance is the
// core's INTERACTION_DISTANCE).
inline bool InUseRange(float distance, float interactionDistance)
{
    return distance <= interactionDistance - kInteractMargin;
}

// ---------------------------------------------------------------------------
// Object state after a use
// ---------------------------------------------------------------------------

enum class ObjectEffect : uint8_t
{
    Waiting, // still the same state: no reaction yet
    Reacted, // the object changed state
    Gone,    // the object (or the map) is gone: not a result
};

// A used object shows its effect by changing state. One that disappears is not
// an "opened": a despawn or a map change during the wait fails the order.
inline ObjectEffect JudgeObjectEffect(bool present, uint32_t baseline, uint32_t now)
{
    if (!present)
        return ObjectEffect::Gone;
    return now != baseline ? ObjectEffect::Reacted : ObjectEffect::Waiting;
}

// ---------------------------------------------------------------------------
// Lock verdict
// ---------------------------------------------------------------------------

struct LockFacts
{
    uint32_t keyItem = 0;      // LOCK_KEY_ITEM row: the item that opens it (0 = none)
    bool hasKey = false;       // the bot carries that item
    uint32_t skillId = 0;      // LOCK_KEY_SKILL row: the skill that opens it (0 = none)
    uint32_t reqSkill = 0;     // the skill value it asks for
    uint32_t skillValue = 0;   // the bot's value in that skill
};

enum class LockVerdict : uint8_t
{
    Open,
    NeedsItem,
    Locked,
};

// The rows of a lock are alternatives: a carried key or enough skill opens it.
// A lock the bot cannot open asks for the key when it has a key row (the item
// to fetch), otherwise it is plainly locked.
inline LockVerdict CheckLock(LockFacts const& facts)
{
    bool const keyRow = facts.keyItem != 0;
    bool const skillRow = facts.skillId != 0;
    if (!keyRow && !skillRow)
        return LockVerdict::Open;
    if (keyRow && facts.hasKey)
        return LockVerdict::Open;
    if (skillRow && facts.skillValue >= facts.reqSkill)
        return LockVerdict::Open;
    return keyRow ? LockVerdict::NeedsItem : LockVerdict::Locked;
}

inline char const* LockVerdictCode(LockVerdict verdict)
{
    switch (verdict)
    {
        case LockVerdict::Open: return "ok";
        case LockVerdict::NeedsItem: return kCodeNoItem;
        case LockVerdict::Locked: return kCodeLocked;
    }
    return kCodeFailed;
}

inline char const* LockVerdictText(LockVerdict verdict)
{
    switch (verdict)
    {
        case LockVerdict::Open: return "ok";
        case LockVerdict::NeedsItem: return "The bot does not carry the key or item that opens this lock.";
        case LockVerdict::Locked: return "The lock needs more skill than the bot has.";
    }
    return "The bot cannot open it.";
}

// ---------------------------------------------------------------------------
// Cast failures
// ---------------------------------------------------------------------------

// The short reason of the view feed's party record (oom, range, los, cd, moving,
// casting, no-item, no-target) as an order code. Anything else is a plain failure.
inline char const* CastReasonCode(std::string const& reason)
{
    if (reason == "range")
        return kCodeTooFar;
    if (reason == "no-item")
        return kCodeNoItem;
    if (reason == "no-target")
        return kCodeInvalidTarget;
    if (reason == "cd" || reason == "casting" || reason == "moving")
        return kCodeBusy;
    return kCodeFailed;
}

// ---------------------------------------------------------------------------
// Loot ledger
// ---------------------------------------------------------------------------

// What a commanded loot order sees of its loot window. The order watches one
// object per bot; StoreLootAction (the bot's loot window handler) reports into
// it: every item taken, the money, and how the window ended. With a watch on the
// object, the window takes the order's item or everything its group rights allow,
// where the strategy's loot filter does not apply (an explicit order).
struct LootWatch
{
    uint64_t target = 0;
    uint32_t onlyItem = 0;      // 0 = everything
    bool finished = false;      // the loot window of the object has closed
    bool refused = false;       // the server refused the window
    uint32_t copper = 0;
    std::vector<std::pair<uint32_t, uint32_t>> items; // itemId, count
};

class LootLedger
{
public:
    void Watch(uint64_t bot, uint64_t target, uint32_t onlyItem)
    {
        LootWatch watch;
        watch.target = target;
        watch.onlyItem = onlyItem;
        m_watches[bot] = watch;
    }

    void Forget(uint64_t bot) { m_watches.erase(bot); }

    LootWatch const* Find(uint64_t bot) const
    {
        auto it = m_watches.find(bot);
        return it == m_watches.end() ? nullptr : &it->second;
    }

    // True when the bot has a watch on this object; onlyItem is the order's filter.
    bool Watching(uint64_t bot, uint64_t target, uint32_t& onlyItem) const
    {
        LootWatch const* watch = Find(bot);
        if (!watch || watch->target != target || watch->finished)
            return false;
        onlyItem = watch->onlyItem;
        return true;
    }

    void NoteItem(uint64_t bot, uint64_t target, uint32_t itemId, uint32_t count)
    {
        LootWatch* watch = Mine(bot, target);
        if (!watch || !itemId || !count)
            return;
        for (auto& entry : watch->items)
            if (entry.first == itemId)
            {
                entry.second += count;
                return;
            }
        watch->items.emplace_back(itemId, count);
    }

    void NoteCopper(uint64_t bot, uint64_t target, uint32_t copper)
    {
        if (LootWatch* watch = Mine(bot, target))
            watch->copper += copper;
    }

    void NoteWindowEnd(uint64_t bot, uint64_t target, bool refused)
    {
        if (LootWatch* watch = Mine(bot, target))
        {
            watch->finished = true;
            watch->refused = refused;
        }
    }

    bool Empty() const { return m_watches.empty(); }
    void Clear() { m_watches.clear(); }

private:
    LootWatch* Mine(uint64_t bot, uint64_t target)
    {
        auto it = m_watches.find(bot);
        if (it == m_watches.end() || it->second.target != target || it->second.finished)
            return nullptr;
        return &it->second;
    }

    std::unordered_map<uint64_t, LootWatch> m_watches;
};

// The one ledger. StoreLootAction reports into it, the commander glue reads it.
inline LootLedger& LootOrders()
{
    static LootLedger ledger;
    return ledger;
}

// Does the order's filter let this item through (0 = everything)?
inline bool LootItemWanted(uint32_t onlyItem, uint32_t itemId)
{
    return onlyItem == 0 || onlyItem == itemId;
}

inline uint32_t LootedItemCount(LootWatch const& watch)
{
    uint32_t total = 0;
    for (auto const& entry : watch.items)
        total += entry.second;
    return total;
}

// "7741:1,7740:1" (+ ",copper:N" when money was taken); "-" when nothing was taken.
inline std::string FormatLootedItems(LootWatch const& watch)
{
    std::string text;
    for (auto const& entry : watch.items)
    {
        if (!text.empty())
            text += ',';
        text += std::to_string(entry.first) + ":" + std::to_string(entry.second);
    }
    if (watch.copper)
    {
        if (!text.empty())
            text += ',';
        text += "copper:" + std::to_string(watch.copper);
    }
    return text.empty() ? std::string("-") : text;
}

// ---------------------------------------------------------------------------
// Loot method
// ---------------------------------------------------------------------------

// Values of the core's LootMethod (Group.h): the same numbers the client sends.
constexpr int kLootFfa = 0;
constexpr int kLootRoundRobin = 1;
constexpr int kLootMaster = 2;
constexpr int kLootGroup = 3;
constexpr int kLootNeedBeforeGreed = 4;
// Loot thresholds the core accepts (ItemQualities): common to legendary.
constexpr int kThresholdMin = 1;
constexpr int kThresholdMax = 5;

inline std::string LowerAscii(std::string text)
{
    for (char& c : text)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return text;
}

inline bool ParseLootMethod(std::string const& token, int& method)
{
    std::string const word = LowerAscii(token);
    if (word == "ffa" || word == "freeforall" || word == "free")
        method = kLootFfa;
    else if (word == "rr" || word == "roundrobin")
        method = kLootRoundRobin;
    else if (word == "master" || word == "masterloot" || word == "ml")
        method = kLootMaster;
    else if (word == "group" || word == "grouploot")
        method = kLootGroup;
    else if (word == "nbg" || word == "needbeforegreed")
        method = kLootNeedBeforeGreed;
    else
        return false;
    return true;
}

inline char const* LootMethodName(int method)
{
    switch (method)
    {
        case kLootFfa: return "ffa";
        case kLootRoundRobin: return "rr";
        case kLootMaster: return "master";
        case kLootGroup: return "group";
        case kLootNeedBeforeGreed: return "nbg";
    }
    return "unknown";
}

// A quality number 1-5, or common/green/uncommon/blue/rare/purple/epic/orange/legendary.
inline bool ParseLootThreshold(std::string const& token, int& threshold)
{
    std::string const word = LowerAscii(token);
    if (word.size() == 1 && word[0] >= '0' && word[0] <= '9')
        threshold = word[0] - '0';
    else if (word == "common" || word == "white")
        threshold = 1;
    else if (word == "uncommon" || word == "green")
        threshold = 2;
    else if (word == "rare" || word == "blue")
        threshold = 3;
    else if (word == "epic" || word == "purple")
        threshold = 4;
    else if (word == "legendary" || word == "orange")
        threshold = 5;
    else
        return false;
    return threshold >= kThresholdMin && threshold <= kThresholdMax;
}

} // namespace RemoteCommander
} // namespace TortoiseBots
