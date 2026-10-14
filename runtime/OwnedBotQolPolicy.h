#pragma once

// Issue #473: eligibility + argument parsing for the opt-in owned/hired-bot
// quality-of-life play style. Every gate defaults closed and pool bots are
// never eligible: the rule is isBot && !isRandomBot, the same predicate as
// PlayerbotAI::IsOwnedBot. Hired companions carry random=true (their
// characters live on managed pool accounts), so callers must pass
// isHired=true for them - a hired bot is eligible only while its hire is
// active. Pure header so the command handlers and the group-accept hook
// share one rule, unit-tested in tools/test_owned_bot_qol_policy.cpp.

#include <cctype>
#include <cstdint>
#include <string>

namespace TortoiseBots
{

enum class OwnedBotQolDecision
{
    Allowed,
    RefusedPoolBot,      // random pool bot: organic progression only
    RefusedFeatureOff,   // the config flag for this part is off
};

inline OwnedBotQolDecision DecideOwnedBotQol(bool isBot, bool isRandomBot, bool isHired,
    bool featureEnabled)
{
    if (!isBot || (isRandomBot && !isHired))
        return OwnedBotQolDecision::RefusedPoolBot;
    if (!featureEnabled)
        return OwnedBotQolDecision::RefusedFeatureOff;
    return OwnedBotQolDecision::Allowed;
}

inline char const* OwnedBotQolDecisionName(OwnedBotQolDecision decision)
{
    switch (decision)
    {
        case OwnedBotQolDecision::Allowed: return "allowed";
        case OwnedBotQolDecision::RefusedPoolBot: return "pool bots keep progressing organically";
        case OwnedBotQolDecision::RefusedFeatureOff: return "feature is disabled";
    }
    return "unknown";
}

// Summon-on-accept condition policy: revive/repair run only out of combat
// unless allowInCombat explicitly opens it (still bounded by the cooldown),
// and a per-bot cooldown (seconds since last group summon, 0 = off) bounds
// uninvite/invite macro abuse. Pure function for unit tests.
inline bool OwnedBotGroupSummonReady(bool inCombat, bool allowInCombat,
    uint32_t cooldown, long secondsSinceLast)
{
    if (inCombat && !allowInCombat)
        return false;
    if (cooldown > 0 && secondsSinceLast >= 0 &&
        static_cast<uint64_t>(secondsSinceLast) < cooldown)
        return false;
    return true;
}

inline bool OwnedBotGroupSummonRevive(bool inCombat, bool allowInCombat, bool revive)
{
    return revive && (!inCombat || allowInCombat);
}
// Autogear ranged pick: a bow or crossbow shoots arrows, a gun shoots bullets,
// and auto shot is dead without the matching ammo. Autogear hands out no ammo
// (the bot's own kit or owned-bot maintenance does), so the ranged pick must
// fit what the bags already hold. Kinds: Empty = nothing in the ranged slot,
// NoAmmo = thrown, wand, relic (no ammo needed), Arrow = bow/crossbow,
// Bullet = gun.
enum class RangedAmmoKind
{
    Empty,
    NoAmmo,
    Arrow,
    Bullet,
};

// A candidate of a different ammo type replaces a usable current weapon only
// when its stat score is at least this percent of the current one.
constexpr uint32_t kRangedTypeSwitchGainPct = 150;

inline bool RangedKindNeedsAmmo(RangedAmmoKind kind)
{
    return kind == RangedAmmoKind::Arrow || kind == RangedAmmoKind::Bullet;
}

inline bool BagsHoldAmmoFor(RangedAmmoKind kind, bool hasArrows, bool hasBullets)
{
    return kind == RangedAmmoKind::Arrow ? hasArrows : kind == RangedAmmoKind::Bullet ? hasBullets : false;
}

// True when the equipped ranged weapon is a dud (needs ammo the bags lack) and
// the candidate needs ammo the bags do hold: then even a lower-scored
// candidate is an upgrade, so the "not better than the old item" gate is skipped.
inline bool OwnedBotRangedReplacesDud(RangedAmmoKind current, RangedAmmoKind candidate,
    bool hasArrows, bool hasBullets)
{
    return RangedKindNeedsAmmo(current) && !BagsHoldAmmoFor(current, hasArrows, hasBullets) &&
        RangedKindNeedsAmmo(candidate) && BagsHoldAmmoFor(candidate, hasArrows, hasBullets);
}

// Decide whether autogear may equip `candidate` (score candidateScore) over
// the equipped ranged weapon `current` (score currentScore, 0 when empty),
// given which ammo the bags hold. Rules:
//  - a candidate that needs no ammo follows the normal score rules;
//  - a candidate whose ammo is in the bags is fine, but switching away from a
//    weapon that is still usable (its own ammo in the bags) to the other ammo
//    type needs a big score gain;
//  - a candidate whose ammo is NOT in the bags is refused when the bot carries
//    ammo of the other type; when it carries none at all, only the type of the
//    weapon it had before (or any type for an empty slot) is allowed.
inline bool OwnedBotAcceptRangedPick(RangedAmmoKind current, uint32_t currentScore,
    RangedAmmoKind candidate, uint32_t candidateScore, bool hasArrows, bool hasBullets)
{
    if (!RangedKindNeedsAmmo(candidate))
        return true;

    auto holds = [&](RangedAmmoKind kind) { return BagsHoldAmmoFor(kind, hasArrows, hasBullets); };

    if (!holds(candidate))
    {
        if (hasArrows || hasBullets)
            return false;
        return current == RangedAmmoKind::Empty || current == candidate;
    }

    if (RangedKindNeedsAmmo(current) && current != candidate && holds(current))
        return static_cast<uint64_t>(candidateScore) * 100 >=
            static_cast<uint64_t>(currentScore) * kRangedTypeSwitchGainPct;
    return true;
}

// Parse the optional autogear quality/ilvl argument ("", "green", "200").
// Returns the clamped quality / effective ilvl cap (0 = config cap only).
// Rejects bare digits <= 5: those read as qualities, not item levels (donor
// AutoGearAction keeps the same confusion guard).
struct OwnedBotAutogearRequest
{
    bool ok = false;
    char const* error = nullptr;
    uint32_t quality = 2;
    uint32_t ilvlCap = 0;
};

inline OwnedBotAutogearRequest ParseOwnedBotAutogearArg(std::string const& param,
    uint32_t qualityCap, uint32_t configIlvlCap)
{
    OwnedBotAutogearRequest request;
    request.quality = qualityCap > 5 ? 5 : qualityCap;
    request.ilvlCap = configIlvlCap;

    std::string arg = param;
    while (!arg.empty() && (arg.front() == ' ' || arg.front() == '\t'))
        arg.erase(arg.begin());
    while (!arg.empty() && (arg.back() == ' ' || arg.back() == '\t'))
        arg.pop_back();
    if (arg.empty())
    {
        request.ok = true;
        return request;
    }
    for (char& c : arg)
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

    // Quality words (donor ChatHelper::parseItemQuality vocabulary).
    static struct { char const* word; uint32_t quality; } const kQualities[] = {
        { "poor", 0 }, { "gray", 0 }, { "grey", 0 }, { "normal", 1 }, { "white", 1 },
        { "uncommon", 2 }, { "green", 2 }, { "rare", 3 }, { "blue", 3 },
        { "epic", 4 }, { "purple", 4 }, { "violet", 4 }, { "legendary", 5 },
    };
    for (auto const& entry : kQualities)
        if (arg == entry.word)
        {
            request.quality = entry.quality > qualityCap ? qualityCap : entry.quality;
            request.ok = true;
            return request;
        }

    bool digitsOnly = !arg.empty() && arg.size() <= 5;
    for (char c : arg)
        if (c < '0' || c > '9')
            digitsOnly = false;
    if (!digitsOnly)
    {
        request.error = "unknown option (use a quality word or an item level number)";
        return request;
    }
    uint32_t requested = static_cast<uint32_t>(stoul(arg));
    if (requested <= 5)
    {
        request.error = "looks like a quality; numbers are item levels (e.g. 200)";
        return request;
    }
    request.ilvlCap = (configIlvlCap == 0) ? requested
        : (requested < configIlvlCap ? requested : configIlvlCap);
    request.ok = true;
    return request;
}

} // namespace TortoiseBots
