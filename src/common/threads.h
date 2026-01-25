#pragma once

#include "log.h"

constexpr int MAX_THREADS = 64;

typedef enum
{
    eThreadPriorityLow = -1,
    eThreadPriorityNormal,
    eThreadPriorityHigh
} q_threadpriority;

typedef void (*q_threadfunction)(int);

constexpr int DEFAULT_NUMTHREADS = -1;

constexpr q_threadpriority DEFAULT_THREAD_PRIORITY = eThreadPriorityNormal;

extern int g_numthreads;
extern q_threadpriority g_threadpriority;

extern void ThreadSetPriority(q_threadpriority type);
extern void ThreadSetDefault();
extern int GetThreadWork();
extern void ThreadLock();
extern void ThreadUnlock();

extern void RunThreadsOnIndividual(int workcnt, bool showpacifier, q_threadfunction);
extern void RunThreadsOn(int workcnt, bool showpacifier, q_threadfunction);

#define NamedRunThreadsOn(n, p, f) \
    {                              \
        Log("%s\n", #f ":");       \
        RunThreadsOn(n, p, f);     \
    }
#define NamedRunThreadsOnIndividual(n, p, f) \
    {                                        \
        Log("%s\n", #f ":");                 \
        RunThreadsOnIndividual(n, p, f);     \
    }
