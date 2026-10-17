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
#define FASTCALL __fastcall // hlvis
#define CDECL __cdecl       // hlcsg, hlrad
#define INLINE __inline     // hlvis
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
#define FASTCALL                                      // hlvis
#define CDECL                                         // hlcsg, hlrad
#define INLINE inline                                 // hlvis
#define FORCEINLINE inline __attribute__((always_inline)) // hlrad
#define _strdup strdup                                // hlcsg, common/bspfile.cpp (ParseEpair, Phase 5)

static inline char *_strupr(char *s) // hlcsg
{
    if (s)
        for (char *p = s; *p; ++p)
            *p = toupper((unsigned char)*p);
    return s;
}

static inline int strcpy_s(char *d, size_t s, const char *src) // hlcsg, common/tokenizer.cpp (Phase 5)
{
    if (!d || !src || s == 0)
        return 22;
    strncpy(d, src, s - 1);
    d[s - 1] = '\0';
    return 0;
}
template <size_t size>
static inline int strcpy_s(char (&d)[size], const char *s) { return strcpy_s(d, size, s); }

template <size_t size>
static inline int sprintf_s(char (&d)[size], const char *f, ...) // hlcsg, hlrad
{
    va_list a;
    va_start(a, f);
    int r = ::vsnprintf(d, size, f, a);
    va_end(a);
    return r;
}

static inline uint32_t GetModuleFileName(void *, char *f, uint32_t s) // hlbsp
{
    ssize_t len = readlink("/proc/self/exe", f, s - 1);
    if (len != -1)
    {
        f[len] = '\0';
        return (uint32_t)len;
    }
    if (s > 0)
        f[0] = '\0';
    return 0;
}

#endif
