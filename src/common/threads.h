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

int GetThreadWork();

void RunThreadsOnIndividual(int workcnt, bool showpacifier, q_threadfunction);
void ThreadSetPriority(q_threadpriority type);
void ThreadSetDefault();
void ThreadLock();
void ThreadUnlock();

void threads_InitCrit();
void threads_UninitCrit();
void RunThreadsOn(int workcnt, bool showpacifier, q_threadfunction);

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
