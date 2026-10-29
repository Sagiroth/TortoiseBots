#pragma once

// Ready check (task R4m): the facts the dashboard needs before a pull, read
// off a bound bot out of combat. Aura facts become the raid-buff and
// consumable bits; the bot's spells become the key cooldowns (a class
// cooldown of two minutes or longer), one rank per chain. Standard library
// only, so tools/test_ready_check_policy.cpp compiles it without a game
// header; the core numbers it mirrors are pinned by that test.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace TortoiseBots {
namespace ReadyCheck {

// The aura effects, stat indexes, power and spell families the core defines.
constexpr uint32_t kAuraModStat = 29;          // Spells/SpellAuraDefines.h:167 SPELL_AURA_MOD_STAT
constexpr uint32_t kAuraModPercentStat = 80;   // :218 SPELL_AURA_MOD_PERCENT_STAT
constexpr uint32_t kAuraModPowerRegen = 85;    // :223 SPELL_AURA_MOD_POWER_REGEN
constexpr uint32_t kAuraModAttackPower = 99;   // :237 SPELL_AURA_MOD_ATTACK_POWER
constexpr int32_t kStatAll = -1;               // the all-stats misc value (Gift of the Wild)
constexpr int32_t kStatStamina = 2;            // SharedDefines.h:123 STAT_STAMINA
constexpr int32_t kStatIntellect = 3;          // :124 STAT_INTELLECT
constexpr int32_t kStatSpirit = 4;             // :125 STAT_SPIRIT
constexpr int32_t kPowerMana = 0;              // :132 POWER_MANA
constexpr uint32_t kFamilyGeneric = 0;         // Database/DBCEnums.h:162 SPELLFAMILY_GENERIC
constexpr uint32_t kFamilyPotion = 13;         // :175 SPELLFAMILY_POTION
constexpr uint32_t kElixirFlaskMask = 0x03;    // Spells/SpellMgr.h:104 ELIXIR_FLASK_MASK
constexpr uint32_t kElixirWellFed = 0x10;      // :105 ELIXIR_WELL_FED

// The wire bits (section 5.1 of the spec).
constexpr uint32_t kBuffStamina = 1;
constexpr uint32_t kBuffIntellect = 2;
constexpr uint32_t kBuffSpirit = 4;
constexpr uint32_t kBuffAllStats = 8;
constexpr uint32_t kBuffAttackPower = 16;
constexpr uint32_t kBuffManaRegen = 32;
constexpr uint32_t kConsFlask = 1;
constexpr uint32_t kConsElixir = 2;
constexpr uint32_t kConsWellFed = 4;

// A class cooldown is worth listing from two minutes up (RecoveryTime or
// CategoryRecoveryTime, whichever is longer).
constexpr uint32_t kKeyCooldownMs = 120000;
constexpr uint32_t kReadyRefreshMs = 10000;    // the auras and the cooldown state
constexpr uint32_t kKeySpellsRefreshMs = 60000; // which spells are known
constexpr size_t kMaxKeySpells = 8;

// One effect of one aura holder, as the commands layer reads it: the holder's
// spell prototype and state, per effect index.
struct AuraFact
{
    uint32_t auraType = 0;     // EffectApplyAuraName[i]
    int32_t misc = 0;          // EffectMiscValue[i]: the stat index or the power type
    uint32_t family = 0;       // SpellFamilyName
    uint32_t elixirMask = 0;   // sSpellMgr.GetSpellElixirMask(id)
    bool positive = false;     // holder->IsPositive()
    bool fromPlayer = false;   // holder->GetCasterGuid().IsPlayer()
};

// The raid buffs present, by aura category: a positive aura a player cast, of
// a spell family that is neither generic (a consumable) nor a potion.
inline uint32_t BuffBits(std::vector<AuraFact> const& facts)
{
    uint32_t bits = 0;
    for (AuraFact const& f : facts)
    {
        if (!f.positive || !f.fromPlayer || f.family == kFamilyGeneric || f.family == kFamilyPotion)
            continue;
        if (f.auraType == kAuraModStat || f.auraType == kAuraModPercentStat)
        {
            if (f.misc == kStatAll)
                bits |= kBuffAllStats;
            else if (f.misc == kStatStamina)
                bits |= kBuffStamina;
            else if (f.misc == kStatIntellect)
                bits |= kBuffIntellect;
            else if (f.misc == kStatSpirit)
                bits |= kBuffSpirit;
        }
        else if (f.auraType == kAuraModAttackPower)
            bits |= kBuffAttackPower;
        else if (f.auraType == kAuraModPowerRegen && f.misc == kPowerMana)
            bits |= kBuffManaRegen;
    }
    return bits;
}

// The consumables the bot has on: a flask (both elixir bits), any other elixir
// mask, well fed, or a potion-family aura without an elixir mask.
inline uint32_t ConsumableBits(std::vector<AuraFact> const& facts)
{
    uint32_t bits = 0;
    for (AuraFact const& f : facts)
    {
        if (!f.positive)
            continue;
        if (f.elixirMask)
        {
            if ((f.elixirMask & kElixirFlaskMask) == kElixirFlaskMask)
                bits |= kConsFlask;
            else if (f.elixirMask & ~kElixirWellFed)
                bits |= kConsElixir;
            if (f.elixirMask & kElixirWellFed)
                bits |= kConsWellFed;
        }
        else if (f.family == kFamilyPotion)
            bits |= kConsElixir;
    }
    return bits;
}

// A known spell as the commands layer reads it from the spell map and the
// spell's DBC row.
struct SpellCd
{
    uint32_t spell = 0;              // the spell id
    uint32_t chainFirst = 0;         // sSpellMgr.GetFirstSpellInChain(id)
    uint32_t recoveryMs = 0;         // SpellEntry::RecoveryTime (Spells/SpellEntry.h:560)
    uint32_t categoryRecoveryMs = 0; // SpellEntry::CategoryRecoveryTime (:561)
    bool passive = false;            // SpellEntry::IsPassiveSpell (Spells/SpellEntry.h:839)
    uint32_t family = 0;             // SpellEntry::SpellFamilyName (:623)
};

inline uint32_t CooldownMs(SpellCd const& s)
{
    return std::max(s.recoveryMs, s.categoryRecoveryMs);
}

// A key cooldown: an active class spell that takes two minutes or more.
inline bool IsKeyCooldown(SpellCd const& s)
{
    return !s.passive && s.family != kFamilyGeneric && CooldownMs(s) >= kKeyCooldownMs;
}

// One spell per chain (the highest rank present), the longest cooldown first,
// at most kMaxKeySpells.
inline std::vector<uint32_t> KeySpells(std::vector<SpellCd> const& spells)
{
    std::map<uint32_t, SpellCd> byChain;
    for (SpellCd const& s : spells)
    {
        if (!IsKeyCooldown(s))
            continue;
        uint32_t const chain = s.chainFirst ? s.chainFirst : s.spell;
        auto it = byChain.find(chain);
        if (it == byChain.end())
            byChain.emplace(chain, s);
        else if (s.spell > it->second.spell)
            it->second = s;
    }
    std::vector<SpellCd> chosen;
    chosen.reserve(byChain.size());
    for (auto const& entry : byChain)
        chosen.push_back(entry.second);
    std::sort(chosen.begin(), chosen.end(), [](SpellCd const& a, SpellCd const& b) {
        uint32_t const am = CooldownMs(a), bm = CooldownMs(b);
        if (am != bm)
            return am > bm;
        return a.spell > b.spell;
    });
    size_t const count = std::min(chosen.size(), kMaxKeySpells);
    std::vector<uint32_t> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i)
        out.push_back(chosen[i].spell);
    return out;
}

} // namespace ReadyCheck
} // namespace TortoiseBots
