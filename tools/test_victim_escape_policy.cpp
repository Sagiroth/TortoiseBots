// Standalone check for ai/playerbot/VictimEscapePolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_victim_escape_policy.cpp -o /tmp/test_victim_escape_policy
//   /tmp/test_victim_escape_policy
#include "../ai/playerbot/VictimEscapePolicy.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

namespace
{
    constexpr std::uint8_t CLASS_WARRIOR = 1;
    constexpr std::uint8_t CLASS_PALADIN = 2;
    constexpr std::uint8_t CLASS_HUNTER = 3;
    constexpr std::uint8_t CLASS_ROGUE = 4;
    constexpr std::uint8_t CLASS_PRIEST = 5;
    constexpr std::uint8_t CLASS_SHAMAN = 7;
    constexpr std::uint8_t CLASS_MAGE = 8;
    constexpr std::uint8_t CLASS_WARLOCK = 9;
    constexpr std::uint8_t CLASS_DRUID = 11;

    VictimEscapeReady Ready(bool fd, bool vanish, bool iceBlock, bool divineShield)
    {
        VictimEscapeReady r;
        r.feignDeath = fd;
        r.vanish = vanish;
        r.iceBlock = iceBlock;
        r.divineShield = divineShield;
        return r;
    }

    const VictimEscapeReady kAll = Ready(true, true, true, true);
    const VictimEscapeReady kNone = Ready(false, false, false, false);
}

int main()
{
    std::cout << "Starting TortoiseBots victim-escape policy tests...\n";

    // The named failing case: Hakkar (taunt-immune) turns on a mage after Cause Insanity and Ice Block is ready.
    // Before the rule nothing fired: the mage died (49 % of non-tank victim episodes in walk 58).
    VictimEscapeReady iceBlock;
    iceBlock.iceBlock = true;
    CHECK(VictimEscapeFor(true, false, true, true, CLASS_MAGE, iceBlock) == VictimEscape::IceBlock);

    // One escape per class, each only when ready.
    CHECK(VictimEscapeFor(true, false, true, true, CLASS_HUNTER, Ready(true, false, false, false)) == VictimEscape::FeignDeath);
    CHECK(VictimEscapeFor(true, false, true, true, CLASS_ROGUE, Ready(false, true, false, false)) == VictimEscape::Vanish);
    CHECK(VictimEscapeFor(true, false, true, true, CLASS_PALADIN, Ready(false, false, false, true)) == VictimEscape::DivineShield);
    // A class reads only its own spell: another class's ready flag never picks it.
    CHECK(VictimEscapeFor(true, false, true, true, CLASS_MAGE, Ready(true, true, false, true)) == VictimEscape::AskProtection);
    CHECK(VictimEscapeFor(true, false, true, true, CLASS_HUNTER, kAll) == VictimEscape::FeignDeath);
    CHECK(VictimEscapeFor(true, false, true, true, CLASS_ROGUE, kAll) == VictimEscape::Vanish);
    // Not ready: the victim asks for protection.
    CHECK(VictimEscapeFor(true, false, true, true, CLASS_HUNTER, kNone) == VictimEscape::AskProtection);
    CHECK(VictimEscapeFor(true, false, true, true, CLASS_ROGUE, kNone) == VictimEscape::AskProtection);
    CHECK(VictimEscapeFor(true, false, true, true, CLASS_MAGE, kNone) == VictimEscape::AskProtection);
    CHECK(VictimEscapeFor(true, false, true, true, CLASS_PALADIN, kNone) == VictimEscape::AskProtection);
    // No own escape on this core: warlock, warrior, druid, shaman, and the priest (Fade is a dummy aura here and moves
    // no threat).
    CHECK(VictimEscapeFor(true, false, true, true, CLASS_WARLOCK, kAll) == VictimEscape::AskProtection);
    CHECK(VictimEscapeFor(true, false, true, true, CLASS_WARRIOR, kAll) == VictimEscape::AskProtection);
    CHECK(VictimEscapeFor(true, false, true, true, CLASS_DRUID, kAll) == VictimEscape::AskProtection);
    CHECK(VictimEscapeFor(true, false, true, true, CLASS_SHAMAN, kAll) == VictimEscape::AskProtection);
    CHECK(VictimEscapeFor(true, false, true, true, CLASS_PRIEST, kAll) == VictimEscape::AskProtection);

    // Flag off: nothing, for every class.
    CHECK(VictimEscapeFor(false, false, true, true, CLASS_MAGE, kAll) == VictimEscape::None);
    CHECK(VictimEscapeFor(false, false, true, true, CLASS_WARLOCK, kAll) == VictimEscape::None);
    // Never on a tank (a protection warrior or a paladin tank keeps the boss).
    CHECK(VictimEscapeFor(true, true, true, true, CLASS_MAGE, kAll) == VictimEscape::None);
    CHECK(VictimEscapeFor(true, true, true, true, CLASS_PALADIN, kAll) == VictimEscape::None);
    CHECK(VictimEscapeFor(true, true, true, true, CLASS_WARRIOR, kAll) == VictimEscape::None);
    // A tauntable boss: the tanks take it back, no escape is spent.
    CHECK(VictimEscapeFor(true, false, true, false, CLASS_MAGE, kAll) == VictimEscape::None);
    CHECK(VictimEscapeFor(true, false, true, false, CLASS_WARLOCK, kAll) == VictimEscape::None);
    // The boss is not on this bot.
    CHECK(VictimEscapeFor(true, false, false, true, CLASS_MAGE, kAll) == VictimEscape::None);

    // The 6 s throttle: one landed escape per victim per window.
    CHECK(!VictimEscapeRecent(false, true, 0));
    CHECK(VictimEscapeRecent(true, true, 0));
    CHECK(VictimEscapeRecent(true, true, 5999));
    CHECK(!VictimEscapeRecent(true, true, 6000));
    CHECK(!VictimEscapeRecent(true, true, 60000));
    VictimEscapeGuards recent;
    recent.escapedRecently = VictimEscapeRecent(true, true, 2000);
    CHECK(VictimEscapeFor(true, false, true, true, CLASS_MAGE, kAll, recent) == VictimEscape::None);
    VictimEscapeGuards past;
    past.escapedRecently = VictimEscapeRecent(true, true, 6000);
    CHECK(VictimEscapeFor(true, false, true, true, CLASS_MAGE, kAll, past) == VictimEscape::IceBlock);

    // A resisted Feign Death (the boss is still on the hunter, no feign-death state) holds the slot for 1 s only; then
    // Feign Death is on cooldown, the hunter asks for protection and a paladin in reach protects it.
    CHECK(VictimEscapeRecent(true, false, 999));
    CHECK(!VictimEscapeRecent(true, false, 1000));
    VictimEscapeGuards resisted;
    resisted.escapedRecently = VictimEscapeRecent(true, false, 500);
    CHECK(!ProtectorShouldBop(true, true, false, VictimEscapeFor(true, false, true, true, CLASS_HUNTER, kNone, resisted), 10.0f));
    resisted.escapedRecently = VictimEscapeRecent(true, false, 1200);
    CHECK(ProtectorShouldBop(true, true, false, VictimEscapeFor(true, false, true, true, CLASS_HUNTER, kNone, resisted), 10.0f));

    // Readiness is the full cast check, not the cooldown. A silenced mage (Ice Block off cooldown) fails it, so a
    // paladin protects it; a rogue without Flash Powder the same; a mage mid-Frostbolt is refused only by its own cast,
    // which the escape breaks, so Ice Block is ready; mid-cast and refused by the core check: not ready.
    CHECK(!VictimEscapeCastReady(false, false, false));                 // silenced: SPELL_FAILED_SILENCED
    CHECK(!VictimEscapeCastReady(false, false, true));                  // a refusal other than the own cast
    CHECK(VictimEscapeCastReady(true, false, false));
    CHECK(VictimEscapeCastReady(false, true, true));                    // own Frostbolt in progress
    CHECK(!VictimEscapeCastReady(false, true, false));
    VictimEscapeReady silencedMage;
    silencedMage.iceBlock = VictimEscapeCastReady(false, false, false);
    CHECK(ProtectorShouldBop(true, true, false, VictimEscapeFor(true, false, true, true, CLASS_MAGE, silencedMage), 20.0f));
    VictimEscapeReady noFlashPowder;
    noFlashPowder.vanish = VictimEscapeCastReady(false, false, false); // SPELL_FAILED_ITEM_NOT_READY / reagents
    CHECK(ProtectorShouldBop(true, true, false, VictimEscapeFor(true, false, true, true, CLASS_ROGUE, noFlashPowder), 20.0f));
    // A real player (no bot AI) reads no own escape: a paladin protects them.
    CHECK(ProtectorShouldBop(true, true, false, VictimEscapeFor(true, false, true, true, CLASS_MAGE, kNone), 20.0f));

    // Scope: a taunt-immune boss with a threat_successor row (Hakkar, Jin'do). A taunt-immune boss without one (a
    // fixate boss such as Buru) gives no escape.
    CHECK(VictimEscapeBossInScope(true, true));
    CHECK(!VictimEscapeBossInScope(true, false));
    CHECK(!VictimEscapeBossInScope(false, true));
    CHECK(VictimEscapeFor(true, false, true, VictimEscapeBossInScope(true, false), CLASS_MAGE, kAll) == VictimEscape::None);
    CHECK(VictimEscapeFor(true, false, true, VictimEscapeBossInScope(true, true), CLASS_MAGE, kAll) == VictimEscape::IceBlock);

    // The duty rule: a hunter a Son of Hakkar is attacking gets no Feign Death (it would wipe the Son's threat too).
    VictimEscapeGuards sonOnHunter;
    sonOnHunter.holdsDuty = VictimHoldsDuty(false, false, 1);
    CHECK(VictimEscapeFor(true, false, true, true, CLASS_HUNTER, kAll, sonOnHunter) == VictimEscape::None);
    CHECK(VictimHoldsDuty(true, false, 0));
    CHECK(VictimHoldsDuty(false, true, 0));
    CHECK(!VictimHoldsDuty(false, false, 0));

    // The only living non-tank target: the boss would come straight back, so nothing is spent.
    VictimEscapeGuards alone;
    alone.otherNonTankTarget = false;
    CHECK(VictimEscapeFor(true, false, true, true, CLASS_MAGE, kAll, alone) == VictimEscape::None);
    // A boss-tactic duty (the walk-out puller, a held Son) is never dropped.
    VictimEscapeGuards duty;
    duty.holdsDuty = true;
    CHECK(VictimEscapeFor(true, false, true, true, CLASS_HUNTER, kAll, duty) == VictimEscape::None);
    CHECK(VictimEscapeFor(true, false, true, true, CLASS_WARLOCK, kAll, duty) == VictimEscape::None);

    // The Blessing of Protection fallback: a paladin within 30 yd protects a victim with no own escape ready.
    const VictimEscape warlockAsks = VictimEscapeFor(true, false, true, true, CLASS_WARLOCK, kNone);
    CHECK(ProtectorShouldBop(true, true, false, warlockAsks, 25.0f));
    CHECK(ProtectorShouldBop(true, true, false, warlockAsks, 30.0f));   // the edge is inclusive
    CHECK(!ProtectorShouldBop(true, true, false, warlockAsks, 30.5f));
    CHECK(!ProtectorShouldBop(false, true, false, warlockAsks, 10.0f)); // only a paladin
    CHECK(!ProtectorShouldBop(true, false, false, warlockAsks, 10.0f)); // protection not ready
    CHECK(!ProtectorShouldBop(true, true, true, warlockAsks, 0.0f));    // never itself
    // A victim with its own escape ready uses it; a tank, a flag off, a tauntable boss or a throttled victim give None.
    CHECK(!ProtectorShouldBop(true, true, false, VictimEscapeFor(true, false, true, true, CLASS_MAGE, iceBlock), 10.0f));
    CHECK(!ProtectorShouldBop(true, true, false, VictimEscapeFor(true, true, true, true, CLASS_WARLOCK, kNone), 10.0f));
    CHECK(!ProtectorShouldBop(true, true, false, VictimEscapeFor(false, false, true, true, CLASS_WARLOCK, kNone), 10.0f));
    CHECK(!ProtectorShouldBop(true, true, false, VictimEscapeFor(true, false, true, false, CLASS_WARLOCK, kNone), 10.0f));
    CHECK(!ProtectorShouldBop(true, true, false, VictimEscapeFor(true, false, true, true, CLASS_WARLOCK, kNone, recent), 10.0f));
    // A paladin victim whose Divine Shield is on cooldown may be protected by another paladin.
    CHECK(ProtectorShouldBop(true, true, false, VictimEscapeFor(true, false, true, true, CLASS_PALADIN, kNone), 10.0f));
    // A priest victim (no threat drop of its own on this core) is protected.
    CHECK(ProtectorShouldBop(true, true, false, VictimEscapeFor(true, false, true, true, CLASS_PRIEST, kAll), 10.0f));

    // The cast names the action resolves through the bot's "spell id" lookup (the highest known rank).
    CHECK(std::strcmp(VictimEscapeWord(VictimEscape::IceBlock), "ice block") == 0);
    CHECK(std::strcmp(VictimEscapeWord(VictimEscape::AskProtection), "hand of protection") == 0);
    CHECK(std::strcmp(VictimEscapeWord(VictimEscape::None), "") == 0);

    std::cout << "All victim-escape policy tests passed.\n";
    return 0;
}
