#pragma once

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cctype>

#if defined(_MSC_VER) || defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define strncasecmp _strnicmp
#define strcasecmp _stricmp

#define STDCALL __stdcall
#define FASTCALL __fastcall
#define CDECL __cdecl
#define INLINE __inline
#define FORCEINLINE __forceinline

#define SYSTEM_SLASH_CHAR '\\'
#define SYSTEM_SLASH_STR "\\"
#define FORMAT_PRINTF(A, B)

#elif defined(__GNUC__) || defined(__clang__)
#include <limits.h>
#include <stdarg.h>
#include <strings.h>
#include <unistd.h>
#include <sys/time.h>

#define _MAX_PATH PATH_MAX
#define MAX_PATH PATH_MAX
#define _unlink unlink
#define _strdup strdup
#define SYSTEM_SLASH_CHAR '/'
#define SYSTEM_SLASH_STR "/"

#define STDCALL
#define FASTCALL
#define CDECL
#define INLINE inline
#define FORCEINLINE inline __attribute__((always_inline))
#define FORMAT_PRINTF(A, B) __attribute__((format(printf, A, B)))

typedef struct
{
    uint32_t dwLowDateTime;
    uint32_t dwHighDateTime;
} FILETIME;
struct timeb
{
    time_t time;
    unsigned short millitm;
    short timezone;
    short dstflag;
};

static inline char *_strlwr(char *s)
{
    if (s)
        for (char *p = s; *p; ++p)
            *p = tolower((unsigned char)*p);
    return s;
}
static inline char *_strupr(char *s)
{
    if (s)
        for (char *p = s; *p; ++p)
            *p = toupper((unsigned char)*p);
    return s;
}

static inline int strcpy_s(char *d, size_t s, const char *src)
{
    if (!d || !src || s == 0)
        return 22;
    strncpy(d, src, s - 1);
    d[s - 1] = '\0';
    return 0;
}
template <size_t size>
static inline int strcpy_s(char (&d)[size], const char *s) { return strcpy_s(d, size, s); }

static inline int sprintf_s(char *d, size_t s, const char *f, ...)
{
    va_list a;
    va_start(a, f);
    int r = ::vsnprintf(d, s, f, a);
    va_end(a);
    return r;
}

template <size_t size>
static inline int sprintf_s(char (&d)[size], const char *f, ...)
{
    va_list a;
    va_start(a, f);
    int r = ::vsnprintf(d, size, f, a);
    va_end(a);
    return r;
}

static inline uint32_t GetModuleFileName(void *, char *f, uint32_t s)
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

static inline void GetSystemTimeAsFileTime(FILETIME *ft)
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    uint64_t t = ((uint64_t)tv.tv_sec + 11644473600ULL) * 10000000ULL + (tv.tv_usec * 10ULL);
    ft->dwLowDateTime = (uint32_t)t;
    ft->dwHighDateTime = (uint32_t)(t >> 32);
}

static inline void ftime(struct timeb *tb)
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    tb->time = tv.tv_sec;
    tb->millitm = tv.tv_usec / 1000;
    tb->timezone = 0;
    tb->dstflag = 0;
}

#endif