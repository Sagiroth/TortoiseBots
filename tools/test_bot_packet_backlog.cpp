// Standalone regression test for the held-packet backlog and the command
// deferral gate (host/BotPacketBacklog.h), the data behind issue #846:
// - a deferred `.bot` command waits while its bot still holds chat or packets
//   queued before it, and a `/p stay` typed before `/p .bot action follow` in
//   one tick is delivered first, so follow lands last as typed (M1: the defer
//   check must come after the drain that moves those items into the backlog);
// - only items queued BEFORE the command defer it: a newer item must not hold
//   the command back until it ages out and is dropped (M2).
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_bot_packet_backlog.cpp -o /tmp/test_bot_packet_backlog
//   /tmp/test_bot_packet_backlog

#include "../host/BotPacketBacklog.h"
#include "../runtime/policy/infra/DeferredDeliveryPolicy.h"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using TortoiseBots::BotPacketBacklog;
using TortoiseBots::DeferCommandForHeldPackets;
using TortoiseBots::DeferredDelivery::Decide;
using TortoiseBots::DeferredDelivery::Verdict;

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

namespace
{
constexpr std::uint64_t kBot = 0x77;
constexpr std::uint64_t kMaster = 0x11;
constexpr std::uint64_t kMaxHeldMs = 3000;
char const* const kStay = "/p stay";
char const* const kFollow = "/p .bot action follow";
// Stands for a CMSG master-incoming opcode (quest accept, gossip, loot roll,
// group removal): none of them is in DeferredDelivery::ReadsBotMap.
constexpr std::uint16_t kMasterIncomingOpcode = 0;

// The adapter's tick, in the shipped order: the defer gate drains the queues
// first (queued chat before the bound goes to the AI, or is held when the bot
// is between maps), then asks the backlog whether the command has anything
// older to wait for.
struct Tick
{
    BotPacketBacklog<std::string> backlog;
    std::vector<std::pair<std::uint64_t, std::string>> queued; // arrival order
    std::vector<std::string> applied;                          // what reached the bot, in order
    bool teleporting = true;
    std::uint64_t nowMs = 0;

    void Drain(std::uint64_t before)
    {
        for (auto it = queued.begin(); it != queued.end(); )
        {
            if (it->first >= before)
            {
                ++it;
                continue;
            }
            if (teleporting)
                backlog.Append(kBot, it->first, it->second, nowMs);
            else
                applied.push_back(it->second);
            it = queued.erase(it);
        }
        backlog.Drain(before, nowMs, kMaxHeldMs,
            [](std::uint64_t) { return false; },
            [this](std::uint64_t) { return !teleporting; },
            [this](std::uint64_t, std::string const& text) { applied.push_back(text); });
    }

    bool ShouldDeferShipped(std::uint64_t commandSeq)
    {
        return DeferCommandForHeldPackets(kMaster, commandSeq,
            [this](std::uint64_t before) { Drain(before); },
            [this](std::uint64_t, std::uint64_t before) { return backlog.AnyBefore({ kBot }, before); });
    }

    // The order before the fix: ask first, drain after.
    bool ShouldDeferCheckFirst(std::uint64_t commandSeq)
    {
        bool const held = backlog.AnyBefore({ kBot }, commandSeq);
        Drain(commandSeq);
        return held;
    }

    // DeliverNowOrHold for a master-incoming packet that arrives on the world
    // thread: behind the bot's backlog when it has one, else the delivery gate
    // decides. The pre-fix direct call (preFixDirect) skipped both and handed
    // the packet to the AI at once.
    void HandOffNow(std::uint64_t seq, std::string const& text, bool preFixDirect)
    {
        if (preFixDirect)
        {
            applied.push_back(text);
            return;
        }
        if (backlog.Contains(kBot))
        {
            backlog.Append(kBot, seq, text, nowMs);
            return;
        }
        switch (Decide(true, teleporting, kMasterIncomingOpcode))
        {
            case Verdict::Deliver:
                applied.push_back(text);
                break;
            case Verdict::Hold:
                backlog.Append(kBot, seq, text, nowMs);
                break;
            case Verdict::Drop:
                break;
        }
    }
};
} // namespace

static void M1FollowWinsWhenChatPrecedesItInOneTick(bool shippedOrder)
{
    Tick tick;
    tick.queued.push_back({ 10, kStay });
    // Tick 1: the master types `/p stay` then `/p .bot action follow` while the
    // bot is between maps.
    bool const deferred = shippedOrder ? tick.ShouldDeferShipped(12) : tick.ShouldDeferCheckFirst(12);
    CHECK(tick.applied.empty());     // the bot is still between maps: nothing reached it
    CHECK(deferred == shippedOrder); // shipped: the command waits; broken order: it runs now
    if (!deferred)
        tick.applied.push_back(kFollow);

    // Tick 2: the teleport landed. The held `stay` is delivered, then the
    // command runs.
    ++tick.nowMs;
    tick.teleporting = false;
    if (shippedOrder ? !tick.ShouldDeferShipped(12) : !tick.ShouldDeferCheckFirst(12))
        tick.applied.push_back(kFollow);

    CHECK(tick.applied.size() == 2);
    if (shippedOrder)
    {
        // The order the master typed: stay first, follow last.
        CHECK(tick.applied[0] == kStay);
        CHECK(tick.applied[1] == kFollow);
    }
    else
    {
        // The pre-fix order overtook `stay`: follow ran first, stay landed on
        // top of it. This is the regression the shipped order fixes.
        CHECK(tick.applied[0] == kFollow);
        CHECK(tick.applied[1] == kStay);
    }
}

// The master's packet (direct hand-off, world thread) during a far teleport,
// behind an older packet that is already held: both reach the bot in arrival
// order once the teleport lands. The pre-fix direct call delivered the newer
// packet first, while the bot was still between maps.
static void M3MasterIncomingWaitsBehindHeldPacket(bool preFixDirect)
{
    Tick tick;
    tick.queued.push_back({ 10, "older" });
    // Tick 1: still between maps; the older packet is drained and held.
    tick.Drain(11);
    CHECK(tick.applied.empty());
    CHECK(tick.backlog.Contains(kBot));

    // The newer master packet arrives on the world thread, still between maps.
    tick.HandOffNow(11, "direct", preFixDirect);
    CHECK(tick.applied.empty() == !preFixDirect); // shipped: held; pre-fix: overtook

    // Tick 2: the teleport landed. The held packet, then the direct one.
    ++tick.nowMs;
    tick.teleporting = false;
    tick.Drain(20);
    CHECK(tick.applied.size() == 2);
    if (preFixDirect)
    {
        CHECK(tick.applied[0] == "direct");
        CHECK(tick.applied[1] == "older");
    }
    else
    {
        CHECK(tick.applied[0] == "older");
        CHECK(tick.applied[1] == "direct");
    }
    CHECK(tick.backlog.Empty());
}

static void M2OnlyOlderHeldItemsDeferACommand()
{
    BotPacketBacklog<std::string> backlog;
    // A command of seq 10 is deferred. A later invite (seq 12) joined the same
    // bot's backlog through another drain: it must not hold the command.
    backlog.Append(kBot, 12, "invite", 0);
    CHECK(!backlog.AnyBefore({ kBot }, 10));
    CHECK(backlog.HasBefore(kBot, 13)); // ... it does hold a command queued after it

    // An older item behind the newer one cannot happen (arrival order), and an
    // older front does defer the command.
    backlog.Append(kBot, 14, "later invite", 0);
    CHECK(backlog.HasBefore(kBot, 13)); // front 12 < 13
    CHECK(!backlog.HasBefore(kBot, 12)); // front 12 is not before 12

    // The drain with the command's bound delivers nothing queued after it.
    std::vector<std::string> applied;
    backlog.Drain(10, 0, kMaxHeldMs, [](std::uint64_t) { return false; },
        [](std::uint64_t) { return true; },
        [&applied](std::uint64_t, std::string const& text) { applied.push_back(text); });
    CHECK(applied.empty());
    CHECK(backlog.Contains(kBot));

    // Once an older item exists, the defer check sees it and the drain
    // delivers it before the bound; the newer entries wait.
    BotPacketBacklog<std::string> ordered;
    ordered.Append(kBot, 8, "stay", 0);
    ordered.Append(kBot, 12, "invite", 0);
    CHECK(ordered.AnyBefore({ kBot }, 10));
    ordered.Drain(10, 0, kMaxHeldMs, [](std::uint64_t) { return false; },
        [](std::uint64_t) { return true; },
        [&applied](std::uint64_t, std::string const& text) { applied.push_back(text); });
    CHECK(applied.size() == 1 && applied.front() == "stay");
    CHECK(ordered.HasBefore(kBot, 13)); // the invite waits for its own command
    CHECK(!ordered.HasBefore(kBot, 12));
}

static void BacklogDropsStaleAndGoneEntriesInOrder()
{
    BotPacketBacklog<std::string> backlog;
    backlog.Append(kBot, 1, "old", 0);
    backlog.Append(kBot, 2, "new", 2500);
    std::vector<std::string> applied;
    // 3.5 s on: the first entry is stale, the second is not.
    backlog.Drain(100, 3500, kMaxHeldMs, [](std::uint64_t) { return false; },
        [](std::uint64_t) { return true; },
        [&applied](std::uint64_t, std::string const& text) { applied.push_back(text); });
    CHECK(applied.size() == 1 && applied.front() == "new");
    CHECK(backlog.Empty());

    // A bot whose AI left the storage takes its backlog with it.
    BotPacketBacklog<std::string> gone;
    gone.Append(kBot, 1, "stay", 0);
    gone.Drain(100, 0, kMaxHeldMs, [](std::uint64_t) { return true; },
        [](std::uint64_t) { return true; },
        [](std::uint64_t, std::string const&) {});
    CHECK(gone.Empty());
}

int main()
{
    M1FollowWinsWhenChatPrecedesItInOneTick(true);
    M1FollowWinsWhenChatPrecedesItInOneTick(false);
    M2OnlyOlderHeldItemsDeferACommand();
    M3MasterIncomingWaitsBehindHeldPacket(false);
    M3MasterIncomingWaitsBehindHeldPacket(true);
    BacklogDropsStaleAndGoneEntriesInOrder();

    if (g_failed)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("test_bot_packet_backlog: all checks passed\n");
    return 0;
}
