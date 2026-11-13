// Standalone check for ai/playerbot/CheckedMutex.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_checked_mutex.cpp -o /tmp/test_checked_mutex && /tmp/test_checked_mutex
//
// The wrapper exists for the World-thread freeze of 2026-11-13: a non-recursive std::mutex locked twice on one thread
// (a read that called finishStandRead while it already held g_standHoldMutex) waited forever. The wrapper asserts on
// the second lock instead. The abort would take this process with it, so the double lock runs in a forked child and
// the parent reads the child's exit status.
//
// The child hangs instead of aborting if the guard is gone (that is what a plain std::mutex does), so the parent
// carries an alarm: a broken guard fails the test either way, as an abort-at-the-right-place test must.
#include "../ai/playerbot/CheckedMutex.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>

#ifndef NDEBUG
namespace
{
int Fail(const char* what)
{
    std::fprintf(stderr, "FAIL: %s\n", what);
    return 1;
}
}
#endif

int main()
{
#ifndef NDEBUG
    // A lock released and taken again by the same thread is the normal case (every FindBossStand read does it) and must
    // not assert.
    {
        CheckedMutex mutex;
        {
            std::lock_guard<CheckedMutex> held(mutex);
        }
        {
            std::lock_guard<CheckedMutex> again(mutex);
        }
    }
    // Two different mutexes nested on one thread are not a self-deadlock either.
    {
        CheckedMutex outer;
        CheckedMutex inner;
        std::lock_guard<CheckedMutex> heldOuter(outer);
        std::lock_guard<CheckedMutex> heldInner(inner);
    }

    std::fflush(nullptr);
    const pid_t child = fork();
    if (child < 0)
        return Fail("fork");
    if (child == 0)
    {
        CheckedMutex mutex;
        std::lock_guard<CheckedMutex> held(mutex);
        std::lock_guard<CheckedMutex> again(mutex);  // the owner locks again: the guard must abort here
        _exit(0);  // reached only if the guard did not fire
    }

    alarm(20);
    int status = 0;
    const pid_t waited = waitpid(child, &status, 0);
    alarm(0);
    if (waited != child)
        return Fail("waitpid");
    if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGABRT)
        return Fail("the second lock by the owner did not abort");

    std::printf("the second lock by the owner aborted in the child\n");
#else
    std::printf("CheckedMutex is std::mutex (NDEBUG): the double-lock guard is not built\n");
#endif
    std::printf("test_checked_mutex OK\n");
    return 0;
}
