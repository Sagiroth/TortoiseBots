// Standalone regression test for the deferred bot packet delivery gate: a
// packet whose bot is off its map is never handed to the AI (the drain runs
// before the world update that acks a far teleport), is held without a drain
// count (the adapter's per-bot backlog owns the time limit), and is dropped as
// soon as its handler would read the map.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_deferred_packet_policy.cpp -o /tmp/test_deferred_packet_policy
//   /tmp/test_deferred_packet_policy

#include "../runtime/policy/infra/DeferredDeliveryPolicy.h"

#include <cstdio>

using TortoiseBots::DeferredDelivery::Decide;
using TortoiseBots::DeferredDelivery::kSmsgCastResult;
using TortoiseBots::DeferredDelivery::kSmsgMoveKnockBack;
using TortoiseBots::DeferredDelivery::kSmsgSpellDelayed;
using TortoiseBots::DeferredDelivery::kSmsgSpellFailure;
using TortoiseBots::DeferredDelivery::ReadsBotMap;
using TortoiseBots::DeferredDelivery::Verdict;

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

// SMSG_MESSAGECHAT / SMSG_GROUP_INVITE: handlers that do not read the bot's
// map, so they may wait for the teleport to finish.
enum : uint16_t { kChat = 0x096, kInvite = 0x06F };

int main()
{
    // A bot on its map, not teleporting: everything is delivered, including a
    // cast result (the crash case only needs the bot off its map).
    {
        CHECK(Decide(true, false, kSmsgCastResult) == Verdict::Deliver);
        CHECK(Decide(true, false, kChat) == Verdict::Deliver);
    }

    // Failing case: a failed crowd-control cast result (the only GetUnit call
    // in the packet handler) for a bot that is not in world must not reach the
    // AI; the old drain delivered it and GetMap() asserted.
    {
        CHECK(Decide(false, false, kSmsgCastResult) == Verdict::Drop);
        CHECK(Decide(true, true, kSmsgCastResult) == Verdict::Drop);
        // Map-reading handlers in general: never held, always dropped.
        CHECK(ReadsBotMap(kSmsgCastResult));
        CHECK(ReadsBotMap(kSmsgSpellFailure));
        CHECK(ReadsBotMap(kSmsgSpellDelayed));
        CHECK(ReadsBotMap(kSmsgMoveKnockBack));
        CHECK(!ReadsBotMap(kChat));
        CHECK(!ReadsBotMap(kInvite));
        CHECK(Decide(false, false, kSmsgSpellFailure) == Verdict::Drop);
        CHECK(Decide(false, false, kSmsgSpellDelayed) == Verdict::Drop);
        CHECK(Decide(true, true, kSmsgMoveKnockBack) == Verdict::Drop);
    }

    // A packet whose handler does not read the map is always held while the
    // bot is off its map. The adapter's per-bot backlog ages held entries by
    // time, not by drain count: however many commands drain in one tick, a
    // fresh packet survives to the next bot update.
    {
        CHECK(Decide(false, false, kChat) == Verdict::Hold);
        CHECK(Decide(true, true, kChat) == Verdict::Hold);
        CHECK(Decide(false, false, kInvite) == Verdict::Hold);
        for (int i = 0; i < 16; ++i)
            CHECK(Decide(false, false, kChat) == Verdict::Hold);
    }

    if (g_failed)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("test_deferred_packet_policy: all checks passed\n");
    return 0;
}
