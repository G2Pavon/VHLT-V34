#pragma once

#include "common/log.h"

static constexpr int DEFAULT_NUMTHREADS = -1;

using q_threadfunction = void (*)(int);

extern int g_numthreads;

void ThreadSetDefault();

void ThreadLock();

void ThreadUnlock();

// maxthreads > 0 limits the number of worker threads (1: the work runs in order on one thread).
void RunThreadsOn(int workcnt, bool showpacifier, q_threadfunction func, int maxthreads = 0);

void RunThreadsOnIndividual(int workcnt, bool showpacifier, q_threadfunction func, int maxthreads = 0);

int GetThreadWork();

// With ordered log on (off by default), a multi-threaded RunThreadsOn* writes exactly the log that
// -threads 1 writes: the output of each work item (from GetThreadWork handing it out until the thread
// asks for the next one) is captured (see LogCapture) and written in work order, as soon as every
// earlier item's has been. A captured Error() ends the run after the output of the items before it
// (the items after it are dropped, as they would never have run), from the main thread. Output
// written outside a work item (before the first or after the last GetThreadWork of a thread) is not
// ordered. With one thread nothing is captured: the work runs and logs as it always did.
void ThreadSetOrderedLog(bool ordered);

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
