#pragma once
// Per-fight combat numbers of one bound group, fed by the core's damage,
// heal, cast and death hooks and a 1 Hz aura sample; the numbers only,
// the dashboard judges them; plain data and the standard library only.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace TortoiseBots {
namespace CombatMonitor {

constexpr uint32_t kMinFightMs = 5000;        // shorter fights are dropped
constexpr uint32_t kOutOfCombatEndMs = 3000;  // a fight ends this long after the last member left combat
constexpr uint32_t kWindowMs = 10000;         // the rolling window
constexpr uint32_t kMaxDeathsPerFight = 200;  // death recaps kept per fight, oldest dropped past it
constexpr uint32_t kMaxEngagedPerFight = 500; // distinct engaged creature guids tracked per fight (all entries together)
constexpr size_t kMaxEnemyDeathsPerFight = kMaxEngagedPerFight; // enemy entries remembered for the sampler's later boss pick
constexpr size_t kMaxDroppedTracked = 4096;   // distinct guids past that cap counted as dropped; the count stops here
constexpr size_t kLastHits = 5;               // death recap
constexpr size_t kMaxSpellsPerBot = 12;
constexpr size_t kMaxFightsKept = 5;          // finished fights waiting to be sent, per book
constexpr size_t kPayloadByteBudget = 60000;
constexpr size_t kMaxLiveEvents = 64;          // queued per book, oldest dropped (counted)
constexpr size_t kMaxEventsPerDatagram = 32;
constexpr size_t kEventsByteBudget = 16000;
constexpr size_t kEventTextBytes = 256;   // last-resort clip of every event and header text
constexpr uint32_t kEventKeepaliveMs = 5000;
constexpr size_t kSeqDigitsMax = 20;   // UINT64_MAX in decimal
// The truncated (last-resort) payload bounds every variable-length field, so its
// size has a fixed ceiling well under the budget: about 2.5 KB of header (five
// texts of 48 bytes, 64 planned entries) plus about 0.5 KB per bot row (names and
// classes of 24 bytes, no spells), 40 rows at most.
constexpr size_t kTruncatedTextBytes = 48;    // commander, plan, leg, kind, boss name (raw bytes before escaping)
constexpr size_t kTruncatedNameBytes = 24;    // a bot's name and class
constexpr size_t kTruncatedPlanned = 64;      // planned entries
constexpr size_t kTruncatedBots = 40;         // bot rows

// Spell names always listed when cast (the daemon's rules read them).
inline bool AlwaysListed(std::string const& spell)
{
    return spell == "Sunder Armor" || spell == "Maul" || spell == "Swipe" || spell == "Taunt" ||
           spell == "Revenge" || spell == "Shield Block" || spell == "Righteous Fury" ||
           spell == "Auto Shot" || spell == "Hunter's Mark";
}

// Debuff keys of the raid recon (L15), by the aura's spell name and stacks; empty when not tracked.
inline std::vector<std::string> DebuffKeys(std::string const& spellName, uint32_t stacks)
{
    if (spellName == "Sunder Armor")
    {
        if (stacks >= 5)
            return {"sunder", "sunder5"};
        return {"sunder"};
    }
    if (spellName == "Faerie Fire" || spellName == "Faerie Fire (Feral)")
        return {"faerie_fire"};
    if (spellName == "Curse of Recklessness")
        return {"cor"};
    if (spellName == "Curse of the Elements")
        return {"coe"};
    if (spellName == "Curse of Shadow")
        return {"cos"};
    if (spellName == "Demoralizing Shout" || spellName == "Demoralizing Roar")
        return {"demo"};
    if (spellName == "Thunder Clap")
        return {"thunder_clap"};
    if (spellName == "Expose Armor")
        return {"expose_armor"};
    return {};
}

enum class Role : uint8_t { Tank, Healer, Damage };
enum class Outcome : uint8_t { Kill, Wipe, Unfinished, End };

inline char const* OutcomeName(Outcome o)    // "kill", "wipe", "unfinished", "end"
{
    switch (o)
    {
    case Outcome::Kill:
        return "kill";
    case Outcome::Wipe:
        return "wipe";
    case Outcome::Unfinished:
        return "unfinished";
    case Outcome::End:
    default:
        return "end";
    }
}

inline char const* RoleName(Role r)
{
    switch (r)
    {
    case Role::Tank:
        return "tank";
    case Role::Healer:
        return "healer";
    case Role::Damage:
    default:
        return "dps";
    }
}

// One line of the live fight stream (COMBAT_EVENTS): what happened at atMs of
// the fight, before the service cuts the queue into datagrams.
enum class EventKind : uint8_t { Start, Engage, Death, Taunt, Interrupt, Cast, End };

struct LiveEvent
{
    EventKind kind = EventKind::Start;
    uint32_t atMs = 0;   // world tick time of the event (End: the fight's end)
    uint32_t guid = 0;   // the bot (death, taunt, interrupt)
    std::string name, by, spell, targetName;
    uint64_t target = 0;   // taunt/interrupt target raw guid, or the casting unit's guid
    uint32_t entry = 0, count = 0;
    char rank = 0;         // cast: 'b' or 'e'
    float x = 0, y = 0;
    Outcome outcome = Outcome::End;
    uint32_t durMs = 0;
    bool kept = false;
};

struct MemberInfo
{
    uint32_t guid = 0;
    std::string name, cls;
    Role role = Role::Damage;
    int subgroup = -1;
};

struct Hit
{
    std::string attacker;
    uint32_t amount = 0;
    uint32_t atMs = 0;   // since fight start
};

struct Death
{
    std::string name;
    uint32_t atMs = 0;   // since fight start
    float x = 0, y = 0, z = 0;
    std::string killer;
    std::vector<Hit> hits;   // last hits taken, newest last
};

struct BotTotals
{
    uint64_t damage = 0, healing = 0, overheal = 0, taken = 0;
    uint32_t casts = 0, healCasts = 0, deaths = 0;
    std::map<std::string, uint32_t> spells;
};

struct FightContext
{
    std::string plan, leg, kind;
    uint32_t step = 0;
    uint32_t bossEntry = 0;
    std::string bossName;
    std::vector<uint32_t> planned;   // the run's step at fight start (empty plan: no run)
};

inline std::string JsonEscape(std::string const& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20)
            {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            }
            else
                out += static_cast<char>(c);
        }
    }
    return out;
}

// At most maxRaw bytes of s, cut on a UTF-8 character boundary, then escaped.
inline std::string BoundedJson(std::string const& s, size_t maxRaw)
{
    if (s.size() <= maxRaw)
        return JsonEscape(s);
    size_t cut = maxRaw;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80)
        --cut;
    return JsonEscape(s.substr(0, cut));
}

inline std::string F1(float v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(v));
    return buf;
}

// The fight a COMBAT_EVENTS datagram belongs to; the datagram sends "fight":null
// when its last event is the fight's end, and a fight header when it carries
// events or nothing went out yet while a fight is open (inFight).
struct EventsFightHeader
{
    bool inFight = false;
    uint32_t number = 0;
    uint32_t startMs = 0;   // world tick time the fight started
    int64_t startUnix = 0;
    uint32_t fightMs = 0;   // nowMs - startMs
    FightContext ctx;
};

// One COMBAT_EVENTS datagram (protocol v12, "fmt":1). At most kMaxEventsPerDatagram
// events go out; past kEventsByteBudget the count is halved until it fits, and a
// single event still too big is sent with every text clipped to kEventTextBytes,
// so one huge name can never cross the datagram budget or push an event out.
// *emitted reports how many of the handed events were serialized (the rest stay
// the caller's to requeue); the fight header follows exactly that prefix. Every
// event's `t` is its offset from the fight start (the end event carries its
// duration).
inline std::string BuildEventsJson(int protocolVersion, uint64_t session, uint64_t seq, int64_t ts, int64_t tsMs,
                                   std::string const& commander, uint32_t commanderGuid, uint32_t groupId,
                                   uint32_t mapId, EventsFightHeader const& fight,
                                   std::vector<LiveEvent> const& events, uint32_t dropped,
                                   size_t* emitted = nullptr)
{
    size_t count = std::min(events.size(), kMaxEventsPerDatagram);
    bool clipped = false;
    for (;;)
    {
        auto text = [clipped](std::string const& s) {
            return clipped ? BoundedJson(s, kEventTextBytes) : JsonEscape(s);
        };
        bool const present = count == 0 ? fight.inFight : events[count - 1].kind != EventKind::End;
        std::ostringstream ss;
        ss.setf(std::ios::fixed);
        ss.precision(1);
        ss << "{\"v\":" << protocolVersion << ",\"fmt\":1,\"session\":" << session << ",\"seq\":" << seq
           << ",\"ts\":" << ts << ",\"ts_ms\":" << tsMs << ",\"type\":\"COMBAT_EVENTS\",\"commander\":\""
           << text(commander) << "\",\"commander_guid\":" << commanderGuid << ",\"group\":" << groupId
           << ",\"map\":" << mapId << ",\"fight\":";
        if (present)
        {
            int64_t const startMsUnix = tsMs > 0 ? tsMs - static_cast<int64_t>(fight.fightMs) : fight.startUnix * 1000;
            ss << "{\"id\":\"" << groupId << "-" << fight.number << "\",\"start\":" << fight.startUnix
               << ",\"start_ms\":" << startMsUnix << ",\"plan\":\"" << text(fight.ctx.plan) << "\",\"leg\":\""
               << text(fight.ctx.leg) << "\",\"step\":" << fight.ctx.step << ",\"kind\":\""
               << text(fight.ctx.kind) << "\",\"boss\":" << fight.ctx.bossEntry << ",\"boss_name\":\""
               << text(fight.ctx.bossName) << "\",\"fight_ms\":" << fight.fightMs << "}";
        }
        else
            ss << "null";
        ss << ",\"dropped\":" << dropped << ",\"events\":[";
        for (size_t i = 0; i < count; ++i)
        {
            LiveEvent const& e = events[i];
            uint32_t const t = e.kind == EventKind::End
                ? e.durMs
                : (e.atMs >= fight.startMs ? e.atMs - fight.startMs : 0);
            if (i)
                ss << ",";
            switch (e.kind)
            {
            case EventKind::Start:
                ss << "{\"k\":\"start\",\"t\":0}";
                break;
            case EventKind::Engage:
                ss << "{\"k\":\"engage\",\"t\":" << t << ",\"e\":" << e.entry << ",\"n\":\"" << text(e.name)
                   << "\",\"c\":" << e.count << "}";
                break;
            case EventKind::Death:
                ss << "{\"k\":\"death\",\"t\":" << t << ",\"g\":" << e.guid << ",\"n\":\"" << text(e.name)
                   << "\",\"by\":\"" << text(e.by) << "\",\"x\":" << F1(e.x) << ",\"y\":" << F1(e.y) << "}";
                break;
            case EventKind::Taunt:
            case EventKind::Interrupt:
                ss << "{\"k\":\"" << (e.kind == EventKind::Taunt ? "taunt" : "interrupt") << "\",\"t\":" << t
                   << ",\"g\":" << e.guid << ",\"n\":\"" << text(e.name) << "\",\"s\":\"" << text(e.spell)
                   << "\"";
                if (e.target)
                    ss << ",\"tg\":\"" << e.target << "\",\"tn\":\"" << text(e.targetName) << "\"";
                ss << "}";
                break;
            case EventKind::Cast:
                ss << "{\"k\":\"cast\",\"t\":" << t << ",\"id\":\"" << e.target << "\",\"e\":" << e.entry
                   << ",\"n\":\"" << text(e.name) << "\",\"s\":\"" << text(e.spell) << "\",\"r\":\""
                   << (e.rank == 'e' ? 'e' : 'b') << "\"}";
                break;
            case EventKind::End:
                ss << "{\"k\":\"end\",\"t\":" << t << ",\"o\":\"" << OutcomeName(e.outcome) << "\",\"dur\":" << e.durMs
                   << ",\"kept\":" << (e.kept ? "true" : "false") << "}";
                break;
            }
        }
        ss << "]}";
        std::string out = ss.str();
        if (out.size() <= kEventsByteBudget || (count == 0 && clipped))
        {
            if (emitted)
                *emitted = count;
            return out;
        }
        if (count > 1)
            count /= 2;
        else if (!clipped)
            clipped = true;
        else if (count == 1)
            count = 0;
    }
}

// Spells for one bot line: every AlwaysListed spell cast, then the rest by
// count (ties by name), up to maxN.
inline std::vector<std::pair<std::string, uint32_t>> SpellList(BotTotals const& t, size_t maxN)
{
    std::vector<std::pair<std::string, uint32_t>> always, rest;
    for (auto const& kv : t.spells)
    {
        if (AlwaysListed(kv.first))
            always.push_back(kv);
        else
            rest.push_back(kv);
    }
    auto byCount = [](std::pair<std::string, uint32_t> const& a, std::pair<std::string, uint32_t> const& b) {
        if (a.second != b.second)
            return a.second > b.second;
        return a.first < b.first;
    };
    std::sort(always.begin(), always.end(), byCount);
    std::sort(rest.begin(), rest.end(), byCount);
    always.insert(always.end(), rest.begin(), rest.end());
    if (always.size() > maxN)
        always.resize(maxN);
    return always;
}

inline void PruneBefore(std::vector<std::pair<uint32_t, uint32_t>>& events, uint32_t nowMs)
{
    int64_t const cut = static_cast<int64_t>(nowMs) - static_cast<int64_t>(kWindowMs);
    while (!events.empty() && (static_cast<int64_t>(events.front().first) <= cut || events.front().first > nowMs))
        events.erase(events.begin());
}

inline void PruneBefore(std::vector<uint32_t>& times, uint32_t nowMs)
{
    int64_t const cut = static_cast<int64_t>(nowMs) - static_cast<int64_t>(kWindowMs);
    while (!times.empty() && (static_cast<int64_t>(times.front()) <= cut || times.front() > nowMs))
        times.erase(times.begin());
}

// One engaged creature entry of the open fight, as the world thread reads it to
// pick the fight's boss.
struct EngagedUnit
{
    uint32_t entry = 0;
    uint64_t guid = 0;
    std::string name;
};

class Book   // one per bound group
{
public:
    void SetMembers(std::vector<MemberInfo> members)   // replaces the member table
    {
        std::unordered_map<uint32_t, MemberState> old;
        for (auto& m : order_)
            old[m.info.guid] = m;
        order_.clear();
        index_.clear();
        for (auto const& mi : members)
        {
            auto it = old.find(mi.guid);
            if (it != old.end())
            {
                it->second.info = mi;
                order_.push_back(it->second);
            }
            else
            {
                MemberState st;
                st.info = mi;
                order_.push_back(st);
            }
        }
        for (size_t i = 0; i < order_.size(); ++i)
            index_[order_[i].info.guid] = i;
        for (auto it = dead_.begin(); it != dead_.end();)
        {
            if (index_.find(*it) == index_.end())
                it = dead_.erase(it);
            else
                ++it;
        }
    }

    bool IsMember(uint32_t guid) const { return index_.find(guid) != index_.end(); }
    bool InFight() const { return inFight_; }

    void Start(uint32_t nowMs, int64_t unixNow, FightContext ctx, uint32_t number)   // no-op while in a fight
    {
        if (inFight_ || order_.empty())
            return;
        inFight_ = true;
        startMs_ = nowMs;
        startUnix_ = unixNow;
        ctx_ = ctx;
        bossDied_ = false;
        enemyDeaths_.clear();
        outOfCombat_ = false;
        outOfCombatSince_ = 0;
        engaged_.clear();
        engagedGuids_ = 0;
        droppedGuids_.clear();
        engagedDropped_ = 0;
        debuffUp_.clear();
        debuffSampled_ = 0;
        deaths_.clear();
        dead_.clear();
        for (auto& m : order_)
        {
            m.totals = BotTotals{};
            m.dmgDone.clear();
            m.heals.clear();
            m.castTimes.clear();
            m.lastSpell.clear();
            m.lastSpellMs = 0;
            m.recentHits.clear();
        }
        fightNumber_ = number;
        LiveEvent start;
        start.kind = EventKind::Start;
        start.atMs = startMs_;
        Push(std::move(start));
    }

    void OnCast(uint32_t caster, std::string const& spell, bool heal, uint32_t nowMs, char castKind = 0,
                uint64_t target = 0, std::string const& targetName = std::string())
    {
        if (!inFight_ || !IsMember(caster))
            return;
        MemberState& m = At(caster);
        ++m.totals.casts;
        if (heal)
            ++m.totals.healCasts;
        ++m.totals.spells[spell];
        m.castTimes.push_back(nowMs);
        PruneBefore(m.castTimes, nowMs);
        m.lastSpell = spell;
        m.lastSpellMs = nowMs;
        if (castKind == 't' || castKind == 'i')
        {
            LiveEvent ev;
            ev.kind = castKind == 't' ? EventKind::Taunt : EventKind::Interrupt;
            ev.atMs = nowMs;
            ev.guid = caster;
            ev.name = m.info.name;
            ev.spell = spell;
            ev.target = target;
            ev.targetName = targetName;
            Push(std::move(ev));
        }
    }

    void OnDamage(uint32_t attackerMember, uint32_t victimMember, uint32_t amount,
                  std::string const& attackerName, uint32_t enemyEntry, uint64_t enemyGuid,
                  std::string const& enemyName, uint32_t nowMs)
    {
        // attackerMember/victimMember: 0 when that side is not a member (pets are mapped to their owner by the caller)
        if (!inFight_)
            return;
        bool const atk = attackerMember != 0 && IsMember(attackerMember);
        bool const vic = victimMember != 0 && IsMember(victimMember);
        if (atk && !vic)
        {
            MemberState& m = At(attackerMember);
            m.totals.damage += amount;
            m.dmgDone.emplace_back(nowMs, amount);
            PruneBefore(m.dmgDone, nowMs);
            AddEngaged(enemyEntry, enemyGuid, enemyName, nowMs);
        }
        else if (!atk && vic)
        {
            MemberState& m = At(victimMember);
            m.totals.taken += amount;
            uint32_t at = nowMs >= startMs_ ? nowMs - startMs_ : 0;
            m.recentHits.push_back(Hit{attackerName, amount, at});
            while (m.recentHits.size() > kLastHits)
                m.recentHits.pop_front();
            AddEngaged(enemyEntry, enemyGuid, enemyName, nowMs);
        }
        // Member-on-member damage (charm, Mind Control) and pairs with no
        // member on either side are not counted.
    }

    void OnHeal(uint32_t healerMember, uint32_t amount, uint32_t overheal, uint32_t nowMs)
    {
        if (!inFight_ || !IsMember(healerMember))
            return;
        MemberState& m = At(healerMember);
        m.totals.healing += amount;
        m.totals.overheal += overheal;
        m.heals.emplace_back(nowMs, amount);
        PruneBefore(m.heals, nowMs);
    }

    void OnDeath(uint32_t member, std::string const& killer, float x, float y, float z, uint32_t nowMs)
    {
        if (!inFight_ || member == 0 || !IsMember(member))
            return;
        MemberState& m = At(member);
        ++m.totals.deaths;
        Death d;
        d.name = m.info.name;
        d.atMs = nowMs >= startMs_ ? nowMs - startMs_ : 0;
        d.x = x;
        d.y = y;
        d.z = z;
        d.killer = killer;
        d.hits.assign(m.recentHits.begin(), m.recentHits.end());
        deaths_.push_back(d);
        while (deaths_.size() > kMaxDeathsPerFight)
            deaths_.pop_front();
        dead_.insert(member);
        LiveEvent ev;
        ev.kind = EventKind::Death;
        ev.atMs = nowMs;
        ev.guid = member;
        ev.name = m.info.name;
        ev.by = killer;
        ev.x = x;
        ev.y = y;
        Push(std::move(ev));
        if (dead_.size() >= order_.size())
            EndFight(nowMs, true);
    }

    void SampleDebuffs(std::vector<std::string> const& keysUp)   // 1 Hz, in a fight; counts a sampled second
    {
        if (!inFight_)
            return;
        ++debuffSampled_;
        for (auto const& k : keysUp)
            ++debuffUp_[k];
    }

    void SetInCombat(bool anyMemberInCombat, bool allDead, uint32_t nowMs)   // 1 Hz; ends the fight
    {
        if (!inFight_)
            return;
        if (allDead)
        {
            EndFight(nowMs, true);
            return;
        }
        if (anyMemberInCombat)
        {
            outOfCombat_ = false;
            return;
        }
        if (!outOfCombat_)
        {
            outOfCombat_ = true;
            outOfCombatSince_ = nowMs;
            return;
        }
        if (nowMs >= outOfCombatSince_ && nowMs - outOfCombatSince_ >= kOutOfCombatEndMs)
            EndFight(outOfCombatSince_, false);
    }

    void NoteBossDied(uint32_t entry)   // an enemy of the open fight died
    {
        if (!inFight_ || entry == 0)
            return;
        if (entry == ctx_.bossEntry)
        {
            bossDied_ = true;
            return;
        }
        // The sampler picks the boss after the tick's Refresh, so a boss can die
        // before any pick: the entry is kept for the pick that comes later.
        if (enemyDeaths_.size() < kMaxEnemyDeathsPerFight)
            enemyDeaths_.insert(entry);
    }

    // The sampler's boss: set once per fight, and never over a run's own boss.
    void NoteBoss(uint32_t entry, std::string const& name)
    {
        if (!inFight_ || entry == 0 || ctx_.bossEntry != 0)
            return;
        ctx_.bossEntry = entry;
        ctx_.bossName = name;
        if (enemyDeaths_.count(entry))
            bossDied_ = true;
    }

    // The open fight's engaged creatures, one row per entry (its lowest guid and
    // the entry's name), in engaged_ order; empty outside a fight.
    std::vector<EngagedUnit> EngagedNow() const
    {
        std::vector<EngagedUnit> out;
        if (!inFight_)
            return out;
        out.reserve(engaged_.size());
        for (auto const& kv : engaged_)
        {
            if (kv.second.guids.empty())
                continue;
            out.push_back(EngagedUnit{kv.first, *std::min_element(kv.second.guids.begin(), kv.second.guids.end()),
                kv.second.name});
        }
        return out;
    }

    // A creature's cast start seen by the poller: only elite and boss units, only in a fight.
    void NoteEnemyCast(uint32_t entry, uint64_t guid, std::string const& name, std::string const& spell, char rank,
                       uint32_t nowMs)
    {
        if (!inFight_ || (rank != 'b' && rank != 'e'))
            return;
        LiveEvent ev;
        ev.kind = EventKind::Cast;
        ev.atMs = nowMs;
        ev.target = guid;
        ev.entry = entry;
        ev.name = name;
        ev.spell = spell;
        ev.rank = rank;
        Push(std::move(ev));
    }

    // The binding is gone: end the open fight (a wipe when every member is dead) instead of losing it.
    void Close(uint32_t nowMs)
    {
        if (inFight_)
            EndFight(nowMs, !order_.empty() && dead_.size() >= order_.size());
    }

    // The lead stayed unavailable past the sampler's grace: end the fight it
    // left open, unfinished unless it was a wipe or the boss already fell.
    void CloseUnfinished(uint32_t nowMs)
    {
        if (inFight_)
            EndFight(nowMs, false, true);
    }

    bool HasEvents() const { return !events_.empty() || !newEngaged_.empty(); }

    // The queued events, oldest first, at most kMaxEventsPerDatagram; the coalesced
    // engagements go out first. dropped = events the queue cap lost since the last take.
    void TakeEvents(std::vector<LiveEvent>& out, uint32_t& dropped)
    {
        for (auto const& kv : newEngaged_)
        {
            LiveEvent ev;
            ev.kind = EventKind::Engage;
            ev.atMs = engagedAtMs_;
            ev.entry = kv.first;
            ev.name = kv.second.first;
            ev.count = kv.second.second;
            Push(std::move(ev));
        }
        newEngaged_.clear();
        dropped = eventsDropped_;
        eventsDropped_ = 0;
        size_t const count = std::min(events_.size(), kMaxEventsPerDatagram);
        for (size_t i = 0; i < count; ++i)
        {
            out.push_back(std::move(events_.front()));
            events_.pop_front();
        }
    }

    // Events taken for a datagram but not serialized (the budget shrank the
    // count) go back to the front in order: the next datagram carries them
    // before anything newer, and nothing leaves the queue unrecorded.
    void RequeueEvents(std::vector<LiveEvent> const& taken, size_t from)
    {
        for (size_t i = taken.size(); i > from; --i)
            events_.push_front(taken[i - 1]);
    }

    uint32_t FightNumber() const { return fightNumber_; }
    uint32_t StartMs() const { return startMs_; }
    int64_t StartUnix() const { return startUnix_; }
    uint32_t LastFightMs() const { return lastDurMs_; }
    FightContext const& Context() const { return ctx_; }

    bool HasTakeablePayloads() const   // a finished fight still owed to the emitter
    {
        for (FinishedFight const& f : finished_)
            if (f.takes < 2)
                return true;
        return false;
    }

    // Ended fights not yet taken, oldest first; each fight is handed out twice, then dropped.
    // Each fight is built at seq 0 and shrunk until it fits the budget (fewer spells,
    // fewer recap hits, then the truncated form with every field bounded, then the
    // truncated form without bot rows); only a payload that is queued takes a seq
    // (nextSeq runs once per queued payload) and advances the fight's takes. A fight
    // that fits in no form is dropped (takes = 2) and its id goes to oversizeDropped.
    std::vector<std::string> TakeFightPayloads(int protocolVersion, uint64_t session,
                                               std::function<uint64_t()> const& nextSeq, int64_t ts,
                                               std::string const& commander, uint32_t commanderGuid,
                                               uint32_t groupId, uint32_t mapId,
                                               std::vector<std::string>* oversizeDropped = nullptr)
    {
        std::vector<std::string> out;
        for (auto& f : finished_)
        {
            if (f.takes >= 2)
                continue;
            auto build = [&](size_t maxSpells, size_t maxHits, bool truncated, size_t maxBots) {
                return BuildFightJson(f, protocolVersion, session, 0, ts, commander, commanderGuid, groupId, mapId,
                                      maxSpells, maxHits, truncated, maxBots);
            };
            // Measured with seq 0: keep room for the widest seq (20 digits) the
            // resequence puts in, so a queued payload never crosses the budget.
            size_t const fit = kPayloadByteBudget - kSeqDigitsMax;
            std::string js = build(kMaxSpellsPerBot, kLastHits, false, f.bots.size());
            if (js.size() > fit)
                js = build(3, kLastHits, false, f.bots.size());
            if (js.size() > fit)
                js = build(3, 2, false, f.bots.size());
            if (js.size() > fit)
                js = build(0, 0, true, kTruncatedBots);
            if (js.size() > fit)
                js = build(0, 0, true, 0);
            if (js.size() > fit)
            {
                f.takes = 2;   // no runaway payload crosses the wire, and no seq is spent on it
                if (oversizeDropped)
                    oversizeDropped->push_back(std::to_string(groupId) + "-" + std::to_string(f.number));
                continue;
            }
            out.push_back(Resequenced(std::move(js), nextSeq()));
            ++f.takes;
        }
        for (auto it = finished_.begin(); it != finished_.end();)
        {
            if (it->takes >= 2)
                it = finished_.erase(it);
            else
                ++it;
        }
        return out;
    }

    void PruneWindows(uint32_t nowMs)   // drop per-event window entries older than the window
    {
        if (!inFight_)
            return;
        for (auto& m : order_)
        {
            PruneBefore(m.dmgDone, nowMs);
            PruneBefore(m.heals, nowMs);
            PruneBefore(m.castTimes, nowMs);
        }
    }

    size_t WindowEventCount() const   // per-event window entries still stored (tests pin the prune)
    {
        size_t n = 0;
        for (auto const& m : order_)
            n += m.dmgDone.size() + m.heals.size() + m.castTimes.size();
        return n;
    }

    std::string WindowJson(uint32_t nowMs)   // the `combat` object of COMMANDERS, "" outside a fight
    {
        if (!inFight_)
            return "";
        PruneWindows(nowMs);
        int64_t const cut = static_cast<int64_t>(nowMs) - static_cast<int64_t>(kWindowMs);
        std::ostringstream ss;
        ss << "{\"win_s\":" << (kWindowMs / 1000)
           << ",\"fight_ms\":" << (nowMs >= startMs_ ? nowMs - startMs_ : 0) << ",\"bots\":[";
        bool first = true;
        for (auto const& m : order_)
        {
            uint64_t dmg = 0, heal = 0;
            uint32_t casts = 0;
            for (auto const& e : m.dmgDone)
            {
                if (static_cast<int64_t>(e.first) > cut && e.first <= nowMs)
                    dmg += e.second;
            }
            for (auto const& e : m.heals)
            {
                if (static_cast<int64_t>(e.first) > cut && e.first <= nowMs)
                    heal += e.second;
            }
            for (uint32_t t : m.castTimes)
            {
                if (static_cast<int64_t>(t) > cut && t <= nowMs)
                    ++casts;
            }
            if (dmg == 0 && heal == 0 && casts == 0)
                continue;
            if (!first)
                ss << ",";
            first = false;
            ss << "{\"g\":" << m.info.guid << ",\"dps\":" << (dmg * 1000 / kWindowMs)
               << ",\"hps\":" << (heal * 1000 / kWindowMs) << ",\"c\":" << casts << ",\"ls\":\""
               << JsonEscape(m.lastSpell) << "\"}";
        }
        ss << "]}";
        return ss.str();
    }

private:
    struct MemberState
    {
        MemberInfo info;
        BotTotals totals;
        std::vector<std::pair<uint32_t, uint32_t>> dmgDone;   // (nowMs, amount)
        std::vector<std::pair<uint32_t, uint32_t>> heals;     // (nowMs, amount)
        std::vector<uint32_t> castTimes;
        std::string lastSpell;
        uint32_t lastSpellMs = 0;
        std::deque<Hit> recentHits;   // hits taken, newest last
    };

    struct EngagedData
    {
        std::string name;
        std::unordered_set<uint64_t> guids;
    };

    struct EngagedEntry
    {
        uint32_t entry = 0;
        std::string name;
        uint32_t guids = 0;
    };

    struct FinishedFight
    {
        uint32_t number = 0;
        int64_t startUnix = 0;
        uint32_t durMs = 0;
        Outcome outcome = Outcome::End;
        FightContext ctx;
        std::vector<EngagedEntry> engaged;
        uint64_t dropped = 0;   // distinct engaged creature guids past the cap
        std::vector<MemberState> bots;
        uint64_t sampled = 0;
        std::map<std::string, uint64_t> up;
        std::deque<Death> deaths;
        uint32_t takes = 0;
    };

    static std::string Resequenced(std::string js, uint64_t seqNum)   // stamps the queued payload's seq over the built 0
    {
        std::string const tag = "\"seq\":";
        std::string::size_type pos = js.find(tag);
        if (pos == std::string::npos)
            return js;
        pos += tag.size();
        std::string::size_type end = pos;
        while (end < js.size() && js[end] >= '0' && js[end] <= '9')
            ++end;
        js.replace(pos, end - pos, std::to_string(seqNum));
        return js;
    }

    static std::string BuildFightJson(FinishedFight const& f, int protocolVersion, uint64_t session,
                                      uint64_t seqNum, int64_t ts, std::string const& commander,
                                      uint32_t commanderGuid, uint32_t groupId, uint32_t mapId,
                                      size_t maxSpells, size_t maxHits, bool truncated, size_t maxBots)
    {
        // truncated: every variable-length text is bounded and only the first maxBots bot rows go out
        auto text = [truncated](std::string const& v, size_t maxRaw) {
            return truncated ? BoundedJson(v, maxRaw) : JsonEscape(v);
        };
        std::ostringstream ss;
        ss << "{\"v\":" << protocolVersion << ",\"session\":" << session << ",\"seq\":" << seqNum
           << ",\"ts\":" << ts << ",\"type\":\"COMBAT_FIGHT\",\"fight\":{";
        ss << "\"id\":\"" << groupId << "-" << f.number << "\",\"commander\":\""
           << text(commander, kTruncatedTextBytes)
           << "\",\"commander_guid\":" << commanderGuid << ",\"group\":" << groupId << ",\"map\":" << mapId
           << ",\"plan\":\"" << text(f.ctx.plan, kTruncatedTextBytes) << "\",\"leg\":\""
           << text(f.ctx.leg, kTruncatedTextBytes)
           << "\",\"step\":" << f.ctx.step << ",\"kind\":\"" << text(f.ctx.kind, kTruncatedTextBytes)
           << "\",\"start\":" << f.startUnix << ",\"dur_ms\":" << f.durMs << ",\"outcome\":\""
           << OutcomeName(f.outcome) << "\",\"boss\":" << f.ctx.bossEntry << ",\"boss_name\":\""
           << text(f.ctx.bossName, kTruncatedTextBytes) << "\",\"planned\":[";
        size_t const planned = truncated ? std::min(f.ctx.planned.size(), kTruncatedPlanned) : f.ctx.planned.size();
        for (size_t i = 0; i < planned; ++i)
            ss << (i ? "," : "") << f.ctx.planned[i];
        ss << "],\"engaged\":[";
        if (!truncated)
            for (size_t i = 0; i < f.engaged.size(); ++i)
            {
                if (i)
                    ss << ",";
                ss << "{\"e\":" << f.engaged[i].entry << ",\"n\":\"" << JsonEscape(f.engaged[i].name)
                   << "\",\"c\":" << f.engaged[i].guids << "}";
            }
        ss << "],\"dropped\":" << f.dropped << ",\"bots\":[";
        for (size_t i = 0; i < f.bots.size() && i < maxBots; ++i)
        {
            MemberState const& m = f.bots[i];
            if (i)
                ss << ",";
            ss << "{\"name\":\"" << text(m.info.name, kTruncatedNameBytes) << "\",\"guid\":" << m.info.guid
               << ",\"class\":\"" << text(m.info.cls, kTruncatedNameBytes) << "\",\"role\":\"" << RoleName(m.info.role)
               << "\",\"sub\":" << m.info.subgroup << ",\"dmg\":" << m.totals.damage
               << ",\"heal\":" << m.totals.healing << ",\"over\":" << m.totals.overheal
               << ",\"taken\":" << m.totals.taken << ",\"casts\":" << m.totals.casts
               << ",\"heal_casts\":" << m.totals.healCasts << ",\"deaths\":" << m.totals.deaths
               << ",\"spells\":[";
            std::vector<std::pair<std::string, uint32_t>> spells = truncated
                ? std::vector<std::pair<std::string, uint32_t>>{} : SpellList(m.totals, maxSpells);
            for (size_t j = 0; j < spells.size(); ++j)
                ss << (j ? "," : "") << "{\"n\":\"" << JsonEscape(spells[j].first)
                   << "\",\"c\":" << spells[j].second << "}";
            ss << "]}";
        }
        ss << "],\"debuffs\":{\"sampled_s\":" << f.sampled << ",\"up_s\":{";
        {
            bool first = true;
            if (!truncated)
                for (auto const& kv : f.up)
                {
                    if (!first)
                        ss << ",";
                    first = false;
                    ss << "\"" << JsonEscape(kv.first) << "\":" << kv.second;
                }
        }
        ss << "}},\"deaths\":[";
        if (!truncated)
            for (size_t i = 0; i < f.deaths.size(); ++i)
            {
                Death const& d = f.deaths[i];
                if (i)
                    ss << ",";
                ss << "{\"name\":\"" << JsonEscape(d.name) << "\",\"t_ms\":" << d.atMs << ",\"x\":" << F1(d.x)
                   << ",\"y\":" << F1(d.y) << ",\"z\":" << F1(d.z) << ",\"killer\":\"" << JsonEscape(d.killer)
                   << "\",\"hits\":[";
                size_t skip = d.hits.size() > maxHits ? d.hits.size() - maxHits : 0;
                for (size_t j = skip; j < d.hits.size(); ++j)
                    ss << (j > skip ? "," : "") << "{\"a\":\"" << JsonEscape(d.hits[j].attacker)
                       << "\",\"d\":" << d.hits[j].amount << ",\"t_ms\":" << d.hits[j].atMs << "}";
                ss << "]}";
            }
        ss << "]}}";
        std::string out = ss.str();
        if (truncated)
        {
            std::string::size_type pos = out.rfind("}}");
            out.insert(pos, ",\"truncated\":true");
        }
        return out;
    }

    MemberState& At(uint32_t guid) { return order_[index_[guid]]; }

    void Push(LiveEvent ev)
    {
        events_.push_back(std::move(ev));
        while (events_.size() > kMaxLiveEvents)
        {
            events_.pop_front();
            ++eventsDropped_;
        }
    }

    void AddEngaged(uint32_t entry, uint64_t guid, std::string const& name, uint32_t nowMs)
    {
        if (guid == 0)
            return;
        auto it = engaged_.find(entry);
        if (it != engaged_.end() && it->second.guids.count(guid))
            return;
        if (engagedGuids_ >= kMaxEngagedPerFight)
        {
            // Past the cap: counted once per distinct guid, never inserted.
            if (droppedGuids_.size() < kMaxDroppedTracked && droppedGuids_.insert(guid).second)
                ++engagedDropped_;
            return;
        }
        EngagedData& e = it != engaged_.end() ? it->second : engaged_[entry];
        if (e.name.empty())
            e.name = name;
        e.guids.insert(guid);
        ++engagedGuids_;
        std::pair<std::string, uint32_t>& engaged = newEngaged_[entry];
        if (engaged.first.empty())
            engaged.first = e.name;
        ++engaged.second;
        engagedAtMs_ = nowMs;
    }

    void EndFight(uint32_t endMs, bool allDead, bool unfinished = false)
    {
        Outcome o = Outcome::End;
        if (allDead)
            o = Outcome::Wipe;
        else if (bossDied_ && ctx_.bossEntry != 0)
            o = Outcome::Kill;
        else if (unfinished)
            o = Outcome::Unfinished;
        uint32_t const dur = endMs >= startMs_ ? endMs - startMs_ : 0;
        lastDurMs_ = dur;
        LiveEvent ev;
        ev.kind = EventKind::End;
        ev.atMs = endMs;
        ev.outcome = o;
        ev.durMs = dur;
        ev.kept = dur >= kMinFightMs;
        Push(std::move(ev));
        if (dur >= kMinFightMs)
        {
            FinishedFight f;
            f.number = fightNumber_;
            f.startUnix = startUnix_;
            f.durMs = dur;
            f.outcome = o;
            f.ctx = ctx_;
            for (auto const& kv : engaged_)
                f.engaged.push_back(EngagedEntry{kv.first, kv.second.name,
                                                static_cast<uint32_t>(kv.second.guids.size())});
            f.dropped = engagedDropped_;
            f.bots = order_;
            f.sampled = debuffSampled_;
            f.up = debuffUp_;
            f.deaths = deaths_;
            finished_.push_back(f);
            while (finished_.size() > kMaxFightsKept)
                finished_.pop_front();
        }
        inFight_ = false;
        bossDied_ = false;
        enemyDeaths_.clear();
        outOfCombat_ = false;
        outOfCombatSince_ = 0;
    }

    std::vector<MemberState> order_;
    std::unordered_map<uint32_t, size_t> index_;
    bool inFight_ = false;
    uint32_t startMs_ = 0;
    int64_t startUnix_ = 0;
    FightContext ctx_;
    bool bossDied_ = false;
    std::unordered_set<uint32_t> enemyDeaths_;   // enemy entries that died before the sampler picked a boss
    bool outOfCombat_ = false;
    uint32_t outOfCombatSince_ = 0;
    std::map<uint32_t, EngagedData> engaged_;
    uint32_t engagedGuids_ = 0;     // distinct guids in engaged_, all entries together
    std::unordered_set<uint64_t> droppedGuids_;   // guids past the cap, at most kMaxDroppedTracked
    uint64_t engagedDropped_ = 0;   // distinct engaged creature guids past the cap (stops at kMaxDroppedTracked)
    std::map<std::string, uint64_t> debuffUp_;
    uint64_t debuffSampled_ = 0;
    std::deque<Death> deaths_;
    std::unordered_set<uint32_t> dead_;
    uint32_t fightNumber_ = 0;
    uint32_t lastDurMs_ = 0;
    std::deque<FinishedFight> finished_;
    std::deque<LiveEvent> events_;   // live fight stream, oldest first, capped at kMaxLiveEvents
    uint32_t eventsDropped_ = 0;
    std::map<uint32_t, std::pair<std::string, uint32_t>> newEngaged_;   // entry -> (name, new distinct guids)
    uint32_t engagedAtMs_ = 0;
};

}  // namespace CombatMonitor
}  // namespace TortoiseBots
