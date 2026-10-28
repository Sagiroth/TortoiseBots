#pragma once

// Remote commander telemetry (issue #884 follow-up): what the observability
// dashboard's Commanders tab shows. A COMMANDERS datagram lists each standing
// binding (commander, lead bot, the bound bots with their vitals and
// position, and the commander's last orders with the module's answer).
//
// Map data (additive fields, old daemons ignore them): the commander's own
// position when online, each bound bot's target, and the hostile units near
// the lead (the list the commander's TBM:VIEW feed builds, capped), so the
// dashboard map can draw mobs, threat lines and aggro rings.
//
// Unit info (additive, same rule): each bot's activity (state letter, newest
// action, spell being cast, crowd control, drinking/eating, moving, target
// name), each unit's level, rank (elite/rare/boss), spell being cast and
// crowd control, and when the binding began, so the dashboard can say what
// every character is doing and answer a hover over any unit.
//
// Map drawing (additive, same rule): the spell a bot is casting also names its target (a healer's heal target, a crowd-control
// caster's mob), the game objects the bots have seen near them (doors, chests, altars, gongs: type and state, capped), and, for a run
// leader's run, the plan's steps of the current section (capped, rebuilt only when the step moves). The dashboard draws paths,
// run steps, objects, stall spots and combat lines from these and from its own memory of the 2 s reports.
//
// Bosses (additive, same rule; owner request 2026-10-21): while the lead stands on a dungeon or raid map, every known boss of that map
// (BossInfo) at its spawn point with its state in the lead's instance, so the dashboard map showed all of them all the time, alive or
// dead, and its run line could count the bosses killed of the run's bosses.
//
// Cost rule: nothing here runs per bot tick. The emitter asks for the
// bindings once per 2 s snapshot cycle, and only while at least one binding
// exists; an order is noted when its reply is sent (a few per minute). The
// order log is bounded per commander and in the number of commanders.
//
// Standard library only (Player is a forward declaration), so
// tools/test_commander_telemetry.cpp compiles it without a game header.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Player;

namespace TortoiseBots {
namespace CommanderTelemetry {

constexpr size_t kMaxOrdersPerCommander = 8;
constexpr size_t kMaxTrackedCommanders = 16;
constexpr size_t kMaxBindings = 8;     // per payload: keeps one datagram well under the loopback MTU
constexpr size_t kMaxBotsPerBinding = 40;   // a raid binding is the only bound raid (kMaxRaidBindSize)
constexpr size_t kMaxBotsPerPayload = 64;   // all bindings together: the heaviest raid plus a party, or four parties
constexpr size_t kMaxMobsPerBinding = 24; // the perception feed's own cap (kMaxHostiles)
constexpr size_t kMaxMobsPerPayload = 64; // all bindings together: the datagram stays well under 64 KiB
constexpr size_t kMaxMobName = 24;
constexpr size_t kMaxCastName = 24;     // a spell name
constexpr size_t kMaxActionName = 32;   // the bot's newest action name
constexpr size_t kMaxTargetName = 24;
constexpr size_t kMaxRunWord = 32;      // plan, state, leg, kind and phase names of a run
constexpr size_t kMaxRunNote = 120;     // what the run does now, or why it paused
constexpr size_t kMaxRunNotes = 30;     // the run's last status lines (a notes ring per run)
constexpr size_t kMaxRunSteps = 20;     // plan steps listed per run (the current section's next ones)
constexpr size_t kMaxRunStepsPerPayload = 32; // all bindings together
constexpr size_t kMaxObjectsPerBinding = 16;
constexpr size_t kMaxObjectsPerPayload = 24;
constexpr size_t kMaxObjectName = 24;
constexpr size_t kMaxBossesPerBinding = 32; // a longer roster is not sent at all (never cut short); the dashboard keeps 32 too
constexpr size_t kMaxBossesPerPayload = 48;
constexpr size_t kMaxBossName = 24;
constexpr size_t kPayloadByteBudget = 60000; // the boss lists are left out of the bindings that would pass it
constexpr size_t kMaxOrderDetail = 80;
constexpr size_t kMaxOrderResult = 120;

// One order of a commander and the module's answer to it.
struct OrderRecord
{
    int64_t at = 0;       // unix seconds
    std::string verb;     // "move", "attack", ... (the intent without "cmd ")
    std::string detail;   // the order's arguments as typed (clipped)
    bool ok = false;      // TBM:ACTION_ACK (true) or TBM:ACTION_ERR (false)
    std::string result;   // ack: the scope; err: "<code>: <message>" (clipped)
};

// Crowd control as one letter, the first that holds: s stunned, c confused (polymorph, sleep and the like), f fleeing
// (feared or running), r rooted; 0 = free.
inline char CcKind(bool stunned, bool confused, bool fleeing, bool rooted)
{
    return stunned ? 's' : confused ? 'c' : fleeing ? 'f' : rooted ? 'r' : 0;
}

// A creature's rank as one letter: b boss (world boss rank, one that binds the instance, or a dungeon boss), e elite,
// r rare (or rare elite); 0 = an ordinary unit. A five-man boss carries rank 1 and no instance bind in the core's data
// (Lord Cobrahn 3669, Oggleflint 11517), so `planBoss` makes it a boss: DungeonPlanTable::IsBossEntry, the entry of a
// dungeon plan's boss step or an `ai_dungeon_boss` row, both by map.
inline char RankKind(uint32_t rank, bool bindsInstance, bool planBoss)
{
    // rank: 0 normal, 1 elite, 2 rare elite, 3 world boss, 4 rare
    if (rank == 3 || bindsInstance || planBoss)
        return 'b';
    if (rank == 1)
        return 'e';
    if (rank == 2 || rank == 4)
        return 'r';
    return 0;
}

// A UTF-8-safe clip: never cuts inside a multi-byte sequence.
inline std::string Clip(std::string const& s, size_t max)
{
    if (s.size() <= max)
        return s;
    size_t cut = max;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80)
        --cut;
    return s.substr(0, cut);
}

// Bounded log of the commanders' last orders. World thread only.
class OrderLog
{
public:
    void Note(uint64_t commander, OrderRecord rec)
    {
        if (!commander)
            return;
        rec.detail = Clip(rec.detail, kMaxOrderDetail);
        rec.result = Clip(rec.result, kMaxOrderResult);
        auto it = m_orders.find(commander);
        if (it == m_orders.end())
        {
            if (m_orders.size() >= kMaxTrackedCommanders)
            {
                // Drop the commander noted longest ago.
                uint64_t const oldest = m_arrival.front();
                m_arrival.pop_front();
                m_orders.erase(oldest);
            }
            it = m_orders.emplace(commander, std::deque<OrderRecord>()).first;
            m_arrival.push_back(commander);
        }
        it->second.push_back(std::move(rec));
        while (it->second.size() > kMaxOrdersPerCommander)
            it->second.pop_front();
    }

    // Oldest first.
    std::vector<OrderRecord> Recent(uint64_t commander) const
    {
        auto it = m_orders.find(commander);
        if (it == m_orders.end())
            return {};
        return std::vector<OrderRecord>(it->second.begin(), it->second.end());
    }

    // Forget every commander that is not in keep (a released binding).
    void Retain(std::unordered_set<uint64_t> const& keep)
    {
        for (auto it = m_orders.begin(); it != m_orders.end();)
            it = keep.count(it->first) ? std::next(it) : m_orders.erase(it);
        for (auto it = m_arrival.begin(); it != m_arrival.end();)
            it = m_orders.count(*it) ? std::next(it) : m_arrival.erase(it);
    }

    void Clear()
    {
        m_orders.clear();
        m_arrival.clear();
    }

    size_t Commanders() const { return m_orders.size(); }

private:
    std::unordered_map<uint64_t, std::deque<OrderRecord>> m_orders;
    std::deque<uint64_t> m_arrival; // commanders in the order they first noted an order
};

// One hostile-ish unit near the lead, as the dashboard shows it.
struct MobInfo
{
    uint64_t id = 0;       // raw guid (a decimal string on the wire: 64 bits do not fit a JSON number)
    uint32_t entry = 0;
    std::string name;
    uint32_t hpPct = 0;
    float x = 0.0f;
    float y = 0.0f;
    uint64_t victim = 0;   // who the unit targets (raw guid), 0 = nobody
    char reaction = 'h';   // h hostile, n neutral, f friendly (to the bound bots)
    char kind = 0;         // 0 ordinary, 'k' critter, 'v' civilian, 'g' guard, 'u' unattackable
    uint32_t level = 0;    // 0 = unknown
    char rank = 0;         // RankKind: 0 ordinary, 'e' elite, 'r' rare, 'b' boss
    char cc = 0;           // CcKind: 0 free, 's' stunned, 'c' confused, 'f' fleeing, 'r' rooted
    std::string cast;      // name of the spell it is casting, empty = none
};

// One step of the run leader's plan, as the dashboard draws it (a dashed route for moves, numbered circles for pulls, marks for the rest).
struct RunStepInfo
{
    uint32_t index = 0;    // 1-based position in the plan (the "step" of the run counts the same way)
    std::string kind;      // move, move_direct, pull, boss, loot, open, use_item, wait_object, wait_clear, rest, ritual
    float x = 0.0f;
    float y = 0.0f;
    float radius = 0.0f;   // pull, boss and wait_clear: the area the step cares about
    uint32_t entry = 0;    // the creature or object the step is about (a boss, a chest), 0 = none
    std::string name;      // that creature's name when known, empty = unknown
    std::string label;     // the row's own comment (the plan step's short description), empty = none
    bool skipped = false;  // the step was left out with `.bot run skip`
};

// A game object a bound bot has seen near it. type: d door, c chest, b button or lever, g goober (gong, altar, switch), r ritual, o other.
// state: r ready (closed, untouched), a active (open, pressed), u used (looted or spent).
struct ObjectInfo
{
    uint64_t id = 0;
    uint32_t entry = 0;
    std::string name;
    float x = 0.0f;
    float y = 0.0f;
    char type = 'o';
    char state = 'r';
};

// One known boss of the lead's dungeon or raid map (owner 2026-10-21: every boss on the dashboard map, alive or dead): a creature of an
// `ai_dungeon_boss` row, a plan's boss step, world-boss rank or the instance-bind flag, at its spawn point, with its state in the lead's
// own instance. state: a alive, d killed, u not known (its grid is not loaded, or it has not spawned). inPlan: a plan of the map has a boss
// step for the entry (the dashboard counts these as the run's bosses).
struct BossInfo
{
    uint32_t entry = 0;
    std::string name;
    float x = 0.0f;
    float y = 0.0f;
    char state = 'u';
    bool inPlan = false;
};

// The state of one boss spawn in an instance: found on the map -> alive, or a corpse (dead); not found -> dead while the instance still
// holds its respawn time (killed, corpse gone), else unknown (the grid is not loaded, or the creature never spawned there).
inline char BossState(bool found, bool alive, bool respawnPending)
{
    if (found)
        return alive ? 'a' : 'd';
    return respawnPending ? 'd' : 'u';
}

// The same from what the game hands over for one spawn row: whether the creature is on the instance's map and alive, and the respawn
// time the instance holds for that row (unix seconds, 0 = none). A respawn time still in the future means it was killed.
inline char BossSpawnState(bool found, bool alive, int64_t respawnAt, int64_t now)
{
    return BossState(found, alive, respawnAt > now);
}

// Of two reads of one boss entry (several spawn rows of one entry), the stronger: alive beats dead beats unknown; any other letter is unknown.
inline char StrongerBossState(char a, char b)
{
    auto rankOf = [](char st) { return st == 'a' ? 2 : st == 'd' ? 1 : 0; };
    int const ra = rankOf(a), rb = rankOf(b);
    int const r = ra > rb ? ra : rb;
    return r == 2 ? 'a' : r == 1 ? 'd' : 'u';
}

// One spawn row of a known boss as the spawn cache reads it: the `creature` row (0 for a plan boss step with no spawn row) and the boss.
struct BossSpawnRow
{
    uint32_t dbGuid = 0;
    BossInfo info;
};

// One entry of a map's roster with every spawn row of it (the cache's shape: each report reads the rows' states and keeps the strongest).
struct BossEntrySpawns
{
    BossInfo info;                 // the first spawn's place and name; inPlan when any spawn is
    std::vector<uint32_t> dbGuids; // the entry's spawn rows (empty: a plan boss with no spawn row)
};

// The spawns of a map grouped by entry, once when the cache is built (Codex review 2026-10-22: the report used to search its growing output
// for every spawn): one row per entry in the order the entries first appear, the first spawn's place and name kept, in the plan when any
// spawn is; a zero entry is dropped and a zero guid adds no spawn row. Not capped: a roster longer than kMaxBossesPerBinding is left out of
// the payload whole (BuildPayload), never cut short.
inline std::vector<BossEntrySpawns> GroupBossSpawns(std::vector<BossSpawnRow> const& rows)
{
    std::vector<BossEntrySpawns> out;
    std::unordered_map<uint32_t, size_t> at; // entry -> its row in out
    for (BossSpawnRow const& r : rows)
    {
        if (!r.info.entry)
            continue;
        auto const it = at.emplace(r.info.entry, out.size());
        if (it.second)
        {
            BossEntrySpawns e;
            e.info = r.info;
            e.info.state = 'u';
            out.push_back(std::move(e));
        }
        BossEntrySpawns& e = out[it.first->second];
        e.info.inPlan = e.info.inPlan || r.info.inPlan;
        if (r.dbGuid)
            e.dbGuids.push_back(r.dbGuid);
    }
    return out;
}

// The killed latch (Codex review 2026-10-22, lead's decision): a killed boss whose respawn time ran out while its grid was not loaded is
// not found and has no respawn time any more, so it read 'u' although nobody saw it alive. Once an entry was read killed ('d') in an
// instance it stays killed there until a living creature of the entry is read ('a': the latch is cleared), the instance id is gone (a reset
// deletes the respawn times and gives a new instance id, which starts fresh) or no binding has stood on the instance for kForgetAfterS
// (Expire). At most kMaxLatches latches: past that the oldest is dropped. World thread only (the 2 s commander snapshot).
class BossKillLatch
{
public:
    static constexpr size_t kMaxLatches = 512;
    static constexpr int64_t kForgetAfterS = 600;

    // The state to report for one entry read as `state` in an instance at `now` (unix seconds). Instance id 0 is never latched.
    char Apply(uint32_t instanceId, uint32_t entry, char state, int64_t now)
    {
        if (!instanceId)
            return state;
        lastSeen_[instanceId] = now;
        Key const key{instanceId, entry};
        if (state == 'a')
        {
            latches_.erase(key);
            return state;
        }
        if (state == 'd')
        {
            if (latches_.find(key) == latches_.end())
            {
                if (latches_.size() >= kMaxLatches)
                    DropOldest();
                latches_.emplace(key, next_++);
            }
            return state;
        }
        return latches_.count(key) ? 'd' : state;
    }

    // Forget the instances no binding has stood on for kForgetAfterS, with their latches.
    void Expire(int64_t now)
    {
        for (auto it = lastSeen_.begin(); it != lastSeen_.end();)
        {
            if (now - it->second < kForgetAfterS)
            {
                ++it;
                continue;
            }
            uint32_t const id = it->first;
            latches_.erase(latches_.lower_bound(Key{id, 0}), latches_.upper_bound(Key{id, UINT32_MAX}));
            it = lastSeen_.erase(it);
        }
    }

    bool Holds(uint32_t instanceId, uint32_t entry) const { return latches_.count(Key{instanceId, entry}) != 0; }
    size_t Size() const { return latches_.size(); }

private:
    using Key = std::pair<uint32_t, uint32_t>; // (instance id, entry)

    void DropOldest()
    {
        auto oldest = latches_.begin();
        for (auto it = latches_.begin(); it != latches_.end(); ++it)
            if (it->second < oldest->second)
                oldest = it;
        if (oldest != latches_.end())
            latches_.erase(oldest);
    }

    std::map<Key, uint64_t> latches_;       // -> the order it was latched in (the oldest is dropped first)
    std::map<uint32_t, int64_t> lastSeen_;  // instance id -> when a binding last read it
    uint64_t next_ = 0;
};

// Which steps of a plan are listed: the current section's (leg's) steps, at most `cap`. legFirst..legEnd is the 0-based half-open range
// of the section in the plan's step list, `current` the 0-based step the run stands at (it may lie past the end of a finished run).
// A section longer than the cap shows the window that keeps the current step a few steps from its start, so what is coming up is
// listed and the last cleared steps stay in sight. Returns {first, end} 0-based, half-open; empty when the section is.
inline std::pair<size_t, size_t> RunStepWindow(size_t legFirst, size_t legEnd, size_t current, size_t cap = kMaxRunSteps)
{
    if (legEnd <= legFirst || cap == 0)
        return {legFirst, legFirst};
    size_t const total = legEnd - legFirst;
    if (total <= cap)
        return {legFirst, legEnd};
    size_t const keepBehind = cap / 4;
    size_t const cur = current < legFirst ? legFirst : (current >= legEnd ? legEnd - 1 : current);
    size_t first = cur > legFirst + keepBehind ? cur - keepBehind : legFirst;
    if (first + cap > legEnd)
        first = legEnd - cap;
    return {first, first + cap};
}

// One status line of the run leader's run (TBM:RUN changed: a step, phase,
// state or note moved). A run keeps the last kMaxRunNotes, oldest first, so
// the dashboard can say why it waits and what it did before.
struct RunNoteInfo
{
    int64_t at = 0;          // unix seconds the line went out
    uint32_t step = 0;       // 1-based step shown at the time
    std::string leg;         // the current leg's name at the time
    std::string kind;        // the step kind at the time
    std::string phase;       // what the step was doing
    std::string state;       // running, paused or done
    std::string note;        // what it did then, or why it paused
};

// The run leader's run of a binding (`.bot run`, behind AiPlayerbot.DungeonSelfClear): where it stands in the dungeon plan. Only a binding
// with a run carries it; the dashboard shows it as the "observe" mode of the dungeon modes (the bots clear, the commander watches).
struct RunInfo
{
    bool present = false;
    std::string plan;       // the plan's name ("uldaman")
    std::string state;      // running, paused or done
    uint32_t step = 0;      // 1-based step shown (0 before the first)
    uint32_t steps = 0;     // steps in the plan
    std::string leg;        // the current leg's name (the section)
    std::string kind;       // the step kind (move, pull, boss ...)
    std::string phase;      // what the step is doing (walk, pull, rest ...)
    std::string label;      // the current step's own comment (the plan row's short description), empty = none
    std::string note;       // what it does now, or why it paused
    int64_t changedAt = 0;  // unix seconds the step, phase, state or note last changed
    std::vector<RunStepInfo> stepList; // the current section's steps (see RunStepWindow); empty = not reported
    std::vector<RunNoteInfo> notes;    // the last status lines, oldest first, at most kMaxRunNotes (the newest)
};

// What a bound bot is doing, read next to the view snapshot (same cadence, no extra scan).
struct BotExtra
{
    uint64_t id = 0;        // the bot's raw guid
    char state = 'n';       // c combat, n non-combat, d dead, r reaction (the AI's state)
    std::string action;     // the engine's newest action verdict name
    char result = '-';      // O ok, F failed, I impossible, U useless, P prereq, V vetoed, - none
    std::string cast;       // spell being cast or channelled, empty = none
    char cc = 0;            // CcKind
    char rest = 0;          // 'd' drinking, 'e' eating, 0 neither
    bool moving = false;
    std::string targetName; // name of its target, empty = none or unknown
    uint64_t castTarget = 0; // the unit the spell being cast is aimed at (raw guid), 0 = none or not a unit
};

// The raid layout the module applied to the binding's group (TBM:RAID_LAYOUT), so the card names the tanks and healers it placed.
struct RaidLayoutInfo
{
    bool present = false;
    std::string mainTank;                                      // the lead bot the layout made main tank
    std::vector<std::string> offTanks;                         // the assist tanks, in layout order
    std::vector<std::pair<std::string, std::string>> tankHealers; // healer, the tank it was given focus on
    std::vector<std::string> raidHealers;                      // the healers left healing the raid
};

// A standing binding as the commands layer reports it: live objects, read on
// the world thread.
struct BindingRef
{
    std::string commander;      // the commander's name; "offline #<guid>" when not found
    uint32_t commanderGuid = 0; // the commander's character guid (low part), also when offline
    bool commanderOnline = false;
    bool viewOn = false;        // the TBM:VIEW perception feed is on
    uint32_t groupId = 0;
    Player* lead = nullptr;     // may be null while the lead bot is between maps
    std::vector<Player*> bots;  // lead bot first
    std::vector<OrderRecord> orders;
    // Map data, filled by the commands layer (see CollectCommanderTelemetry).
    bool commanderPlaced = false; // commanderMap/X/Y are valid (online and in the world)
    uint32_t commanderMap = 0;
    float commanderX = 0.0f;
    float commanderY = 0.0f;
    bool mobsReported = false;    // the mob list was read (it may be empty)
    uint32_t mobsMap = 0;         // the map the mobs are on (the lead's)
    std::vector<MobInfo> mobs;
    std::vector<BotExtra> botExtras; // one per bot read from the view snapshot, matched by id
    int64_t boundAt = 0;             // unix seconds the binding was first seen standing, 0 = unknown
    RunInfo run;                     // the run leader's run of this binding, when it has one
    RaidLayoutInfo layout;           // the raid layout the module applied to its group, when it applied one
    std::unordered_set<uint64_t> awayBots; // raw guids of bots off the run's gather rule or the lead's map
    bool objectsReported = false;    // the object list was read (it may be empty)
    std::vector<ObjectInfo> objects; // game objects the bots have seen, on the lead's map
    bool bossesReported = false;     // the lead is on a dungeon or raid map and its boss list was read (it may be empty)
    uint32_t bossesMap = 0;          // that map
    std::vector<BossInfo> bosses;    // every known boss of it, with its state in the lead's instance (one row per entry)
    std::string combatJson;          // the live fight window (`combat` object), "" outside a fight
};

// One bound bot as the dashboard shows it.
struct BotInfo
{
    std::string name;
    uint32_t guid = 0;
    std::string className;
    std::string role;
    uint32_t level = 0;
    uint32_t hp = 0;
    uint32_t maxHp = 0;
    uint32_t power = 0;
    uint32_t maxPower = 0;
    std::string powerType;
    bool alive = true;
    bool lead = false;
    uint32_t mapId = 0;
    uint32_t zoneId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    uint64_t id = 0;     // raw guid, 0 = not reported
    uint64_t target = 0; // raw guid of its target, 0 = none
    bool hasExtra = false; // the fields below were read
    int subgroup = -1;     // the raid subgroup 0-7, -1 when not in a raid group
    bool away = false;     // off the run's gather rule or the lead's map
    BotExtra extra;
};

struct BindingInfo
{
    std::string commander;
    uint32_t commanderGuid = 0; // the dashboard opens this character's detail view from it
    bool commanderOnline = false;
    bool viewOn = false;
    uint32_t groupId = 0;
    std::string lead;
    std::vector<BotInfo> bots;
    std::vector<OrderRecord> orders;
    bool commanderPlaced = false;
    uint32_t commanderMap = 0;
    float commanderX = 0.0f;
    float commanderY = 0.0f;
    bool mobsReported = false;
    uint32_t mobsMap = 0;
    std::vector<MobInfo> mobs;
    int64_t boundAt = 0;
    RunInfo run;
    RaidLayoutInfo layout;
    bool objectsReported = false;
    std::vector<ObjectInfo> objects;
    bool bossesReported = false;
    uint32_t bossesMap = 0;
    std::vector<BossInfo> bosses;
    std::string combatJson;          // the live fight window (`combat` object), "" outside a fight
};

inline std::string JsonEscape(std::string const& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20)
            {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            }
            else
                out += static_cast<char>(c);
        }
    }
    return out;
}

// One mob object as COMMANDERS and DUNGEON_MOBS write it. The stream must be set to fixed, precision 1 (both builders do).
inline void AppendMobJson(std::ostringstream& ss, MobInfo const& m)
{
    ss << "{\"id\":\"" << m.id << "\",\"entry\":" << m.entry
       << ",\"name\":\"" << JsonEscape(Clip(m.name, kMaxMobName)) << "\""
       << ",\"hp\":" << m.hpPct
       << ",\"x\":" << m.x << ",\"y\":" << m.y;
    if (m.victim)
        ss << ",\"victim\":\"" << m.victim << "\"";
    ss << ",\"reaction\":\"" << (m.reaction == 'n' || m.reaction == 'f' ? m.reaction : 'h') << "\"";
    if (m.kind == 'k' || m.kind == 'v' || m.kind == 'g' || m.kind == 'u')
        ss << ",\"kind\":\"" << m.kind << "\"";
    if (m.level)
        ss << ",\"lvl\":" << m.level;
    if (m.rank == 'b' || m.rank == 'e' || m.rank == 'r')
        ss << ",\"rank\":\"" << m.rank << "\"";
    if (m.cc == 's' || m.cc == 'c' || m.cc == 'f' || m.cc == 'r')
        ss << ",\"cc\":\"" << m.cc << "\"";
    if (!m.cast.empty())
        ss << ",\"cast\":\"" << JsonEscape(Clip(m.cast, kMaxCastName)) << "\"";
    ss << "}";
}

// The COMMANDERS datagram. An empty list is a valid payload: it tells the
// daemon the last binding ended. The boss lists go in last, binding by binding, only while the datagram stays within
// kPayloadByteBudget; a binding whose list does not fit leaves `bosses_map` and `bosses` out (the dashboard falls back).
inline std::string BuildPayload(int protocolVersion, uint64_t session, uint64_t seq, int64_t ts,
                                std::vector<BindingInfo> const& bindings)
{
    std::ostringstream ss;
    ss.setf(std::ios::fixed);
    ss.precision(1);
    ss << "{\"v\":" << protocolVersion
       << ",\"session\":" << session
       << ",\"seq\":" << seq
       << ",\"ts\":" << ts
       << ",\"type\":\"COMMANDERS\",\"commanders\":[";
    size_t shown = 0;
    size_t mobsTotal = 0;
    size_t stepsTotal = 0;
    size_t botsTotal = 0;
    size_t objectsTotal = 0;
    struct BossList
    {
        size_t at;        // where the list goes: before its binding's orders
        std::string json;
        size_t count;     // bosses in it
    };
    std::vector<BossList> bossLists;
    for (BindingInfo const& b : bindings)
    {
        if (shown >= kMaxBindings)
            break;
        if (shown++)
            ss << ",";
        ss << "{\"commander\":\"" << JsonEscape(b.commander) << "\""
           << ",\"commander_guid\":" << b.commanderGuid
           << (b.boundAt > 0 ? ",\"bound_at\":" + std::to_string(b.boundAt) : std::string())
           << ",\"online\":" << (b.commanderOnline ? "true" : "false")
           << ",\"view\":" << (b.viewOn ? "true" : "false")
           << ",\"group\":" << b.groupId
           << ",\"lead\":\"" << JsonEscape(b.lead) << "\",\"bots\":[";
        size_t bots = 0;
        for (BotInfo const& bot : b.bots)
        {
            if (bots >= kMaxBotsPerBinding || botsTotal >= kMaxBotsPerPayload)
                break;
            if (bots++)
                ss << ",";
            ss << "{\"name\":\"" << JsonEscape(bot.name) << "\""
               << ",\"guid\":" << bot.guid
               << ",\"class\":\"" << JsonEscape(bot.className) << "\""
               << ",\"role\":\"" << JsonEscape(bot.role) << "\""
               << ",\"level\":" << bot.level
               << ",\"hp\":" << bot.hp
               << ",\"max_hp\":" << bot.maxHp
               << ",\"power\":" << bot.power
               << ",\"max_power\":" << bot.maxPower
               << ",\"power_type\":\"" << JsonEscape(bot.powerType) << "\""
               << ",\"alive\":" << (bot.alive ? "true" : "false")
               << ",\"lead\":" << (bot.lead ? "true" : "false")
               << ",\"map\":" << bot.mapId
               << ",\"zone\":" << bot.zoneId
               << ",\"x\":" << bot.x << ",\"y\":" << bot.y << ",\"z\":" << bot.z;
            if (bot.subgroup >= 0)
                ss << ",\"sub\":" << bot.subgroup;
            if (bot.away)
                ss << ",\"away\":true";
            if (bot.id)
                ss << ",\"id\":\"" << bot.id << "\"";
            if (bot.target)
                ss << ",\"target\":\"" << bot.target << "\"";
            if (bot.hasExtra)
            {
                BotExtra const& x = bot.extra;
                ss << ",\"st\":\"" << (x.state == 'c' || x.state == 'd' || x.state == 'r' ? x.state : 'n') << "\"";
                if (!x.action.empty())
                    ss << ",\"act\":\"" << JsonEscape(Clip(x.action, kMaxActionName)) << "\""
                       << ",\"res\":\"" << (x.result >= 'A' && x.result <= 'Z' ? x.result : '-') << "\"";
                if (!x.cast.empty())
                {
                    ss << ",\"cast\":\"" << JsonEscape(Clip(x.cast, kMaxCastName)) << "\"";
                    if (x.castTarget)
                        ss << ",\"ctgt\":\"" << x.castTarget << "\"";
                }
                if (x.cc == 's' || x.cc == 'c' || x.cc == 'f' || x.cc == 'r')
                    ss << ",\"cc\":\"" << x.cc << "\"";
                if (x.rest == 'd' || x.rest == 'e')
                    ss << ",\"rest\":\"" << x.rest << "\"";
                if (x.moving)
                    ss << ",\"mv\":true";
                if (!x.targetName.empty())
                    ss << ",\"tname\":\"" << JsonEscape(Clip(x.targetName, kMaxTargetName)) << "\"";
            }
            ++botsTotal;
            ss << "}";
        }
        ss << "]";
        if (b.run.present)
        {
            RunInfo const& r = b.run;
            ss << ",\"run\":{\"plan\":\"" << JsonEscape(Clip(r.plan, kMaxRunWord)) << "\""
               << ",\"state\":\"" << JsonEscape(Clip(r.state, kMaxRunWord)) << "\""
               << ",\"step\":" << r.step << ",\"steps\":" << r.steps
               << ",\"leg\":\"" << JsonEscape(Clip(r.leg, kMaxRunWord)) << "\""
               << ",\"kind\":\"" << JsonEscape(Clip(r.kind, kMaxRunWord)) << "\""
               << ",\"phase\":\"" << JsonEscape(Clip(r.phase, kMaxRunWord)) << "\""
               << ",\"label\":\"" << JsonEscape(Clip(r.label, kMaxRunNote)) << "\""
               << ",\"note\":\"" << JsonEscape(Clip(r.note, kMaxRunNote)) << "\""
               << ",\"changed\":" << r.changedAt;
            if (!r.notes.empty())
            {
                ss << ",\"notes\":[";
                size_t kept = r.notes.size() > kMaxRunNotes ? r.notes.size() - kMaxRunNotes : 0;
                bool first = true;
                for (size_t i = kept; i < r.notes.size(); ++i)
                {
                    RunNoteInfo const& n = r.notes[i];
                    if (!first)
                        ss << ",";
                    first = false;
                    ss << "{\"at\":" << n.at << ",\"step\":" << n.step
                       << ",\"leg\":\"" << JsonEscape(Clip(n.leg, kMaxRunWord)) << "\""
                       << ",\"kind\":\"" << JsonEscape(Clip(n.kind, kMaxRunWord)) << "\""
                       << ",\"phase\":\"" << JsonEscape(Clip(n.phase, kMaxRunWord)) << "\""
                       << ",\"state\":\"" << JsonEscape(Clip(n.state, kMaxRunWord)) << "\""
                       << ",\"note\":\"" << JsonEscape(Clip(n.note, kMaxRunNote)) << "\"}";
                }
                ss << "]";
            }
            if (!r.stepList.empty())
            {
                ss << ",\"plan_steps\":[";
                size_t listed = 0;
                for (RunStepInfo const& st : r.stepList)
                {
                    if (listed >= kMaxRunSteps || stepsTotal >= kMaxRunStepsPerPayload)
                        break;
                    if (listed++)
                        ss << ",";
                    ++stepsTotal;
                    ss << "{\"i\":" << st.index << ",\"k\":\"" << JsonEscape(Clip(st.kind, kMaxRunWord)) << "\""
                       << ",\"x\":" << st.x << ",\"y\":" << st.y;
                    if (st.radius > 0.0f)
                        ss << ",\"r\":" << st.radius;
                    if (st.entry)
                        ss << ",\"e\":" << st.entry;
                    if (!st.name.empty())
                        ss << ",\"n\":\"" << JsonEscape(Clip(st.name, kMaxMobName)) << "\"";
                    if (!st.label.empty())
                        ss << ",\"c\":\"" << JsonEscape(Clip(st.label, kMaxRunNote)) << "\"";
                    if (st.skipped)
                        ss << ",\"sk\":true";
                    ss << "}";
                }
                ss << "]";
            }
            ss << "}";
        }
        if (b.layout.present)
        {
            ss << ",\"layout\":{\"mt\":\"" << JsonEscape(b.layout.mainTank) << "\",\"ot\":[";
            size_t listed = 0;
            for (std::string const& name : b.layout.offTanks)
            {
                if (listed++)
                    ss << ",";
                ss << "\"" << JsonEscape(name) << "\"";
            }
            ss << "],\"th\":[";
            listed = 0;
            for (auto const& pair : b.layout.tankHealers)
            {
                if (listed++)
                    ss << ",";
                ss << "{\"h\":\"" << JsonEscape(pair.first) << "\",\"t\":\"" << JsonEscape(pair.second) << "\"}";
            }
            ss << "],\"rh\":[";
            listed = 0;
            for (std::string const& name : b.layout.raidHealers)
            {
                if (listed++)
                    ss << ",";
                ss << "\"" << JsonEscape(name) << "\"";
            }
            ss << "]}";
        }
        if (b.commanderPlaced)
            ss << ",\"commander_pos\":{\"map\":" << b.commanderMap << ",\"x\":" << b.commanderX << ",\"y\":" << b.commanderY << "}";
        if (b.mobsReported)
        {
            ss << ",\"mobs_map\":" << b.mobsMap << ",\"mobs\":[";
            size_t mobs = 0;
            for (MobInfo const& m : b.mobs)
            {
                if (mobs >= kMaxMobsPerBinding || mobsTotal >= kMaxMobsPerPayload)
                    break;
                if (mobs++)
                    ss << ",";
                ++mobsTotal;
                AppendMobJson(ss, m);
            }
            ss << "]";
        }
        if (b.objectsReported)
        {
            ss << ",\"objects\":[";
            size_t objs = 0;
            for (ObjectInfo const& o : b.objects)
            {
                if (objs >= kMaxObjectsPerBinding || objectsTotal >= kMaxObjectsPerPayload)
                    break;
                if (objs++)
                    ss << ",";
                ++objectsTotal;
                ss << "{\"id\":\"" << o.id << "\",\"entry\":" << o.entry
                   << ",\"name\":\"" << JsonEscape(Clip(o.name, kMaxObjectName)) << "\""
                   << ",\"x\":" << o.x << ",\"y\":" << o.y
                   << ",\"type\":\"" << (std::string("dcbgro").find(o.type) != std::string::npos ? o.type : 'o') << "\""
                   << ",\"state\":\"" << (o.state == 'a' || o.state == 'u' ? o.state : 'r') << "\"}";
            }
            ss << "]";
        }
        // A roster longer than kMaxBossesPerBinding is left out whole, never sent short: a short list reads as "these are all the
        // bosses". Which lists go in (the per-datagram count and the byte budget) is decided below, once every binding is written.
        if (b.bossesReported && b.bosses.size() <= kMaxBossesPerBinding)
        {
            std::ostringstream bs;
            bs.setf(std::ios::fixed);
            bs.precision(1);
            bs << ",\"bosses_map\":" << b.bossesMap << ",\"bosses\":[";
            size_t listed = 0;
            for (BossInfo const& boss : b.bosses)
            {
                if (listed++)
                    bs << ",";
                bs << "{\"e\":" << boss.entry << ",\"n\":\"" << JsonEscape(Clip(boss.name, kMaxBossName)) << "\""
                   << ",\"x\":" << boss.x << ",\"y\":" << boss.y
                   << ",\"s\":\"" << (boss.state == 'a' || boss.state == 'd' ? boss.state : 'u') << "\"";
                if (boss.inPlan)
                    bs << ",\"p\":1";
                bs << "}";
            }
            bs << "]";
            bossLists.push_back(BossList{static_cast<size_t>(ss.tellp()), bs.str(), listed});
        }
        if (!b.combatJson.empty())
            ss << ",\"combat\":" << b.combatJson;
        ss << ",\"orders\":[";
        size_t orders = 0;
        for (OrderRecord const& o : b.orders)
        {
            if (orders >= kMaxOrdersPerCommander)
                break;
            if (orders++)
                ss << ",";
            ss << "{\"at\":" << o.at
               << ",\"verb\":\"" << JsonEscape(o.verb) << "\""
               << ",\"detail\":\"" << JsonEscape(o.detail) << "\""
               << ",\"ok\":" << (o.ok ? "true" : "false")
               << ",\"result\":\"" << JsonEscape(o.result) << "\"}";
        }
        ss << "]}";
    }
    ss << "]}";
    std::string out = ss.str();
    // The lists that fit both the per-datagram count and the byte budget; only a list that goes in counts. The first list tried moves on
    // with the snapshot sequence (Codex review 2026-10-22: in a fixed order a binding late in the list could lose its roster every time),
    // so over as many snapshots as there are lists each one is tried first once and every roster that fits alone is sent whole. Inserted
    // from the back so the earlier positions stay valid.
    size_t total = out.size();
    size_t bosses = 0;
    std::vector<bool> keep(bossLists.size(), false);
    size_t const start = bossLists.empty() ? 0 : static_cast<size_t>(seq % bossLists.size());
    for (size_t n = 0; n < bossLists.size(); ++n)
    {
        size_t const i = (start + n) % bossLists.size();
        if (bosses + bossLists[i].count <= kMaxBossesPerPayload && total + bossLists[i].json.size() <= kPayloadByteBudget)
        {
            keep[i] = true;
            total += bossLists[i].json.size();
            bosses += bossLists[i].count;
        }
    }
    for (size_t i = bossLists.size(); i-- > 0;)
        if (keep[i])
            out.insert(bossLists[i].at, bossLists[i].json);
    return out;
}

} // namespace CommanderTelemetry
} // namespace TortoiseBots
