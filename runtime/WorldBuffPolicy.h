// Issue #492: world buffs at the six capital <Mercenary Hire> recruiters.
// Pure, core-free policy: gating, gossip routing, price math and the static
// unlock catalog (quest ids, aura/boss credit maps, raid-strip map table).
// Unit-tested standalone (tools/test_world_buff_policy.cpp). The gossip and
// purchase code reads this header so the menu, the unlock checks and the
// tests can never disagree about ids or prices.

#pragma once

#include <cstddef>
#include <cstdint>

namespace TortoiseBots
{

// Gossip sender ids for the world-buff branch. Hire wizard owns 501-504;
// unknown senders fall into the class menu, so these must be matched before
// that fallback. Actions stay small (< 254) so they never collide with the
// packed hire confirm/back actions (254/255, matched on action alone).
constexpr uint32_t kWorldBuffSenderRoot = 505;
constexpr uint32_t kWorldBuffSenderBuy = 506;
constexpr uint32_t kWorldBuffSenderSayge = 507;
constexpr uint32_t kWorldBuffSenderBack = 508;
constexpr uint32_t kWorldBuffSenderConfirm = 509;

// Root-menu actions (sender 505).
constexpr uint32_t kWorldBuffRootHire = 1;
constexpr uint32_t kWorldBuffRootBuffs = 2;

// Purchase indices (sender 506 action). Sayge opens a variant submenu
// (sender 507, action = Sayge variant index) instead of buying directly.
constexpr uint8_t kWorldBuffBuyRally = 1;
constexpr uint8_t kWorldBuffBuyWarchief = 2;
constexpr uint8_t kWorldBuffBuyZandalar = 3;
constexpr uint8_t kWorldBuffBuyDmPack = 4;
constexpr uint8_t kWorldBuffBuySayge = 5;
constexpr uint8_t kWorldBuffBuySongflower = 6;
constexpr uint8_t kWorldBuffBuySilithyst = 7;
constexpr uint8_t kWorldBuffBuyCount = 7;

// Unlock quest ids (migration range 90000-90013, one per buff per faction).
constexpr uint32_t kWorldBuffQuestRallyA = 90000;
constexpr uint32_t kWorldBuffQuestRallyH = 90001;
constexpr uint32_t kWorldBuffQuestRendA = 90002;
constexpr uint32_t kWorldBuffQuestRendH = 90003;
constexpr uint32_t kWorldBuffQuestZandalarA = 90004;
constexpr uint32_t kWorldBuffQuestZandalarH = 90005;
constexpr uint32_t kWorldBuffQuestDmA = 90006;
constexpr uint32_t kWorldBuffQuestDmH = 90007;
constexpr uint32_t kWorldBuffQuestSaygeA = 90008;
constexpr uint32_t kWorldBuffQuestSaygeH = 90009;
constexpr uint32_t kWorldBuffQuestSongA = 90010;
constexpr uint32_t kWorldBuffQuestSongH = 90011;
constexpr uint32_t kWorldBuffQuestSilithystA = 90012;
constexpr uint32_t kWorldBuffQuestSilithystH = 90013;

// Invisible kill-credit entries (never spawned, credit-only).
constexpr uint32_t kWorldBuffRallyCredit = 95100;    // Onyxia OR Nefarian
constexpr uint32_t kWorldBuffSilithystCredit = 95101; // 5 opposite-faction kills
constexpr uint32_t kWorldBuffSilithystKillCount = 5;

// Boss entries that feed the Rally credit.
constexpr uint32_t kWorldBuffBossOnyxia = 10184;
constexpr uint32_t kWorldBuffBossNefarian = 11583;
// Deliberately NOT mapped: Gyth (10339, dies with Rend) must not credit.
constexpr uint32_t kWorldBuffBossRend = 10429;
constexpr uint32_t kWorldBuffBossHakkar = 14834;

// Buff aura spells.
constexpr uint32_t kWorldBuffSpellRally = 22888;      // 2 h
constexpr uint32_t kWorldBuffSpellWarchief = 16609;   // 1 h
constexpr uint32_t kWorldBuffSpellZandalar = 24425;  // 2 h
constexpr uint32_t kWorldBuffSpellSongflower = 15366; // 1 h
constexpr uint32_t kWorldBuffSpellSilithyst = 29534;  // 30 min

// Capital recruiter entries (the only NPCs that show the branch).
constexpr uint32_t kWorldBuffCapitalRecruiters[] = {
    95017, // Stormwind (Walter)
    95006, // Ironforge (Dagna Firebrew)
    95012, // Darnassus (Taeloran)
    95025, // Orgrimmar (Throk)
    95018, // Undercity (Agatha)
    95019, // Thunder Bluff (Tahkan)
};

// Every <Mercenary Hire> recruiter entry (95000-95067). Auras whose caster is
// one of these never count toward an aura unlock: otherwise a buyer could
// unlock the buff for the whole group straight from the purchase.
constexpr uint32_t kHireRecruiterEntryFirst = 95000;
constexpr uint32_t kHireRecruiterEntryLast = 95067;

// Maps whose instance_buff_removal rows strip world buffs on entry, plus
// Upper Karazhan (814), which has no rows but is stripped/restored by the
// module when the keep flag is off.
constexpr uint32_t kWorldBuffStripMaps[] = {
    45, 249, 309, 409, 469, 509, 531, 532, 533, 807,
};
constexpr uint32_t kWorldBuffUpperKaraMap = 814;

inline bool IsCapitalRecruiter(uint32_t entry)
{
    for (uint32_t capital : kWorldBuffCapitalRecruiters)
        if (capital == entry)
            return true;
    return false;
}

inline bool IsHireRecruiterEntry(uint32_t entry)
{
    return entry >= kHireRecruiterEntryFirst && entry <= kHireRecruiterEntryLast;
}

// Root branch visibility: feature on, real player (pool bots hire only),
// level 60+.
inline bool ShouldShowWorldBuffs(bool enabled, bool isBot, uint32_t level, uint32_t minLevel)
{
    return enabled && !isBot && level >= minLevel;
}

inline bool IsValidWorldBuffPurchase(uint8_t purchase)
{
    return purchase >= kWorldBuffBuyRally && purchase <= kWorldBuffBuyCount;
}

// base + perPerson * headCount, copper. headCount always includes the buyer
// (min 1): solo Rally is 10g + 1 x 2g = 12g, 5 stacked is 10g + 5 x 2g = 20g.
inline uint32_t WorldBuffTotalPrice(uint32_t baseCopper, uint32_t perPersonCopper, uint32_t headCount)
{
    if (headCount < 1)
        headCount = 1;
    uint64_t total = static_cast<uint64_t>(baseCopper) +
        static_cast<uint64_t>(perPersonCopper) * headCount;
    return total > 100000000ULL ? 100000000U : static_cast<uint32_t>(total);
}

// Purchase price tiers: DM pack shares the standard 10g+2g pair.
enum class WorldBuffPriceTier : uint8_t
{
    Standard,
    Sayge,
    Songflower,
    Silithyst,
};

inline WorldBuffPriceTier PriceTierForPurchase(uint8_t purchase)
{
    switch (purchase)
    {
        case kWorldBuffBuySayge: return WorldBuffPriceTier::Sayge;
        case kWorldBuffBuySongflower: return WorldBuffPriceTier::Songflower;
        case kWorldBuffBuySilithyst: return WorldBuffPriceTier::Silithyst;
        default: return WorldBuffPriceTier::Standard;
    }
}

// Unlock quest for a purchase index and faction.
inline uint32_t PurchaseUnlockQuest(uint8_t purchase, bool horde)
{
    switch (purchase)
    {
        case kWorldBuffBuyRally: return horde ? kWorldBuffQuestRallyH : kWorldBuffQuestRallyA;
        case kWorldBuffBuyWarchief: return horde ? kWorldBuffQuestRendH : kWorldBuffQuestRendA;
        case kWorldBuffBuyZandalar: return horde ? kWorldBuffQuestZandalarH : kWorldBuffQuestZandalarA;
        case kWorldBuffBuyDmPack: return horde ? kWorldBuffQuestDmH : kWorldBuffQuestDmA;
        case kWorldBuffBuySayge: return horde ? kWorldBuffQuestSaygeH : kWorldBuffQuestSaygeA;
        case kWorldBuffBuySongflower: return horde ? kWorldBuffQuestSongH : kWorldBuffQuestSongA;
        case kWorldBuffBuySilithyst: return horde ? kWorldBuffQuestSilithystH : kWorldBuffQuestSilithystA;
        default: return 0;
    }
}

// Dire Maul tribute guards: any ONE of the three counts (each guard casts
// only its own buff, so requiring all three would force three tribute runs).
inline bool IsDireMaulTributeAura(uint32_t spellId)
{
    return spellId == 22817 || spellId == 22818 || spellId == 22820;
}

struct WorldBuffSaygeVariant
{
    uint32_t spellId;
    char const* name;
};

inline WorldBuffSaygeVariant const* SaygeVariants(uint32_t& count)
{
    static WorldBuffSaygeVariant const variants[] = {
        { 23735, "Strength" },
        { 23736, "Agility" },
        { 23737, "Stamina" },
        { 23738, "Spirit" },
        { 23766, "Intelligence" },
        { 23767, "Armor" },
        { 23768, "Damage" },
        { 23769, "Resistance" },
    };
    count = 8;
    return variants;
}

inline bool IsSaygeFortuneAura(uint32_t spellId)
{
    uint32_t count = 0;
    for (WorldBuffSaygeVariant const* v = SaygeVariants(count); count > 0; --count, ++v)
        if (v->spellId == spellId)
            return true;
    return false;
}

// Action encoding for purchase options (sender 506): standard buys carry the
// buy index (1..7) directly. Sayge variant picks (1..8) pack the pick in the
// high byte and kWorldBuffBuySayge in the low byte.
inline uint32_t EncodeSaygeAction(uint8_t buyIndex, uint32_t pick)
{
    return (pick << 8) | buyIndex;
}

inline uint8_t DecodeBuyIndex(uint32_t action)
{
    return static_cast<uint8_t>(action & 0xFF);
}

inline uint32_t DecodeSaygePick(uint32_t action)
{
    return (action >> 8) & 0xFF;
}

// Confirm page (sender 509): the buy action in the low 16 bits and the head
// count the price was quoted for in the high 16, so a changed group is
// re-quoted instead of charged.
inline uint32_t EncodeConfirmAction(uint32_t buyAction, uint32_t people)
{
    return (people << 16) | (buyAction & 0xFFFF);
}

inline uint32_t DecodeConfirmBuyAction(uint32_t action)
{
    return action & 0xFFFF;
}

inline uint32_t DecodeConfirmCount(uint32_t action)
{
    return action >> 16;
}

// Fast check for the aura unlock hot path.
inline bool IsAuraUnlockSpell(uint32_t spellId)
{
    return spellId == kWorldBuffSpellSongflower ||
           IsDireMaulTributeAura(spellId) ||
           IsSaygeFortuneAura(spellId);
}

// Aura-gain unlock quest for one real-way aura gain. 0 = no quest tracks it.
inline uint32_t AuraUnlockQuest(uint32_t spellId, bool horde)
{
    if (IsDireMaulTributeAura(spellId))
        return horde ? kWorldBuffQuestDmH : kWorldBuffQuestDmA;
    if (IsSaygeFortuneAura(spellId))
        return horde ? kWorldBuffQuestSaygeH : kWorldBuffQuestSaygeA;
    if (spellId == kWorldBuffSpellSongflower)
        return horde ? kWorldBuffQuestSongH : kWorldBuffQuestSongA;
    return 0;
}

// Boss-kill credit entry for the Rally OR-quest. Rend/Hakkar quests use
// direct kill objectives (core shares them with the group natively), so they
// map to 0 here: no module translation needed.
inline uint32_t BossKillCreditEntry(uint32_t bossEntry)
{
    if (bossEntry == kWorldBuffBossOnyxia || bossEntry == kWorldBuffBossNefarian)
        return kWorldBuffRallyCredit;
    return 0;
}

inline bool IsRaidStripMap(uint32_t mapId)
{
    for (uint32_t map : kWorldBuffStripMaps)
        if (map == mapId)
            return true;
    return false;
}

// Destination needs a pre-teleport buff snapshot: every core strip map plus
// Upper Kara (the module covers the 814 gap itself).
inline bool NeedsBuffSnapshot(uint32_t mapId)
{
    return mapId == kWorldBuffUpperKaraMap || IsRaidStripMap(mapId);
}

// After the map change: with the keep flag on, snapshots are restored on
// strip maps; with it off, core already stripped those, and the module only
// strips Upper Kara.
inline bool ShouldRestoreAfterTeleport(bool keepInRaids, uint32_t mapId)
{
    return keepInRaids && IsRaidStripMap(mapId);
}

inline bool ShouldStripUpperKara(bool keepInRaids, uint32_t mapId)
{
    return !keepInRaids && mapId == kWorldBuffUpperKaraMap;
}

} // namespace TortoiseBots
