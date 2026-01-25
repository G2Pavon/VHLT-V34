#pragma once

#pragma warning(disable : 4996)
#pragma warning(disable : 4127) // conditional expression is constant
#pragma warning(disable : 4115) // named type definition in parentheses
#pragma warning(disable : 4244) // conversion from 'type' to type', possible loss of data
// AJM
#pragma warning(disable : 4786) // identifier was truncated to '255' characters in the browser information
#pragma warning(disable : 4305) // truncation from 'const double' to 'float'
#pragma warning(disable : 4800) // forcing value to bool 'true' or 'false' (performance warning)

#include <cstddef>

#include "win32fix.h"

#define SYSTEM_SLASH_CHAR '\\'
#define SYSTEM_SLASH_STR "\\"

// the dec offsetof macro doesn't work very well...
#define myoffsetof(type, identifier) ((std::size_t)&((type *)0)->identifier)
#define sizeofElement(type, identifier) (sizeof((type *)0)->identifier)

//#define MODIFICATIONS_STRING "Submit detailed bug reports to (zoner@gearboxsoftware.com)\n"
//#define MODIFICATIONS_STRING "Submit detailed bug reports to (merlinis@bigpond.net.au)\n"
//#define MODIFICATIONS_STRING "Submit detailed bug reports to (amckern@yahoo.com)\n"
#define MODIFICATIONS_STRING "Submit detailed bug reports to (vluzacn@163.com)\n" //--vluzacn

#define ZHLT_VERSIONSTRING "v3.4"
#define HACK_VERSIONSTRING "VL34" //--vluzacn

#if !defined(HLCSG) && !defined(HLBSP) && !defined(HLVIS) && !defined(HLRAD) && !defined(RIPENT) //--vluzacn
#error "You must define one of these in the settings of each project: HLCSG, HLBSP, HLVIS, HLRAD, RIPENT. The most likely cause is that you didn't load the project from the sln file."
#endif

#define PLATFORM_VERSIONSTRING "64-bit"

double I_FloatTime();
const char *stristr(const char *const string, const char *const substring);

char *FlipSlashes(char *string);
void DefaultExtension(char *path, const char *extension);
void StripExtension(char *path);
void ExtractFilePath(const char *const path, char *dest);
void ExtractFile(const char *const path, char *dest);

bool CDECL FORMAT_PRINTF(3, 4) safe_snprintf(char *const dest, const std::size_t count, const char *const args, ...);
bool safe_strncpy(char *const dest, const char *const src, const std::size_t count);
bool safe_strncat(char *const dest, const char *const src, const std::size_t count);
bool TerminatedString(const char *buffer, const int size);