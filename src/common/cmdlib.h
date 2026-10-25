#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <string_view>

#include "common/win32fix.h"

#define MODIFICATIONS_STRING "Submit detailed bug reports to (vluzacn@163.com)\n" //--vluzacn

#define ZHLT_VERSIONSTRING "v3.4"
#define HACK_VERSIONSTRING "VL34" //--vluzacn

// The build date shown in the banner and stored in worldspawn's "compiler" key, so it ends up in
// every .bsp. Defaults to __DATE__; set it (CMake VHLT_BUILD_DATE, same "Mmm dd yyyy" format) for
// reproducible builds - the parity harness pins it to the oracle's date.
#ifndef VHLT_BUILD_DATE
#define VHLT_BUILD_DATE __DATE__
#endif

#define PLATFORM_VERSIONSTRING "64-bit"

// Seconds on a monotonic clock; only differences are meaningful.
double I_FloatTime();

// Case-insensitive strstr; returns a pointer into string.
const char *stristr(const char *const string, const char *const substring);

// Path helpers. Either slash separates directories. compat: the extension is what follows the
// last '.' of the file name, and a path that ends in a slash names a directory, so its "file" is
// the last directory with the slash ("a/b/" -> path "a/", file "b/").
// These stay plain string code rather than std::filesystem::path: on POSIX fs::path doesn't split
// at '\' (so "valve\halflife.wad" would have no directory, and that file name is written into
// the BSP's "wad" key), and it treats ".map", "a/b/" and the trailing slash differently.
[[nodiscard]] std::string PathWithDefaultExtension(std::string_view path, std::string_view extension); // adds it only if there is none
[[nodiscard]] std::string PathWithoutExtension(std::string_view path);
[[nodiscard]] std::string PathDirectory(std::string_view path); // keeps the trailing slash; "" if there is none
[[nodiscard]] std::string PathFile(std::string_view path);
[[nodiscard]] std::string PathWithSystemSlashes(std::string_view path); // either slash -> SYSTEM_SLASH_CHAR

// Binary search returning the element glibc's bsearch() returns: it probes (lo + hi) / 2 and stops
// at the first equal element it lands on, which among equal elements isn't necessarily the first.
// compare(element) is < 0, 0 or > 0 as the key sorts before, equal to or after element.
// compat: when a wad holds the same texture name twice, the probe order picks the copy hlrad loads,
// so it is written out here rather than left to the C library (other bsearch implementations probe
// differently).
template <typename T, typename Compare>
T *ProbeSearch(T *first, std::size_t count, Compare compare)
{
    std::size_t lo = 0;
    std::size_t hi = count;
    while (lo < hi)
    {
        const std::size_t mid = (lo + hi) / 2;
        const int c = compare(first[mid]);
        if (c < 0)
            hi = mid;
        else if (c > 0)
            lo = mid + 1;
        else
            return first + mid;
    }
    return nullptr;
}

// atoi, the way glibc does it: blanks, a sign and decimal digits, stopping at anything else; 0 if
// there are none. compat: atoi is undefined past INT_MAX. glibc computes it as (int)strtol, which
// on 64-bit Linux keeps the low 32 bits ("4294967298" is 2), while a 32-bit long (Windows) clamped to
// INT_MAX. strtoll plus the C++20 int conversion (defined, modulo 2^32) gives glibc's result
// everywhere.
inline int ParseInt(const char *const text)
{
    return static_cast<int>(std::strtoll(text, nullptr, 10));
}

// printf-style formatting into a std::string, without a length limit.
std::string FORMAT_PRINTF(1, 2) StringPrintf(const char *const format, ...);
std::string StringVPrintf(const char *const format, va_list args);

// Bounded string functions: count is the size of dest. dest is always NUL-terminated; the
// result is false if the text had to be cut.
bool FORMAT_PRINTF(3, 4) safe_snprintf(char *const dest, const std::size_t count, const char *const args, ...);
bool safe_strncpy(char *const dest, const char *const src, const std::size_t count);
bool safe_strncat(char *const dest, const char *const src, const std::size_t count);

// True if buffer[0..size) contains a NUL.
bool TerminatedString(const char *buffer, const int size);
