// Standalone regression test for the deferred bot packet delivery gate: a
// packet whose bot is off its map is never handed to the AI (the drain runs
// before the world update that acks a far teleport), is held for a bounded
// number of drains when its handler does not read the map, and is dropped as
// soon as it would.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_deferred_packet_policy.cpp -o /tmp/test_deferred_packet_policy
//   /tmp/test_deferred_packet_policy

#include "../runtime/policy/infra/DeferredDeliveryPolicy.h"
#include "../runtime/WorldThreadQueue.h"

#include <cstdio>

using TortoiseBots::DeferredDelivery::Decide;
using TortoiseBots::DeferredDelivery::kMaxHeldDrains;
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

// The packet copy the drain stores, with the count of drains that held it.
struct Item
{
    uint16_t opcode;
    uint8_t heldDrains = 0;
};

// The adapter's drain body, reduced to the decision: returns true when the
// item reached the AI, false when it was held or dropped, and reports which.
static bool DrainOnce(TortoiseBots::WorldThreadQueue<Item>& queue, bool inWorld, bool farTeleport,
    Verdict& verdict)
{
    bool delivered = false;
    queue.Drain([&](uint64_t, Item& item)
    {
        verdict = Decide(inWorld, farTeleport, item.opcode, item.heldDrains);
        switch (verdict)
        {
            case Verdict::Deliver:
                delivered = true;
                break;
            case Verdict::Hold:
                ++item.heldDrains;
                queue.Enqueue(1, item);
                break;
            case Verdict::Drop:
                break;
        }
    });
    return delivered;
}

int main()
{
    // A bot on its map, not teleporting: everything is delivered, including a
    // cast result (the crash case only needs the bot off its map).
    {
        CHECK(Decide(true, false, kSmsgCastResult, 0) == Verdict::Deliver);
        CHECK(Decide(true, false, kChat, 0) == Verdict::Deliver);
        CHECK(Decide(true, false, kChat, kMaxHeldDrains) == Verdict::Deliver);
    }

    // Failing case: a failed crowd-control cast result (the only GetUnit call
    // in the packet handler) for a bot that is not in world must not reach the
    // AI; the old drain delivered it and GetMap() asserted.
    {
        CHECK(Decide(false, false, kSmsgCastResult, 0) == Verdict::Drop);
        CHECK(Decide(true, true, kSmsgCastResult, 0) == Verdict::Drop);
        CHECK(Decide(false, false, kSmsgCastResult, 2) == Verdict::Drop);
        // Map-reading handlers in general: never held, always dropped.
        CHECK(ReadsBotMap(kSmsgCastResult));
        CHECK(ReadsBotMap(kSmsgSpellFailure));
        CHECK(ReadsBotMap(kSmsgSpellDelayed));
        CHECK(ReadsBotMap(kSmsgMoveKnockBack));
        CHECK(!ReadsBotMap(kChat));
        CHECK(!ReadsBotMap(kInvite));
        CHECK(Decide(false, false, kSmsgSpellFailure, 0) == Verdict::Drop);
        CHECK(Decide(false, false, kSmsgSpellDelayed, 0) == Verdict::Drop);
        CHECK(Decide(true, true, kSmsgMoveKnockBack, 0) == Verdict::Drop);
    }

    // A packet whose handler does not read the map is held for the next drain:
    // chat and invites must survive one far teleport.
    {
        CHECK(Decide(false, false, kChat, 0) == Verdict::Hold);
        CHECK(Decide(true, true, kChat, 0) == Verdict::Hold);
        CHECK(Decide(false, false, kInvite, 0) == Verdict::Hold);
        CHECK(Decide(false, false, kChat, kMaxHeldDrains - 1) == Verdict::Hold);
        CHECK(Decide(false, false, kChat, kMaxHeldDrains) == Verdict::Drop);
    }

    // The whole hold path over the real queue: three drains hold, the fourth
    // drops, and the AI never runs while the bot is off its map.
    {
        TortoiseBots::WorldThreadQueue<Item> queue(8);
        queue.Enqueue(1, Item{ kInvite });
        int held = 0, dropped = 0, delivered = 0;
        for (int i = 0; i < kMaxHeldDrains + 1; ++i)
        {
            Verdict verdict = Verdict::Deliver;
            if (DrainOnce(queue, false, false, verdict))
                ++delivered;
            if (verdict == Verdict::Hold)
                ++held;
            if (verdict == Verdict::Drop)
                ++dropped;
        }
        CHECK(held == kMaxHeldDrains);
        CHECK(dropped == 1);
        CHECK(delivered == 0);
        CHECK(queue.PendingCount() == 0);

        // Once the bot is back on its map the held packet is delivered.
        queue.Enqueue(1, Item{ kInvite });
        Verdict verdict = Verdict::Drop;
        CHECK(!DrainOnce(queue, false, false, verdict));
        CHECK(verdict == Verdict::Hold);
        CHECK(DrainOnce(queue, true, false, verdict));
        CHECK(verdict == Verdict::Deliver);
        CHECK(queue.PendingCount() == 0);
    }

    // The unit-lookup guard (PlayerbotAI::GetUnit and siblings) is pinned by
    // tools/test_getmap_guard_wiring.py: it needs a live Player, so it cannot
    // run here; the policy test above proves the drain never reaches it with a
    // bot off its map.

    if (g_failed)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("test_deferred_packet_policy: all checks passed\n");
    return 0;
}
