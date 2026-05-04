#pragma once

#include "common/log.h"

static constexpr int DEFAULT_NUMTHREADS = -1;

typedef void (*q_threadfunction)(int);

extern int g_numthreads;

void ThreadSetDefault();

void ThreadLock();

void ThreadUnlock();

void RunThreadsOn(int workcnt, bool showpacifier, q_threadfunction func);

void RunThreadsOnIndividual(int workcnt, bool showpacifier, q_threadfunction func);

int GetThreadWork();

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