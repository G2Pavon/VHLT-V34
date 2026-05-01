#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cstdlib>
#include <cstdint>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "cmdlib.h"
#include "messages.h"
#include "hlassert.h"
#include "blockmem.h"
#include "log.h"
#include "mathlib.h"

#define PATHSEPARATOR(c) ((c) == '\\' || (c) == '/')

/*
 * ================
 * I_FloatTime
 * ================
 */

double I_FloatTime()
{
    FILETIME ftime;

    GetSystemTimeAsFileTime(&ftime);

    double rval = ftime.dwLowDateTime;
    rval += ((std::int64_t)ftime.dwHighDateTime) << 32;

    return (rval / 10000000.0);
}

// Case Insensitive substring matching
const char *stristr(const char *const string, const char *const substring)
{
    char *string_copy = _strdup(string);
    _strlwr(string_copy);

    char *substring_copy = _strdup(substring);
    _strlwr(substring_copy);

    const char *match = std::strstr(string_copy, substring_copy);
    if (match)
    {
        match = (string + (match - string_copy));
    }

    std::free(string_copy);
    std::free(substring_copy);
    return match;
}

/*--------------------------------------------------------------------
// New implementation of FlipSlashes, DefaultExtension, 
// StripExtension, ExtractFilePath, ExtractFile, etc.
----------------------------------------------------------------------*/

//Since all of these functions operate around either the extension
//or the directory path, centralize getting both numbers here so we
//can just reference them everywhere else.  Use strrchr to give a
//speed boost while we're at it.
inline void getFilePositions(const char *path, int *extension_position, int *directory_position)
{
    const char *ptr = std::strrchr(path, '.');
    if (ptr == 0)
    {
        *extension_position = -1;
    }
    else
    {
        *extension_position = ptr - path;
    }

    ptr = qmax(std::strrchr(path, '/'), std::strrchr(path, '\\'));
    if (ptr == 0)
    {
        *directory_position = -1;
    }
    else
    {
        *directory_position = ptr - path;
        if (*directory_position > *extension_position)
        {
            *extension_position = -1;
        }

        //cover the case where we were passed a directory - get 2nd-to-last slash
        if (*directory_position == (int)std::strlen(path) - 1)
        {
            do
            {
                --(*directory_position);
            } while (*directory_position > -1 && path[*directory_position] != '/' && path[*directory_position] != '\\');
        }
    }
}

char *FlipSlashes(char *string)
{
    char *ptr = string;
    if (SYSTEM_SLASH_CHAR == '\\')
    {
        while (ptr = strchr(ptr, '/'))
        {
            *ptr = SYSTEM_SLASH_CHAR;
        }
    }
    else
    {
        while (ptr = strchr(ptr, '\\'))
        {
            *ptr = SYSTEM_SLASH_CHAR;
        }
    }
    return string;
}

void DefaultExtension(char *path, const char *extension)
{
    int extension_pos, directory_pos;
    getFilePositions(path, &extension_pos, &directory_pos);
    if (extension_pos == -1)
    {
        std::strcat(path, extension);
    }
}

void StripExtension(char *path)
{
    int extension_pos, directory_pos;
    getFilePositions(path, &extension_pos, &directory_pos);
    if (extension_pos != -1)
    {
        path[extension_pos] = 0;
    }
}

void ExtractFilePath(const char *const path, char *dest)
{
    int extension_pos, directory_pos;
    getFilePositions(path, &extension_pos, &directory_pos);
    if (directory_pos != -1)
    {
        std::memcpy(dest, path, directory_pos + 1); //include directory slash
        dest[directory_pos + 1] = 0;
    }
    else
    {
        dest[0] = 0;
    }
}

void ExtractFile(const char *const path, char *dest)
{
    int extension_pos, directory_pos;
    getFilePositions(path, &extension_pos, &directory_pos);

    int length = std::strlen(path);

    length -= directory_pos + 1;

    std::memcpy(dest, path + directory_pos + 1, length); //exclude directory slash
    dest[length] = 0;
}

//=============================================================================

bool CDECL FORMAT_PRINTF(3, 4) safe_snprintf(char *const dest, const std::size_t count, const char *const args, ...)
{
    va_list argptr;

    hlassert(count > 0);

    va_start(argptr, args);
    std::size_t amt = std::vsnprintf(dest, count, args, argptr);
    va_end(argptr);

    // truncated (bad!, snprintf doesn't null terminate the string when this happens)
    if (amt == count)
    {
        dest[count - 1] = 0;
        return false;
    }

    return true;
}

bool safe_strncpy(char *const dest, const char *const src, const std::size_t count)
{
    return safe_snprintf(dest, count, "%s", src);
}

bool safe_strncat(char *const dest, const char *const src, const std::size_t count)
{
    if (count)
    {
        std::strncat(dest, src, count);

        dest[count - 1] = 0; // Ensure it is null terminated
        return true;
    }
    else
    {
        Warning("safe_strncat passed empty count");
        return false;
    }
}

bool TerminatedString(const char *buffer, const int size)
{
    for (int x = 0; x < size; x++, buffer++)
    {
        if ((*buffer) == 0)
        {
            return true;
        }
    }
    return false;
}
