#pragma once

#include "../runtime/WorldThreadQueue.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace TortoiseBots {

// Issue #833: `.bot` commands typed by a player arrive on whatever thread the
// core processes that chat packet on: `/say` on a map thread, party / raid /
// guild / whisper on the AsyncPackets thread, whose window overlaps the world
// hook that ticks the bots. The command layer touches module and bot AI state
// that only the world thread may touch, so the chat and addon adapters hand a
// player's command to this queue and BotHostAdapter::OnUpdate drains it on the
// world thread, before the bot update. Behaviour is unchanged apart from
// timing: a command runs at most one world tick later.
//
// An entry holds the requester's raw guid and a copy of the command text, never
// a Player, session or handler pointer: the drain looks the player up again and
// drops the command when the player has logged out in between.
//
// Plain data and the standard library only, so tools/test_bot_command_queue.cpp
// exercises it standalone.

// Where the drained command's replies go: the player's chat frame (`.bot` typed
// in chat) or the TBM addon channel (the addon transport).
enum class BotCommandReply : uint8_t
{
    Chat,
    Addon,
};

struct PendingBotCommand
{
    // Issue #846: stamped from NextWorldQueueSequence at enqueue, so the drain
    // can first run deferred bot chat and packets queued before this command.
    uint64_t seq = 0;
    uint64_t requesterGuid = 0;
    BotCommandReply reply = BotCommandReply::Chat;
    std::string args;
    // A top-level player command (`.autogear`): args holds the full command
    // text and the drain dispatches it through TryHandleBotCommand instead of
    // the `.bot` subcommand table.
    bool topLevel = false;
};

// Per-requester commands dropped because the requester already had the
// maximum pending; reported back to that player once, on the world thread.
struct DroppedBotCommands
{
    uint64_t requesterGuid = 0;
    BotCommandReply reply = BotCommandReply::Chat;
    size_t count = 0;
};

class BotCommandQueue
{
public:
    // A burst larger than any UI or typing produces in one world tick; it
    // bounds memory if a client floods addon messages (those skip chat flood
    // control).
    static constexpr size_t kMaxPendingPerRequester = 128;

    static BotCommandQueue& Instance()
    {
        static BotCommandQueue queue;
        return queue;
    }

    // Any thread. False when the requester already has the maximum pending;
    // the command is then dropped and counted for one reply at the next drain.
    bool Enqueue(uint64_t requesterGuid, BotCommandReply reply, std::string args, bool topLevel = false)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        size_t& pending = m_pendingPerRequester[requesterGuid];
        if (pending >= kMaxPendingPerRequester)
        {
            DroppedBotCommands& dropped = m_dropped[requesterGuid];
            dropped.requesterGuid = requesterGuid;
            dropped.reply = reply;
            ++dropped.count;
            return false;
        }
        ++pending;
        m_pending.push_back(PendingBotCommand{ NextWorldQueueSequence(), requesterGuid, reply, std::move(args), topLevel });
        return true;
    }

    // World thread. Runs every command queued so far, in arrival order.
    // `find` maps a requester guid to its live player (or a null-like value
    // once the player logged out: the command is then dropped); `run` executes
    // one command for that player; `reportDropped` tells a live player how many
    // of their commands the cap dropped. The lock is released before anything
    // runs, so a command that queues another `.bot` command (or another thread
    // queuing meanwhile) does not deadlock; that command runs on the next drain.
    // Returns the number of commands executed.
    template <class Find, class Run, class ReportDropped>
    size_t Drain(Find find, Run run, ReportDropped reportDropped)
    {
        return Drain(find, run, reportDropped,
            [](auto, PendingBotCommand const&) { return false; });
    }

    // As above, but `shouldDefer` keeps a command for the next drain when it
    // returns true: the command's bot is still waiting on earlier chat or
    // packets, so running it now would overtake them. Deferred commands keep
    // their arrival order ahead of commands queued while the drain ran.
    template <class Find, class Run, class ReportDropped, class ShouldDefer>
    size_t Drain(Find find, Run run, ReportDropped reportDropped, ShouldDefer shouldDefer)
    {
        std::vector<PendingBotCommand> batch;
        std::unordered_map<uint64_t, DroppedBotCommands> dropped;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            batch.swap(m_pending);
            dropped.swap(m_dropped);
            m_pendingPerRequester.clear();
        }

        std::vector<PendingBotCommand> deferred;
        size_t executed = 0;
        for (PendingBotCommand const& command : batch)
        {
            auto requester = find(command.requesterGuid);
            if (!requester)
                continue;
            if (shouldDefer(requester, command))
            {
                deferred.push_back(command);
                continue;
            }
            run(requester, command);
            ++executed;
        }

        if (!deferred.empty())
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            deferred.insert(deferred.end(), m_pending.begin(), m_pending.end());
            m_pending.swap(deferred);
            m_pendingPerRequester.clear();
            for (PendingBotCommand const& command : m_pending)
                ++m_pendingPerRequester[command.requesterGuid];
        }

        for (auto const& entry : dropped)
        {
            auto requester = find(entry.first);
            if (requester)
                reportDropped(requester, entry.second);
        }
        return executed;
    }

    // World thread, at shutdown: forget everything still queued.
    void Clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending.clear();
        m_dropped.clear();
        m_pendingPerRequester.clear();
    }

    size_t PendingCount() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_pending.size();
    }

    // Issue #846: the sequence number (NextWorldQueueSequence) of the oldest
    // command still queued, or UINT64_MAX when none is.
    uint64_t OldestPendingSeq() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_pending.empty() ? UINT64_MAX : m_pending.front().seq;
    }

    // Issue #846: after Drain, the bound below which the deferred bot chat and
    // packets may run this tick: everything issued so far, but nothing queued
    // after a command that arrived during the drain (that command runs first,
    // next tick). The issued bound is read before the queue, so a command
    // stamped below it is seen there.
    uint64_t DeferredRunLimit() const
    {
        uint64_t const issued = WorldQueueSequenceEnd();
        uint64_t const oldest = OldestPendingSeq();
        return oldest < issued ? oldest : issued;
    }

private:
    mutable std::mutex m_mutex;
    std::vector<PendingBotCommand> m_pending;
    std::unordered_map<uint64_t, size_t> m_pendingPerRequester;
    std::unordered_map<uint64_t, DroppedBotCommands> m_dropped;
};

} // namespace TortoiseBots
