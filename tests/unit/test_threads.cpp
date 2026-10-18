#include "doctest/doctest.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "common/log.h"
#include "common/threads.h"

namespace
{
std::vector<std::atomic<int>> g_hits(1000);
std::atomic<int> g_workers{0};
int g_locked_sum = 0;

void CountWork(int i)
{
    g_hits[i]++;
    ThreadLock();
    g_locked_sum += i;
    ThreadUnlock();
}

void CountWorker(int)
{
    g_workers++;
}

std::atomic<int> g_nested{0};
std::vector<int> g_order;

void RecordOrder(int i)
{
    g_order.push_back(i); // one thread: no lock needed
}

void NestedLock(int)
{
    ThreadLock();
    ThreadLock(); // recursive: warns instead of deadlocking
    g_nested++;
    ThreadUnlock();
    ThreadUnlock();
}
std::vector<int> g_logged; // appended to by LogInOrder actions, which run one at a time

// Items finish out of order: early items take longest.
void SleepThenLog(int i)
{
    std::this_thread::sleep_for(std::chrono::microseconds((200 - i) * 20));
    LogInOrder([i]()
               { g_logged.push_back(i); });
}

void LoopThenLog(int)
{
    int i;
    while ((i = GetThreadWork()) != -1)
    {
        SleepThenLog(i);
    }
}

#ifndef _WIN32
int g_pipe = -1;

void WriteOrFail(int i)
{
    std::this_thread::sleep_for(std::chrono::microseconds((100 - i) * 20));
    LogInOrder([i]()
               {
                   const std::string text = std::to_string(i) + " ";
                   [[maybe_unused]] ssize_t n = write(g_pipe, text.data(), text.size()); });
    if (i == 37)
    {
        ThreadLock(); // held when Error() throws: the runner must release it, or the others deadlock
        Error("item %d failed", i);
    }
}
#endif
} // namespace

TEST_SUITE("threads")
{
    TEST_CASE("RunThreadsOnIndividual hands out every work item exactly once")
    {
        const int saved = g_numthreads;
        g_numthreads = 4;
        for (auto &h : g_hits)
        {
            h = 0;
        }
        g_locked_sum = 0;
        RunThreadsOnIndividual(static_cast<int>(g_hits.size()), false, CountWork);
        for (size_t i = 0; i < g_hits.size(); i++)
        {
            CAPTURE(i);
            CHECK(g_hits[i] == 1);
        }
        CHECK(g_locked_sum == 999 * 1000 / 2);

        RunThreadsOnIndividual(0, false, CountWork); // no work is fine
        g_numthreads = saved;
    }

    TEST_CASE("RunThreadsOn calls the function once per thread")
    {
        const int saved = g_numthreads;
        g_numthreads = 3;
        g_workers = 0;
        RunThreadsOn(10, false, CountWorker);
        CHECK(g_workers == 3);
        g_numthreads = saved;
    }

    TEST_CASE("a nested ThreadLock on the same thread does not deadlock")
    {
        const int saved = g_numthreads;
        g_numthreads = 4;
        g_nested = 0;
        RunThreadsOnIndividual(16, false, NestedLock);
        CHECK(g_nested == 16);
        g_numthreads = saved;
    }

    TEST_CASE("maxthreads 1 runs the work items in order")
    {
        const int saved = g_numthreads;
        g_numthreads = 8;
        g_order.clear();
        RunThreadsOnIndividual(50, false, RecordOrder, 1);
        REQUIRE(g_order.size() == 50);
        for (int i = 0; i < 50; i++)
        {
            CHECK(g_order[i] == i);
        }
        g_numthreads = saved;
    }

    TEST_CASE("log capture holds output back; Error() throws instead of exiting")
    {
        LogCapture capture;
        bool thrown = false;
        {
            ScopedLogCapture scope(capture);
            Log("captured line\n");
            Warning("captured warning");
            try
            {
                Error("captured error %d", 42);
            }
            catch (const CapturedError &)
            {
                thrown = true;
            }
        }
        CHECK(thrown);
        CHECK(!capture.empty());
        LogCapture nested_outer, nested_inner;
        {
            ScopedLogCapture outer(nested_outer);
            {
                ScopedLogCapture inner(nested_inner);
                Log("inner\n");
            }
            CHECK(nested_outer.empty()); // the inner capture took it
            Log("outer\n");
        }
        CHECK(!nested_outer.empty());
        capture.Replay(); // prints the three messages (no log file is open in the tests)
    }

    TEST_CASE("ordered log: output is written in work order whatever the thread count")
    {
        const int saved = g_numthreads;
        ThreadSetOrderedLog(true);
        for (int threads : {1, 8})
        {
            CAPTURE(threads);
            g_numthreads = threads;
            g_logged.clear();
            RunThreadsOnIndividual(200, false, SleepThenLog);
            REQUIRE(g_logged.size() == 200);
            for (int i = 0; i < 200; i++)
            {
                CHECK(g_logged[i] == i);
            }
            g_logged.clear();
            RunThreadsOn(200, false, LoopThenLog); // items handed out by GetThreadWork in the function
            REQUIRE(g_logged.size() == 200);
            for (int i = 0; i < 200; i++)
            {
                CHECK(g_logged[i] == i);
            }
        }
        ThreadSetOrderedLog(false);
        g_numthreads = saved;
    }

#ifndef _WIN32
    TEST_CASE("ordered log: an Error() in a work item ends the run after the items before it")
    {
        int fds[2];
        REQUIRE(pipe(fds) == 0);
        const pid_t child = fork();
        REQUIRE(child >= 0);
        if (child == 0)
        {
            close(fds[0]);
            g_pipe = fds[1];
            alarm(30); // a deadlock fails the test instead of hanging it
            g_numthreads = 8;
            ThreadSetOrderedLog(true);
            RunThreadsOnIndividual(100, false, WriteOrFail);
            _exit(0); // not reached: the run exits with status 1
        }
        close(fds[1]);
        std::string output;
        char buffer[256];
        ssize_t n;
        while ((n = read(fds[0], buffer, sizeof(buffer))) > 0)
        {
            output.append(buffer, n);
        }
        close(fds[0]);
        int status = 0;
        REQUIRE(waitpid(child, &status, 0) == child);
        REQUIRE(WIFEXITED(status));
        CHECK(WEXITSTATUS(status) == 1);
        std::string expected;
        for (int i = 0; i <= 37; i++)
        {
            expected += std::to_string(i) + " ";
        }
        CHECK(output == expected);
    }
#endif

    TEST_CASE("ThreadSetDefault picks a positive thread count when unset")
    {
        const int saved = g_numthreads;
        g_numthreads = DEFAULT_NUMTHREADS;
        ThreadSetDefault();
        CHECK(g_numthreads >= 1);
        CHECK(g_numthreads <= 64);
        g_numthreads = saved;
    }
}
