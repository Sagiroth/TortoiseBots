// Standalone regression test for the reaction cast guard policy
// (ai/playerbot/ReactionCastGuardPolicy.h): AiPlayerbot.ReactionHealCastGuardPct stands
// `boss tactics seek` and `raid spread` down for a healer's heal that has not got far, and
// nothing else changes.
//
// The cases that fail on the old rule:
// - a Flash Heal 4 % into a PREPARING cast bar (the core's state for a normal cast) with the
//   key at 1 must keep the cast; the old guard only read CASTING (this core's channel
//   phase), so it never saw a normal heal and the seek cut it anyway;
// - the same cast with the key at 0 must still be cut, as before.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_reaction_cast_guard_policy.cpp -o /tmp/test_reaction_cast_guard
//   /tmp/test_reaction_cast_guard

#include "../ai/playerbot/ReactionCastGuardPolicy.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

using ai::ReactionCastBreakDue;
using ai::ReactionCastPct;
using ai::ReactionCastRead;
using ai::ReactionCastRunning;

// The core's own numbers (game/Spells/Spell.h), pinned so a core enum move does not pass silently.
constexpr std::uint32_t kPreparing = ai::REACTION_CAST_PREPARING;
constexpr std::uint32_t kCasting = ai::REACTION_CAST_CASTING;
static_assert(kPreparing == 1, "SPELL_STATE_PREPARING moved");
static_assert(kCasting == 2, "SPELL_STATE_CASTING moved");

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static ReactionCastRead Prepare(std::int32_t totalMs, std::uint32_t remainingMs, bool heal = true)
{
    ReactionCastRead cast;
    cast.state = kPreparing;
    cast.totalMs = totalMs;
    cast.remainingMs = remainingMs;
    cast.isHeal = heal;
    return cast;
}

static ReactionCastRead Channel(std::uint32_t remainingMs, bool heal = true)
{
    ReactionCastRead cast;
    cast.state = kCasting;
    cast.totalMs = 4000; // a channel's duration, from the spell entry
    cast.remainingMs = remainingMs;
    cast.isHeal = heal;
    return cast;
}

int main()
{
    const bool kYields = true;        // boss tactics seek / raid spread
    const bool kDoesNotYield = false; // every emergency
    const bool kHealer = true;
    const bool kNotHealer = false;

    // A Flash Heal (2.5 s) 100 ms in: the walk's median cut, 4 % of the cast, on a normal PREPARING bar.
    ReactionCastRead const flashHeal4 = Prepare(2500, 2400);
    // A cast bar already counted to zero: the spell object is left behind, no heal under way.
    ReactionCastRead const finished = Prepare(2500, 0);

    // 1. The cases this change exists for.
    //    A healer's Flash Heal in PREPARING at 4 %, with a seek picked and the key at 1: the cast stands.
    CHECK(ReactionCastRunning(flashHeal4));
    CHECK(ReactionCastPct(flashHeal4) == 4);
    CHECK(!ReactionCastBreakDue(true, kYields, 1, kHealer, &flashHeal4));
    //    The same with the key at 0: cut, as before.
    CHECK(ReactionCastBreakDue(true, kYields, 0, kHealer, &flashHeal4));

    // 2. A channel is a cast under way too (CASTING after the mana was spent), and its progress reads from the
    //    channel's duration, not the (usually 0) pre-channel cast time.
    ReactionCastRead const channel25 = Channel(3000);
    ReactionCastRead const channelStart = Channel(4000);
    CHECK(ReactionCastRunning(channel25));
    CHECK(ReactionCastPct(channel25) == 25);
    CHECK(!ReactionCastBreakDue(true, kYields, 1, kHealer, &channel25));
    //    At the opening of the channel the key's percent may still cut it.
    CHECK(ReactionCastBreakDue(true, kYields, 1, kHealer, &channelStart));

    // 3. Every other state is no cast to protect: a spell left behind its cast breaks as before.
    {
        ReactionCastRead delayed = flashHeal4; // an instant whose missile is in flight
        delayed.state = 5;                     // SPELL_STATE_DELAYED
        CHECK(!ReactionCastRunning(delayed));
        CHECK(ReactionCastPct(delayed) == -1);
        CHECK(ReactionCastBreakDue(true, kYields, 1, kHealer, &delayed));
    }
    {
        ReactionCastRead idle = flashHeal4;
        idle.state = 4; // SPELL_STATE_IDLE
        CHECK(ReactionCastBreakDue(true, kYields, 1, kHealer, &idle));
    }
    //    A finished cast bar (remaining 0) is no heal under way.
    CHECK(!ReactionCastRunning(finished));
    CHECK(ReactionCastBreakDue(true, kYields, 1, kHealer, &finished));

    // 4. Each input alone restores today's behaviour (the cast breaks).
    ReactionCastRead const smite = Prepare(2500, 2400, false);
    ReactionCastRead const instant = Prepare(0, 0); // nothing on a bar: no total to read
    CHECK(ReactionCastBreakDue(true, kDoesNotYield, 1, kHealer, &flashHeal4));   // an emergency
    CHECK(ReactionCastBreakDue(true, kYields, 1, kNotHealer, &flashHeal4));      // a damage dealer's heal
    CHECK(ReactionCastBreakDue(true, kYields, 1, kHealer, nullptr));             // no generic slot
    CHECK(ReactionCastBreakDue(true, kYields, 1, kHealer, &smite));              // a healer's Smite
    CHECK(ReactionCastBreakDue(true, kYields, 1, kHealer, &instant));

    // 5. A reaction that does not want the cast broken never breaks it, whatever the guard says.
    CHECK(!ReactionCastBreakDue(false, kYields, 0, kHealer, &flashHeal4));
    CHECK(!ReactionCastBreakDue(false, kDoesNotYield, 50, kNotHealer, nullptr));

    // 6. Boundaries: the guard cuts strictly below its percent, so 100 keeps the very end and 1 keeps a 1 % cast.
    ReactionCastRead const almostDone = Prepare(2500, 25);     // 99 %
    ReactionCastRead const lastTick = Prepare(2500, 1);        // 99 %; a 1 ms countdown cannot read 100
    ReactionCastRead const fresh = Prepare(2500, 2500);        // 0 %
    ReactionCastRead const onePercent = Prepare(2500, 2475);   // 1 %
    CHECK(ReactionCastBreakDue(true, kYields, 100, kHealer, &almostDone));
    CHECK(ReactionCastBreakDue(true, kYields, 100, kHealer, &lastTick));
    CHECK(ReactionCastBreakDue(true, kYields, 1, kHealer, &fresh));
    CHECK(!ReactionCastBreakDue(true, kYields, 1, kHealer, &onePercent));
    //    A negative guard is off, like 0.
    CHECK(ReactionCastBreakDue(true, kYields, -5, kHealer, &flashHeal4));

    // 7. The progress read itself.
    CHECK(ReactionCastPct(Prepare(1500, 1500)) == 0);   // just started
    CHECK(ReactionCastPct(Prepare(1500, 750)) == 50);
    CHECK(ReactionCastPct(Prepare(1500, 1)) == 99);     // one tick left is not 100
    CHECK(ReactionCastPct(finished) == -1);             // bar done
    CHECK(ReactionCastPct(flashHeal4) == 4);            // the median cut
    CHECK(ReactionCastPct(Prepare(1500, 2000)) == 0);   // a countdown above the total reads as a fresh cast
    CHECK(ReactionCastPct(instant) == -1);              // no total: an instant, nothing to lose
    CHECK(ReactionCastPct(Prepare(-1, 10)) == -1);
    CHECK(ReactionCastPct(Channel(4000)) == 0);
    CHECK(ReactionCastPct(Channel(1000)) == 75);

    // 8. The two together: a Flash Heal cut 100 ms in is 4 % and keeps casting under the walk value 1; under
    //    guard 20 only the first 20 % may still be cut, so 10 % breaks and 25 % keeps.
    ReactionCastRead const tenPercent = Prepare(2500, 2250);
    ReactionCastRead const quarter = Prepare(2500, 1875);
    CHECK(!ReactionCastBreakDue(true, kYields, 1, kHealer, &flashHeal4));
    CHECK(ReactionCastBreakDue(true, kYields, 20, kHealer, &tenPercent));
    CHECK(!ReactionCastBreakDue(true, kYields, 20, kHealer, &quarter));

    std::printf("OK: %d checks (ReactionCastRunning, ReactionCastPct, ReactionCastBreakDue)\n", checks);
    return 0;
}
