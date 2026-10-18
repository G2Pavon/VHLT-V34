#pragma once

// Platform glue used by common/ and the tools.

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

#elif defined(__GNUC__) || defined(__clang__)
#include <limits.h>
#include <strings.h>
#include <unistd.h>

#define _MAX_PATH PATH_MAX
#define SYSTEM_SLASH_CHAR '/'
#define SYSTEM_SLASH_STR "/"
#define FORMAT_PRINTF(A, B) __attribute__((format(printf, A, B)))

#endif
