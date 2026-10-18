#include "common/threads.h"

#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <cstdio>
#include <cstring>

#include "common/win32fix.h"
#include "common/cmdlib.h"
#include "common/messages.h"
#include "common/log.h"
#include "common/hlassert.h"

// Recursive, like the CRITICAL_SECTION the Windows tools were written against: a nested
// ThreadLock on the same thread warns instead of deadlocking.
static std::recursive_mutex g_thread_mutex;
static std::atomic<int> g_enter_count{0}; // lock depth; only changed by the thread holding the lock
static std::atomic<bool> g_is_threaded{false};
static std::atomic<bool> g_abort_after_error{false};

static constexpr int MAX_THREADS = 64;
int g_numthreads = DEFAULT_NUMTHREADS;

static q_threadfunction q_entry;

static std::atomic<int> g_dispatch_index{0};
static int g_work_count = 0;
static int g_old_percentage = -1;
static bool g_show_pacifier = false;

static constexpr int THREADTIMES_SIZE = 100;
static constexpr float THREADTIMES_SIZE_F = static_cast<float>(THREADTIMES_SIZE);
static double g_thread_start_time = 0;
static double g_thread_times[THREADTIMES_SIZE];

static q_threadfunction workfunction;

// --- ordered log output (ThreadSetOrderedLog)
namespace
{
struct ItemLog
{
    LogCapture log;
    bool failed = false; // the item stopped with a captured Error()
};

bool s_ordered_log = false; // the tool asked for it
bool s_ordering = false;    // this run captures (ordered log and more than one thread)
std::mutex s_order_mutex;
int s_order_next = 0;                                 // next work item whose output is written
std::map<int, std::unique_ptr<ItemLog>> s_order_done; // finished items waiting for earlier ones
bool s_order_stopped = false;                         // a failed item's output is written: drop the rest

thread_local int t_item = -1; // the work item this thread is running, or -1
thread_local std::unique_ptr<ItemLog> t_itemlog;
thread_local std::optional<ScopedLogCapture> t_itemcapture;
thread_local int t_lockdepth = 0; // ThreadLocks this thread holds
} // namespace

void ThreadSetOrderedLog(bool ordered)
{
    s_ordered_log = ordered;
}

static void BeginItem(int item)
{
    t_item = item;
    t_itemlog = std::make_unique<ItemLog>();
    t_itemcapture.emplace(t_itemlog->log);
}

// Hands the finished item's output over and writes every item's output that is now next in order.
static void EndItem(bool failed)
{
    if (t_item < 0)
    {
        return;
    }
    t_itemcapture.reset();
    t_itemlog->failed = failed;
    std::lock_guard<std::mutex> lock(s_order_mutex);
    s_order_done[t_item] = std::move(t_itemlog);
    t_item = -1;
    while (!s_order_stopped)
    {
        auto next = s_order_done.find(s_order_next);
        if (next == s_order_done.end())
        {
            break;
        }
        next->second->log.Replay();
        if (next->second->failed)
        {
            s_order_stopped = true;
        }
        s_order_done.erase(next);
        s_order_next++;
    }
}

void ThreadLock()
{
    if (!g_is_threaded)
        return;

    g_thread_mutex.lock();
    if (g_enter_count > 0)
    {
        Warning("Recursive ThreadLock detected\n");
    }
    g_enter_count++;
    t_lockdepth++;
}

void ThreadUnlock()
{
    if (!g_is_threaded)
        return;

    if (g_enter_count <= 0)
    {
        Error("ThreadUnlock without corresponding lock\n");
    }
    g_enter_count--;
    t_lockdepth--;
    g_thread_mutex.unlock();
}

static int DispatchThreadWork();

int GetThreadWork()
{
    if (!s_ordering)
    {
        return DispatchThreadWork();
    }
    EndItem(false);
    const int work = DispatchThreadWork();
    if (work != -1)
    {
        BeginItem(work);
    }
    return work;
}

static int DispatchThreadWork()
{
    std::lock_guard<std::recursive_mutex> lock(g_thread_mutex);

    if (g_dispatch_index == 0)
    {
        g_old_percentage = 0;
    }

    if (g_dispatch_index < 0 || g_dispatch_index >= g_work_count || g_abort_after_error)
    {
        return -1;
    }

    int current_percentage = (THREADTIMES_SIZE * g_dispatch_index) / g_work_count;

    if (g_show_pacifier)
    {
        PrintConsole("\r%6d /%6d", g_dispatch_index.load(), g_work_count);

        if (current_percentage != g_old_percentage)
        {
            double current_time = I_FloatTime();
            for (int i = g_old_percentage; i <= current_percentage; i++)
            {
                if (g_thread_times[i] < 1.0)
                {
                    g_thread_times[i] = current_time;
                }
            }
            g_old_percentage = current_percentage;

            if (current_percentage > 10)
            {
                double time_from_start = current_time - g_thread_times[0];
                double est_finish = (time_from_start) * (THREADTIMES_SIZE_F - current_percentage) / current_percentage;
                double est_finish2 = 10.0 * (current_time - g_thread_times[current_percentage - 10]) * (THREADTIMES_SIZE_F - current_percentage) / THREADTIMES_SIZE_F;
                double est_finish3 = THREADTIMES_SIZE_F * (current_time - g_thread_times[current_percentage - 1]) * (THREADTIMES_SIZE_F - current_percentage) / THREADTIMES_SIZE_F;

                if (est_finish > 1.0)
                {
                    PrintConsole("  (%d%%: est. time %ld/%ld/%ld secs)   ",
                                 current_percentage, (long)est_finish, (long)est_finish2, (long)est_finish3);
                }
                else
                {
                    PrintConsole("  (%d%%: est. time <1 sec)   ", current_percentage);
                }
            }
        }
    }
    else
    {
        if (current_percentage != g_old_percentage)
        {
            g_old_percentage = current_percentage;
            if (current_percentage > 0 && current_percentage % 10 == 0)
            {
                PrintConsole("%d%%...", current_percentage);
            }
        }
    }

    return g_dispatch_index++;
}

static void ThreadWorkerFunction(int)
{
    int work;
    while ((work = GetThreadWork()) != -1)
    {
        workfunction(work);
    }
}

void AbortThreadsAfterError()
{
    g_abort_after_error = true;
}

void ThreadSetDefault()
{
    if (g_numthreads == -1)
    {
        unsigned int n = std::thread::hardware_concurrency();
        g_numthreads = (n > 0) ? static_cast<int>(n) : 1;

        if (g_numthreads > MAX_THREADS)
        {
            g_numthreads = MAX_THREADS;
        }
    }
}

void RunThreadsOnIndividual(int workcnt, bool showpacifier, q_threadfunction func, int maxthreads)
{
    workfunction = func;
    RunThreadsOn(workcnt, showpacifier, ThreadWorkerFunction, maxthreads);
}

void RunThreadsOn(int workcnt, bool showpacifier, q_threadfunction func, int maxthreads)
{
    g_thread_start_time = I_FloatTime();

    std::fill(std::begin(g_thread_times), std::end(g_thread_times), 0.0);

    g_dispatch_index = 0;
    g_work_count = workcnt;
    g_old_percentage = -1;
    g_show_pacifier = showpacifier;
    g_is_threaded = true;
    q_entry = func;

    hlassume(g_work_count >= 0, assume_BadWorkcount);

    ThreadSetDefault();
    const int numthreads = maxthreads > 0 ? std::min(maxthreads, g_numthreads) : g_numthreads;

    Log("Running with %d threads\n", numthreads);

    s_ordering = s_ordered_log && numthreads > 1;
    s_order_next = 0;
    s_order_done.clear();
    s_order_stopped = false;

    // RAII: the worker threads live in this vector
    std::vector<std::thread> workers;
    workers.reserve(numthreads);

    for (int i = 0; i < numthreads; i++)
    {
        // a lambda forwards the thread number to the entry function
        workers.emplace_back([i]()
                             {
                                 if (!s_ordering)
                                 {
                                     q_entry(i);
                                     return;
                                 }
                                 try
                                 {
                                     q_entry(i);
                                 }
                                 catch (const CapturedError &)
                                 {
                                     // an Error() in a work item: let the other threads go on to the end of
                                     // their items, hand out no more, and exit once its output is written
                                     while (t_lockdepth > 0)
                                     {
                                         ThreadUnlock();
                                     }
                                     g_abort_after_error = true;
                                     EndItem(true);
                                     return;
                                 }
                                 EndItem(false); // in case q_entry returned in the middle of an item
                             });
    }

    // wait for all threads to finish
    for (auto &t : workers)
    {
        if (t.joinable())
        {
            t.join();
        }
    }

    q_entry = nullptr;
    g_is_threaded = false;
    s_ordering = false;
    if (g_abort_after_error)
    {
        // A work item failed with an error whose output has been written: exit now, from the main
        // thread, as a single-threaded run would have (before the elapsed-time line).
        ExitAfterError();
    }
    double total_time = I_FloatTime() - g_thread_start_time;

    if (g_show_pacifier)
    {
        PrintConsole("\r%60s\r", "");
    }
    Log(" (%.2f seconds)\n", total_time);
}