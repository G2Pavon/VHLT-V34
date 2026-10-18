#pragma once

// Platform glue. The first part is used by common/ and the tools; the "legacy shims" part
// only by the tools named next to each shim, and goes away as those tools are modernized.

#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_MSC_VER) || defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define strncasecmp _strnicmp
#define strcasecmp _stricmp

#define SYSTEM_SLASH_CHAR '\\'
#define SYSTEM_SLASH_STR "\\"
#define FORMAT_PRINTF(A, B)

// legacy shims (MSVC spellings are native here)
#define CDECL __cdecl       // hlrad
#define FORCEINLINE __forceinline // hlrad

#elif defined(__GNUC__) || defined(__clang__)
#include <limits.h>
#include <strings.h>
#include <unistd.h>

#define _MAX_PATH PATH_MAX
#define SYSTEM_SLASH_CHAR '/'
#define SYSTEM_SLASH_STR "/"
#define FORMAT_PRINTF(A, B) __attribute__((format(printf, A, B)))

// legacy shims: MSVC spellings used by the tools
#define CDECL                                         // hlrad
#define FORCEINLINE inline __attribute__((always_inline)) // hlrad

template <size_t size>
static inline int sprintf_s(char (&d)[size], const char *f, ...) // hlrad
{
    va_list a;
    va_start(a, f);
    int r = ::vsnprintf(d, size, f, a);
    va_end(a);
    return r;
}

#endif
