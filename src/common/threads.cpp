#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "threads.h"
#include "win32fix.h"
#include "cmdlib.h"
#include "messages.h"
#include "log.h"
#include "blockmem.h"
#include "hlassert.h"

static std::mutex g_thread_mutex;
static std::atomic<int> g_enter_count{0};
static std::atomic<bool> g_is_threaded{false};

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

q_threadfunction workfunction;

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
    g_thread_mutex.unlock();
}

int GetThreadWork()
{
    std::lock_guard<std::mutex> lock(g_thread_mutex);

    if (g_dispatch_index == 0)
    {
        g_old_percentage = 0;
    }

    if (g_dispatch_index < 0 || g_dispatch_index >= g_work_count)
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

void RunThreadsOnIndividual(int workcnt, bool showpacifier, q_threadfunction func)
{
    workfunction = func;
    RunThreadsOn(workcnt, showpacifier, ThreadWorkerFunction);
}

void RunThreadsOn(int workcnt, bool showpacifier, q_threadfunction func)
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

    Log("Running with %d threads\n", g_numthreads);

    // RAII: Los hilos se gestionan en este vector
    std::vector<std::thread> workers;
    workers.reserve(g_numthreads);

    for (int i = 0; i < g_numthreads; i++)
    {
        // Usamos una lambda para invocar la función de entrada
        workers.emplace_back([i]()
                             { q_entry(i); });
    }

    // Esperar a que todos los hilos terminen
    for (auto &t : workers)
    {
        if (t.joinable())
        {
            t.join();
        }
    }

    q_entry = nullptr;
    g_is_threaded = false;
    double total_time = I_FloatTime() - g_thread_start_time;

    if (g_show_pacifier)
    {
        PrintConsole("\r%60s\r", "");
    }
    Log(" (%.2f seconds)\n", total_time);
}