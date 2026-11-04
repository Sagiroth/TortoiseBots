// Issue #833: players' `.bot` / TBM commands are queued off the chat threads
// and drained on the world thread. Exercises the queue policy standalone.
// Issue #846 adds the cross-queue order with the deferred bot chat / packet
// queue (runtime/WorldThreadQueue.h): the drain runs, before each command,
// everything queued for bots ahead of it (run it under -fsanitize=thread).
#include "../commands/BotCommandQueue.h"

#include <atomic>
#include <cassert>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace TortoiseBots;

namespace {

struct FakePlayer
{
    uint64_t guid;
};

struct Run
{
    uint64_t guid;
    BotCommandReply reply;
    std::string args;
};

} // namespace

int main()
{
    // Arrival order is kept across requesters and transports; nothing runs
    // until the drain, and a drained queue is empty.
    {
        BotCommandQueue queue;
        FakePlayer alice{ 1 }, bob{ 2 };
        assert(queue.Enqueue(1, BotCommandReply::Chat, "stay"));
        assert(queue.Enqueue(2, BotCommandReply::Addon, "list"));
        assert(queue.Enqueue(1, BotCommandReply::Addon, "action follow"));
        assert(queue.PendingCount() == 3);

        std::vector<Run> runs;
        size_t executed = queue.Drain(
            [&](uint64_t guid) -> FakePlayer* { return guid == 1 ? &alice : guid == 2 ? &bob : nullptr; },
            [&](FakePlayer* p, PendingBotCommand const& c) { runs.push_back(Run{ p->guid, c.reply, c.args }); },
            [&](FakePlayer*, DroppedBotCommands const&) { assert(false); });
        assert(executed == 3);
        assert(runs.size() == 3);
        assert(runs[0].guid == 1 && runs[0].reply == BotCommandReply::Chat && runs[0].args == "stay");
        assert(runs[1].guid == 2 && runs[1].reply == BotCommandReply::Addon && runs[1].args == "list");
        assert(runs[2].guid == 1 && runs[2].reply == BotCommandReply::Addon && runs[2].args == "action follow");
        assert(queue.PendingCount() == 0);
    }

    // A command whose bot still holds an earlier chat or packet is deferred to
    // the next drain instead of overtaking it.
    {
        BotCommandQueue queue;
        FakePlayer alice{ 1 };
        queue.Enqueue(1, BotCommandReply::Chat, "action follow");

        std::vector<std::string> ran;
        auto find = [&](uint64_t) -> FakePlayer* { return &alice; };
        auto run = [&](FakePlayer*, PendingBotCommand const& c) { ran.push_back(c.args); };
        auto none = [&](FakePlayer*, DroppedBotCommands const&) { assert(false); };

        size_t executed = queue.Drain(find, run, none,
            [](FakePlayer*, PendingBotCommand const&) { return true; });
        assert(executed == 0);
        assert(ran.empty());
        assert(queue.PendingCount() == 1);

        executed = queue.Drain(find, run, none);
        assert(executed == 1);
        assert((ran == std::vector<std::string>{ "action follow" }));
        assert(queue.PendingCount() == 0);
    }

    // A deferred command keeps its place ahead of a command queued while the
    // same drain ran.
    {
        BotCommandQueue queue;
        FakePlayer alice{ 1 };
        queue.Enqueue(1, BotCommandReply::Chat, "first");

        std::vector<std::string> ran;
        auto find = [&](uint64_t) -> FakePlayer* { return &alice; };
        auto run = [&](FakePlayer*, PendingBotCommand const& c) { ran.push_back(c.args); };
        auto none = [&](FakePlayer*, DroppedBotCommands const&) { assert(false); };

        size_t executed = queue.Drain(find, run, none,
            [&](FakePlayer*, PendingBotCommand const&)
            {
                queue.Enqueue(1, BotCommandReply::Chat, "second");
                return true;
            });
        assert(executed == 0);
        assert(ran.empty());
        assert(queue.PendingCount() == 2);

        executed = queue.Drain(find, run, none);
        assert(executed == 2);
        assert((ran == std::vector<std::string>{ "first", "second" }));
    }

    // Logout before the drain: the requester is no longer found, so the command
    // is dropped (no reply, nothing executed); other players' commands still run.
    {
        BotCommandQueue queue;
        FakePlayer bob{ 2 };
        queue.Enqueue(1, BotCommandReply::Chat, "summon");
        queue.Enqueue(2, BotCommandReply::Chat, "summon");
        std::vector<uint64_t> ran;
        size_t executed = queue.Drain(
            [&](uint64_t guid) -> FakePlayer* { return guid == 2 ? &bob : nullptr; },
            [&](FakePlayer* p, PendingBotCommand const&) { ran.push_back(p->guid); },
            [&](FakePlayer*, DroppedBotCommands const&) { assert(false); });
        assert(executed == 1);
        assert((ran == std::vector<uint64_t>{ 2 }));
        assert(queue.PendingCount() == 0);
    }

    // Per-requester cap: the overflow is dropped, counted and reported once to
    // that requester at the drain; another requester is unaffected. The count
    // resets with the drain.
    {
        BotCommandQueue queue;
        FakePlayer alice{ 1 }, bob{ 2 };
        for (size_t i = 0; i < BotCommandQueue::kMaxPendingPerRequester; ++i)
            assert(queue.Enqueue(1, BotCommandReply::Addon, "status"));
        assert(!queue.Enqueue(1, BotCommandReply::Addon, "status"));
        assert(!queue.Enqueue(1, BotCommandReply::Addon, "status"));
        assert(queue.Enqueue(2, BotCommandReply::Chat, "list"));

        size_t reports = 0;
        size_t executed = queue.Drain(
            [&](uint64_t guid) -> FakePlayer* { return guid == 1 ? &alice : guid == 2 ? &bob : nullptr; },
            [&](FakePlayer*, PendingBotCommand const&) {},
            [&](FakePlayer* p, DroppedBotCommands const& d)
            {
                ++reports;
                assert(p->guid == 1 && d.count == 2 && d.reply == BotCommandReply::Addon);
            });
        assert(executed == BotCommandQueue::kMaxPendingPerRequester + 1);
        assert(reports == 1);
        assert(queue.Enqueue(1, BotCommandReply::Chat, "list"));
    }

    // Dropped-count report for a requester who logged out is discarded too.
    {
        BotCommandQueue queue;
        for (size_t i = 0; i <= BotCommandQueue::kMaxPendingPerRequester; ++i)
            queue.Enqueue(1, BotCommandReply::Chat, "x");
        size_t executed = queue.Drain(
            [&](uint64_t) -> FakePlayer* { return nullptr; },
            [&](FakePlayer*, PendingBotCommand const&) { assert(false); },
            [&](FakePlayer*, DroppedBotCommands const&) { assert(false); });
        assert(executed == 0);
        assert(queue.PendingCount() == 0);
    }

    // A command that queues another command during the drain does not deadlock;
    // the new one waits for the next drain.
    {
        BotCommandQueue queue;
        FakePlayer alice{ 1 };
        queue.Enqueue(1, BotCommandReply::Chat, "first");
        std::vector<std::string> ran;
        auto find = [&](uint64_t) -> FakePlayer* { return &alice; };
        auto none = [&](FakePlayer*, DroppedBotCommands const&) { assert(false); };
        queue.Drain(find,
            [&](FakePlayer*, PendingBotCommand const& c)
            {
                ran.push_back(c.args);
                queue.Enqueue(1, BotCommandReply::Chat, "second");
            },
            none);
        assert((ran == std::vector<std::string>{ "first" }));
        assert(queue.PendingCount() == 1);
        queue.Drain(find, [&](FakePlayer*, PendingBotCommand const& c) { ran.push_back(c.args); }, none);
        assert((ran == std::vector<std::string>{ "first", "second" }));
    }

    // Producers on other threads (map / AsyncPackets) while the world thread
    // drains: every accepted command runs exactly once.
    {
        BotCommandQueue queue;
        constexpr int kThreads = 4;
        constexpr int kPerThread = 2000;
        std::set<std::string> seen;
        size_t executed = 0;
        auto find = [](uint64_t guid) -> uint64_t { return guid; };
        auto run = [&](uint64_t, PendingBotCommand const& c)
        {
            bool inserted = seen.insert(c.args).second;
            assert(inserted);
            (void)inserted;
        };
        auto none = [](uint64_t, DroppedBotCommands const&) { assert(false); };
        std::vector<std::thread> producers;
        for (int t = 0; t < kThreads; ++t)
        {
            producers.emplace_back([&queue, t]
            {
                for (int i = 0; i < kPerThread; ++i)
                {
                    // Distinct requesters per message keep the cap out of the way.
                    uint64_t guid = 1 + static_cast<uint64_t>(t) * kPerThread + i;
                    while (!queue.Enqueue(guid, BotCommandReply::Chat, std::to_string(guid)))
                        std::this_thread::yield();
                }
            });
        }
        while (executed < static_cast<size_t>(kThreads * kPerThread))
            executed += queue.Drain(find, run, none);
        for (std::thread& producer : producers)
            producer.join();
        executed += queue.Drain(find, run, none);
        assert(executed == static_cast<size_t>(kThreads * kPerThread));
        assert(seen.size() == static_cast<size_t>(kThreads * kPerThread));
    }

    // Issue #846: `/p stay` (chat to the bot, deferred chat queue) then
    // `/p .bot action follow` (command queue) in one tick run in that order,
    // as BotChatAdapter::DrainQueuedCommands drives it: before each command,
    // the bot items queued ahead of it; after the commands, the rest.
    {
        BotCommandQueue commands;
        WorldThreadQueue<std::string> chat(64);
        FakePlayer master{ 1 };
        chat.Enqueue(100, "stay");                                   // party chat to bot 100
        commands.Enqueue(1, BotCommandReply::Chat, "action follow"); // `.bot` typed after it
        chat.Enqueue(100, "attack");                                 // typed after the command
        std::vector<std::string> ran;
        auto runChat = [&](uint64_t limit)
        {
            for (auto& entry : chat.TakeBefore(limit))
                ran.push_back("chat:" + entry.item);
        };
        commands.Drain(
            [&](uint64_t guid) -> FakePlayer* { return guid == 1 ? &master : nullptr; },
            [&](FakePlayer*, PendingBotCommand const& c)
            {
                runChat(c.seq);
                ran.push_back("command:" + c.args);
            },
            [&](FakePlayer*, DroppedBotCommands const&) { assert(false); });
        runChat(commands.DeferredRunLimit());
        assert((ran == std::vector<std::string>{ "chat:stay", "command:action follow", "chat:attack" }));
        assert(chat.PendingCount() == 0);
    }

    // Same order when the command's requester logged out: its command is
    // dropped, the chat still runs (at the final drain), in order.
    {
        BotCommandQueue commands;
        WorldThreadQueue<std::string> chat(64);
        chat.Enqueue(100, "a");
        commands.Enqueue(1, BotCommandReply::Chat, "gone");
        chat.Enqueue(100, "b");
        std::vector<std::string> ran;
        commands.Drain([&](uint64_t) -> FakePlayer* { return nullptr; },
            [&](FakePlayer*, PendingBotCommand const&) { assert(false); },
            [&](FakePlayer*, DroppedBotCommands const&) { assert(false); });
        for (auto& entry : chat.TakeBefore(commands.DeferredRunLimit()))
            ran.push_back(entry.item);
        assert((ran == std::vector<std::string>{ "a", "b" }));
    }

    // A command that arrives during the drain holds back the bot items queued
    // after it: they run after it, next tick, not ahead of it at the end of
    // this one. Items queued before it still run this tick.
    {
        BotCommandQueue commands;
        WorldThreadQueue<std::string> chat(64);
        FakePlayer master{ 1 };
        commands.Enqueue(1, BotCommandReply::Chat, "first");
        std::vector<std::string> ran;
        auto find = [&](uint64_t) -> FakePlayer* { return &master; };
        auto none = [&](FakePlayer*, DroppedBotCommands const&) { assert(false); };
        auto runChat = [&](uint64_t limit)
        {
            for (auto& entry : chat.TakeBefore(limit))
                ran.push_back("chat:" + entry.item);
        };
        auto runCommand = [&](FakePlayer*, PendingBotCommand const& c)
        {
            runChat(c.seq);
            ran.push_back("command:" + c.args);
            if (c.args == "first")
            {
                // Another thread, meanwhile: chat, a command, more chat.
                chat.Enqueue(100, "before");
                commands.Enqueue(1, BotCommandReply::Chat, "second");
                chat.Enqueue(100, "after");
            }
        };
        commands.Drain(find, runCommand, none);
        runChat(commands.DeferredRunLimit());
        assert((ran == std::vector<std::string>{ "command:first", "chat:before" }));
        commands.Drain(find, runCommand, none);
        runChat(commands.DeferredRunLimit());
        assert((ran == std::vector<std::string>{ "command:first", "chat:before", "command:second", "chat:after" }));
    }

    // Producer threads (one per master, like AsyncPackets / map threads)
    // alternate bot chat and `.bot` commands while the world thread drains as
    // above: each producer's items run in exactly the order it queued them,
    // across both queues.
    {
        constexpr int kThreads = 4;
        constexpr int kPerThread = 1500;
        BotCommandQueue commands;
        WorldThreadQueue<int> chat(1000000);
        std::atomic<int> done{ 0 };
        std::vector<std::thread> producers;
        for (int t = 0; t < kThreads; ++t)
        {
            producers.emplace_back([&, t]
            {
                uint64_t const master = 1 + static_cast<uint64_t>(t);
                for (int i = 0; i < kPerThread; ++i)
                {
                    if (i % 3 == 1)
                    {
                        while (!commands.Enqueue(master, BotCommandReply::Chat, std::to_string(i)))
                            std::this_thread::yield();
                    }
                    else
                        chat.Enqueue(master, i);
                }
                ++done;
            });
        }
        std::map<uint64_t, int> next;
        size_t total = 0;
        auto see = [&](uint64_t master, int i)
        {
            assert(next[master] == i);
            next[master] = i + 1;
            ++total;
        };
        auto runChat = [&](uint64_t limit)
        {
            for (auto& entry : chat.TakeBefore(limit))
                see(entry.key, entry.item);
        };
        auto drain = [&]
        {
            commands.Drain([](uint64_t guid) -> uint64_t { return guid; },
                [&](uint64_t master, PendingBotCommand const& c)
                {
                    runChat(c.seq);
                    see(master, std::stoi(c.args));
                },
                // A producer over the cap retries the same command, so a
                // drop report changes nothing here.
                [](uint64_t, DroppedBotCommands const&) {});
            runChat(commands.DeferredRunLimit());
        };
        while (done.load() < kThreads)
            drain();
        for (std::thread& producer : producers)
            producer.join();
        drain();
        assert(total == static_cast<size_t>(kThreads * kPerThread));
        for (int t = 0; t < kThreads; ++t)
            assert(next[1 + static_cast<uint64_t>(t)] == kPerThread);
    }

    // Shutdown forgets anything still queued.
    {
        BotCommandQueue queue;
        queue.Enqueue(1, BotCommandReply::Chat, "list");
        queue.Clear();
        assert(queue.PendingCount() == 0);
    }

    return 0;
}
