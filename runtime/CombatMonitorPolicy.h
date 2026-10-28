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
constexpr uint32_t kMaxEngagedPerFight = 500; // engaged creatures tracked per fight, counted past it as dropped
constexpr size_t kLastHits = 5;               // death recap
constexpr size_t kMaxSpellsPerBot = 12;
constexpr size_t kMaxFightsKept = 5;          // finished fights waiting to be sent, per book
constexpr size_t kPayloadByteBudget = 60000;

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
enum class Outcome : uint8_t { Kill, Wipe, End };

inline char const* OutcomeName(Outcome o)    // "kill", "wipe", "end"
{
    switch (o)
    {
    case Outcome::Kill:
        return "kill";
    case Outcome::Wipe:
        return "wipe";
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

inline std::string F1(float v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(v));
    return buf;
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
        outOfCombat_ = false;
        outOfCombatSince_ = 0;
        engaged_.clear();
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
    }

    void OnCast(uint32_t caster, std::string const& spell, bool heal, uint32_t nowMs)
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
            AddEngaged(enemyEntry, enemyGuid, enemyName);
        }
        else if (!atk && vic)
        {
            MemberState& m = At(victimMember);
            m.totals.taken += amount;
            uint32_t at = nowMs >= startMs_ ? nowMs - startMs_ : 0;
            m.recentHits.push_back(Hit{attackerName, amount, at});
            while (m.recentHits.size() > kLastHits)
                m.recentHits.pop_front();
            AddEngaged(enemyEntry, enemyGuid, enemyName);
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

    void NoteBossDied(uint32_t entry)   // the context's boss died inside the fight
    {
        if (!inFight_ || entry == 0 || entry != ctx_.bossEntry)
            return;
        bossDied_ = true;
    }

    bool HasTakeablePayloads() const   // a finished fight still owed to the emitter
    {
        for (FinishedFight const& f : finished_)
            if (f.takes < 2)
                return true;
        return false;
    }

    // Ended fights not yet taken, oldest first; each fight is handed out twice, then dropped.
    // nextSeq runs once per payload, only when a payload goes out. Every queued payload fits the budget.
    std::vector<std::string> TakeFightPayloads(int protocolVersion, uint64_t session,
                                               std::function<uint64_t()> const& nextSeq, int64_t ts,
                                               std::string const& commander, uint32_t commanderGuid,
                                               uint32_t groupId, uint32_t mapId)
    {
        std::vector<std::string> out;
        for (auto& f : finished_)
        {
            if (f.takes >= 2)
                continue;
            std::string js = BuildFightJson(f, protocolVersion, session, nextSeq(), ts, commander, commanderGuid,
                                            groupId, mapId, kMaxSpellsPerBot, kLastHits, false);
            if (js.size() > kPayloadByteBudget)
                js = BuildFightJson(f, protocolVersion, session, nextSeq(), ts, commander, commanderGuid, groupId,
                                    mapId, 3, kLastHits, false);
            if (js.size() > kPayloadByteBudget)
                js = BuildFightJson(f, protocolVersion, session, nextSeq(), ts, commander, commanderGuid, groupId,
                                    mapId, 3, 2, false);
            if (js.size() > kPayloadByteBudget)
                js = BuildFightJson(f, protocolVersion, session, nextSeq(), ts, commander, commanderGuid, groupId,
                                    mapId, 0, 0, true);
            if (js.size() > kPayloadByteBudget)
                continue;   // no runaway payload crosses the wire
            out.push_back(js);
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
        uint64_t dropped = 0;   // engaged creatures past the cap
        std::vector<MemberState> bots;
        uint64_t sampled = 0;
        std::map<std::string, uint64_t> up;
        std::deque<Death> deaths;
        uint32_t takes = 0;
    };

    static std::string BuildFightJson(FinishedFight const& f, int protocolVersion, uint64_t session,
                                      uint64_t seqNum, int64_t ts, std::string const& commander,
                                      uint32_t commanderGuid, uint32_t groupId, uint32_t mapId,
                                      size_t maxSpells, size_t maxHits, bool truncated)
    {
        std::ostringstream ss;
        ss << "{\"v\":" << protocolVersion << ",\"session\":" << session << ",\"seq\":" << seqNum
           << ",\"ts\":" << ts << ",\"type\":\"COMBAT_FIGHT\",\"fight\":{";
        ss << "\"id\":\"" << groupId << "-" << f.number << "\",\"commander\":\"" << JsonEscape(commander)
           << "\",\"commander_guid\":" << commanderGuid << ",\"group\":" << groupId << ",\"map\":" << mapId
           << ",\"plan\":\"" << JsonEscape(f.ctx.plan) << "\",\"leg\":\"" << JsonEscape(f.ctx.leg)
           << "\",\"step\":" << f.ctx.step << ",\"kind\":\"" << JsonEscape(f.ctx.kind)
           << "\",\"start\":" << f.startUnix << ",\"dur_ms\":" << f.durMs << ",\"outcome\":\""
           << OutcomeName(f.outcome) << "\",\"boss\":" << f.ctx.bossEntry << ",\"boss_name\":\""
           << JsonEscape(f.ctx.bossName) << "\",\"planned\":[";
        for (size_t i = 0; i < f.ctx.planned.size(); ++i)
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
        for (size_t i = 0; i < f.bots.size(); ++i)
        {
            MemberState const& m = f.bots[i];
            if (i)
                ss << ",";
            ss << "{\"name\":\"" << JsonEscape(m.info.name) << "\",\"guid\":" << m.info.guid << ",\"class\":\""
               << JsonEscape(m.info.cls) << "\",\"role\":\"" << RoleName(m.info.role)
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

    void AddEngaged(uint32_t entry, uint64_t guid, std::string const& name)
    {
        if (guid == 0)
            return;
        auto it = engaged_.find(entry);
        if (it != engaged_.end())
        {
            if (it->second.name.empty())
                it->second.name = name;
            it->second.guids.insert(guid);
            return;
        }
        if (engaged_.size() >= kMaxEngagedPerFight)
        {
            ++engagedDropped_;
            return;
        }
        EngagedData& e = engaged_[entry];
        if (e.name.empty())
            e.name = name;
        e.guids.insert(guid);
    }

    void EndFight(uint32_t endMs, bool allDead)
    {
        Outcome o = Outcome::End;
        if (allDead)
            o = Outcome::Wipe;
        else if (bossDied_ && ctx_.bossEntry != 0)
            o = Outcome::Kill;
        uint32_t const dur = endMs >= startMs_ ? endMs - startMs_ : 0;
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
    bool outOfCombat_ = false;
    uint32_t outOfCombatSince_ = 0;
    std::map<uint32_t, EngagedData> engaged_;
    uint64_t engagedDropped_ = 0;   // distinct engaged creatures past the cap
    std::map<std::string, uint64_t> debuffUp_;
    uint64_t debuffSampled_ = 0;
    std::deque<Death> deaths_;
    std::unordered_set<uint32_t> dead_;
    uint32_t fightNumber_ = 0;
    std::deque<FinishedFight> finished_;
};

}  // namespace CombatMonitor
}  // namespace TortoiseBots
