#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Mage portal + warlock/mage service data shared by the whisper command
// (Task A) and `.bot service` (Task B). The refusal/cast rules live here so
// both surfaces answer identically; the callers only map them onto chat lines
// and the TBM protocol. No DB, no world scan: fixed tables in, plain
// strings/ids out.

namespace ai
{

// One portal spell the mage may know. spellId is the spellbook id; EffectItemType in
// tw_world_spell_template confirms every row below is a castable portal (the
// teach rows 1142x/4936x point AT these ids). 1851 is the teach wrapper for
// 10059, not a portal itself, so it stays out.
struct MagePortalSpell
{
    uint32_t spellId;
    char const* city;
};

inline MagePortalSpell const* MagePortalTable(size_t* outCount = nullptr)
{
    static MagePortalSpell const kPortals[] = {
        {10059, "Stormwind"},
        {11416, "Ironforge"},
        {11419, "Darnassus"},
        {11417, "Orgrimmar"},
        {11418, "Undercity"},
        {11420, "Thunder Bluff"},
        {49365, "Theramore"},
        {49362, "Stonard"},
        {49004, "Alah'Thalas"},
        {49002, "Amani'Alor"},
        {49001, "Sparkwater Port"},
        {49000, "Gurubashi Arena"},
        {28148, "Karazhan"},
    };
    if (outCount)
        *outCount = sizeof(kPortals) / sizeof(kPortals[0]);
    return kPortals;
}

// Rune of Portals, consumed by every portal cast. Bots get it free: the cast
// path grants one when missing so the cast never fails for lack of a rune.
inline uint32_t PortalRuneItemId() { return 17032; }

// Conjure/Create lines for the service query. Any rank counts as known.
inline char const* ConjureFoodSpellName() { return "conjure food"; }
inline char const* ConjureWaterSpellName() { return "conjure water"; }
inline char const* CreateHealthstoneSpellName() { return "create healthstone"; }

inline bool CaseInsensitiveStartsWith(char const* text, char const* prefix)
{
    while (*prefix)
    {
        char a = *text ? static_cast<char>(tolower(static_cast<unsigned char>(*text))) : '\0';
        char b = static_cast<char>(tolower(static_cast<unsigned char>(*prefix)));
        if (a != b)
            return false;
        ++text;
        ++prefix;
    }
    return true;
}

// Match a city query against the table: exact name first, then a unique
// prefix ("iron" -> Ironforge). Returns the spell id, or 0 when the query is
// unknown or ambiguous.
inline uint32_t MatchPortalCity(MagePortalSpell const* table, size_t count, std::string const& query)
{
    std::string lowered = query;
    for (char& c : lowered)
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    while (!lowered.empty() && (lowered.front() == ' ' || lowered.front() == '\t'))
        lowered.erase(lowered.begin());
    while (!lowered.empty() && (lowered.back() == ' ' || lowered.back() == '\t'))
        lowered.pop_back();
    if (lowered.empty())
        return 0;

    // Exact city name wins over prefix matches.
    for (size_t i = 0; i < count; ++i)
    {
        std::string city = table[i].city;
        for (char& c : city)
            c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        if (city == lowered)
            return table[i].spellId;
    }

    uint32_t match = 0;
    uint32_t matches = 0;
    for (size_t i = 0; i < count; ++i)
    {
        std::string city = table[i].city;
        for (char& c : city)
            c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        if (city.compare(0, lowered.size(), lowered) == 0)
        {
            match = table[i].spellId;
            if (++matches > 1)
                return 0;
        }
    }
    return matches == 1 ? match : 0;
}

inline uint32_t MatchPortalCity(std::string const& query)
{
    size_t count = 0;
    MagePortalSpell const* table = MagePortalTable(&count);
    return MatchPortalCity(table, count, query);
}

// Refusal reasons for a portal cast, shared by both surfaces. Order matters:
// the first failing check answers, so group/combat state precedes spell
// knowledge.
enum class PortalRefusal
{
    None = 0,
    Dead,
    InCombat,
    Casting,
    NotMage,
    UnknownCity,
    NotLearned,
};

// The caller's live-state probe: everything the policy needs about the bot
// and the request, without touching Player/AI types.
struct PortalCastFacts
{
    bool alive = true;
    bool inCombat = false;
    bool casting = false;
    bool isMage = true;
    bool hasRune = true;
};

inline PortalRefusal CheckPortalCast(PortalCastFacts const& facts, uint32_t spellId, bool knowsSpell)
{
    if (!facts.alive)
        return PortalRefusal::Dead;
    if (facts.inCombat)
        return PortalRefusal::InCombat;
    if (facts.casting)
        return PortalRefusal::Casting;
    if (!facts.isMage)
        return PortalRefusal::NotMage;
    if (!spellId)
        return PortalRefusal::UnknownCity;
    if (!knowsSpell)
        return PortalRefusal::NotLearned;
    return PortalRefusal::None;
}

inline char const* PortalRefusalCode(PortalRefusal refusal)
{
    switch (refusal)
    {
        case PortalRefusal::None: return "";
        case PortalRefusal::Dead: return "dead";
        case PortalRefusal::InCombat: return "combat";
        case PortalRefusal::Casting: return "busy";
        case PortalRefusal::NotMage: return "unknown";
        case PortalRefusal::UnknownCity: return "unknown";
        case PortalRefusal::NotLearned: return "unknown";
    }
    return "unknown";
}

inline char const* PortalRefusalText(PortalRefusal refusal)
{
    switch (refusal)
    {
        case PortalRefusal::None: return "";
        case PortalRefusal::Dead: return "The bot is dead.";
        case PortalRefusal::InCombat: return "The bot is in combat.";
        case PortalRefusal::Casting: return "The bot is busy casting.";
        case PortalRefusal::NotMage: return "That bot is not a mage.";
        case PortalRefusal::UnknownCity: return "Unknown portal city.";
        case PortalRefusal::NotLearned: return "The bot has not learned that portal.";
    }
    return "Unknown portal city.";
}

// City name for a spell id, for the confirm line. Empty when not a portal.
inline char const* PortalCityName(uint32_t spellId)
{
    size_t count = 0;
    MagePortalSpell const* table = MagePortalTable(&count);
    for (size_t i = 0; i < count; ++i)
        if (table[i].spellId == spellId)
            return table[i].city;
    return "";
}

// "spellId:City Name" pairs for the TBM:SERVICE portals field, in table
// order; "-" when the bot knows none.
inline std::string FormatServicePortals(std::vector<uint32_t> const& knownIds)
{
    size_t count = 0;
    MagePortalSpell const* table = MagePortalTable(&count);
    std::string out;
    for (size_t i = 0; i < count; ++i)
    {
        bool known = false;
        for (uint32_t id : knownIds)
            if (id == table[i].spellId)
            {
                known = true;
                break;
            }
        if (!known)
            continue;
        if (!out.empty())
            out += ",";
        out += std::to_string(table[i].spellId);
        out += ":";
        out += table[i].city;
    }
    return out.empty() ? "-" : out;
}

// City list for the bare `portal` whisper answer ("Portals: A, B, C").
inline std::string FormatPortalCityList(std::vector<std::string> const& cities)
{
    std::string out = "Portals: ";
    for (size_t i = 0; i < cities.size(); ++i)
    {
        if (i)
            out += ", ";
        out += cities[i];
    }
    return out;
}

// Shared cast outcome: the single decision both surfaces run. The caller
// probes its live bot into facts, decides, and only then touches the world
// (rune grant, stop, engine cast). Moving never refuses: the caller stops
// the bot first (ai->StopMoving clears the motion master and parks it) and
// casts. Refuse only while a cast is already running.
struct PortalCastDecision
{
    PortalRefusal refusal = PortalRefusal::None;
    bool needsRune = false;
    bool needsStop = false;
};
inline PortalCastDecision DecidePortalCast(PortalCastFacts const& facts, uint32_t spellId, bool knowsSpell,
    bool hasRune, bool moving)
{
    PortalCastDecision decision;
    decision.refusal = CheckPortalCast(facts, spellId, knowsSpell);
    if (decision.refusal != PortalRefusal::None)
        return decision;
    decision.needsRune = !hasRune;
    decision.needsStop = moving;
    return decision;
}

} // namespace ai
