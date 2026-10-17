#pragma once

#include "common/log.h"

static constexpr int DEFAULT_NUMTHREADS = -1;

typedef void (*q_threadfunction)(int);

extern int g_numthreads;

void ThreadSetDefault();

void ThreadLock();

void ThreadUnlock();

// maxthreads > 0 limits the number of worker threads (1: the work runs in order on one thread).
void RunThreadsOn(int workcnt, bool showpacifier, q_threadfunction func, int maxthreads = 0);

void RunThreadsOnIndividual(int workcnt, bool showpacifier, q_threadfunction func, int maxthreads = 0);

int GetThreadWork();

// From a work item: stop handing out work; once the workers have finished, RunThreadsOn exits via
// ExitAfterError() instead of returning. Used when an Error() was captured (see LogCapture) and its
// output has been written.
void AbortThreadsAfterError();

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

// Same log output as NamedRunThreadsOnIndividual, but the work items run in order on one thread.
#define NamedRunThreadsOnIndividualInOrder(n, p, f) \
    {                                               \
        Log("%s\n", #f ":");                        \
        RunThreadsOnIndividual(n, p, f, 1);         \
    }
