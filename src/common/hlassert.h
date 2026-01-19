#pragma once

#include "cmdlib.h" //--vluzacn

#define assume(exp, message)                                                                      \
    {                                                                                             \
        if (!(exp))                                                                               \
        {                                                                                         \
            Error("\nAssume '%s' failed\n at %s:%d\n %s\n\n", #exp, __FILE__, __LINE__, message); \
        }                                                                                         \
    }
#define hlassert(exp)