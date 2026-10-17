#include "doctest/doctest.h"

#include <atomic>
#include <vector>

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

void NestedLock(int)
{
    ThreadLock();
    ThreadLock(); // recursive: warns instead of deadlocking
    g_nested++;
    ThreadUnlock();
    ThreadUnlock();
}
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
