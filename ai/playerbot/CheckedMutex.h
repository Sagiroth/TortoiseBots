#pragma once

// A mutex that turns a self-deadlock into an abort in debug and test builds.
//
// A non-recursive std::mutex locked twice on one thread hangs that thread for good: the second lock waits for the first
// one's release, which only the same thread could do. 2026-11-13 cost a live server its World thread that way
// (g_standHoldMutex in BossTacticsActions.cpp: finishStandRead locked the mutex a read already held). This wrapper
// checks the owner before it blocks, so the second lock asserts at the line instead of waiting.
//
// Release builds are the plain std::mutex: with NDEBUG the alias below makes every lock_guard<CheckedMutex> a
// lock_guard<std::mutex> with no extra member and no extra store. Debug builds (no NDEBUG) and the standalone
// tools/test_*.cpp compiles carry the guard; tools/test_checked_mutex.cpp proves it fires.

#include <cassert>
#include <mutex>
#include <thread>

#ifdef NDEBUG

using CheckedMutex = std::mutex;

#else

class CheckedMutex
{
public:
    void lock()
    {
        {
            std::lock_guard<std::mutex> guard(m_ownerMutex);
            assert(m_owner != std::this_thread::get_id() &&
                   "CheckedMutex: the calling thread already owns this mutex (self-deadlock)");
        }
        m_mutex.lock();
        std::lock_guard<std::mutex> guard(m_ownerMutex);
        m_owner = std::this_thread::get_id();
    }

    void unlock()
    {
        {
            std::lock_guard<std::mutex> guard(m_ownerMutex);
            m_owner = std::thread::id();
        }
        m_mutex.unlock();
    }

private:
    std::mutex m_mutex;
    // The owner is read before the wait and written after the acquire: a thread that already owns m_mutex sees itself
    // here, any other waiter sees the owner's id (or none) and blocks as usual.
    std::mutex m_ownerMutex;
    std::thread::id m_owner;
};

#endif
