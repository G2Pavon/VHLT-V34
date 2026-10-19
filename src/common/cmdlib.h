#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "common/win32fix.h"

//#define MODIFICATIONS_STRING "Submit detailed bug reports to (zoner@gearboxsoftware.com)\n"
//#define MODIFICATIONS_STRING "Submit detailed bug reports to (merlinis@bigpond.net.au)\n"
//#define MODIFICATIONS_STRING "Submit detailed bug reports to (amckern@yahoo.com)\n"
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
[[nodiscard]] std::string PathWithDefaultExtension(std::string_view path, std::string_view extension); // adds it only if there is none
[[nodiscard]] std::string PathWithoutExtension(std::string_view path);
[[nodiscard]] std::string PathDirectory(std::string_view path); // keeps the trailing slash; "" if there is none
[[nodiscard]] std::string PathFile(std::string_view path);

// In-place and into-buffer versions for fixed-size char arrays. They exit with an error instead
// of overflowing the array; passing a bare pointer does not compile.
char *FlipSlashes(char *string); // to SYSTEM_SLASH_CHAR
void StripExtension(char *path); // shrinks only
void CopyPathResult(const std::string &value, char *dest, std::size_t size, const char *what);

template <std::size_t N>
void DefaultExtension(char (&path)[N], const char *extension)
{
    CopyPathResult(PathWithDefaultExtension(path, extension), path, N, "DefaultExtension");
}

template <std::size_t N>
void ExtractFilePath(const char *const path, char (&dest)[N])
{
    CopyPathResult(PathDirectory(path), dest, N, "ExtractFilePath");
}

template <std::size_t N>
void ExtractFile(const char *const path, char (&dest)[N])
{
    CopyPathResult(PathFile(path), dest, N, "ExtractFile");
}

// Bounded string functions: count is the size of dest. dest is always NUL-terminated; the
// result is false if the text had to be cut.
bool FORMAT_PRINTF(3, 4) safe_snprintf(char *const dest, const std::size_t count, const char *const args, ...);
bool safe_strncpy(char *const dest, const char *const src, const std::size_t count);
bool safe_strncat(char *const dest, const char *const src, const std::size_t count);

// True if buffer[0..size) contains a NUL.
bool TerminatedString(const char *buffer, const int size);
