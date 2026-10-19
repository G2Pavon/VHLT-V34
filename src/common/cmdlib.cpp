#include "common/cmdlib.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "common/log.h"

double I_FloatTime()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

const char *stristr(const char *const string, const char *const substring)
{
    const std::string_view s(string);
    const std::string_view sub(substring);
    const auto match = std::search(s.begin(), s.end(), sub.begin(), sub.end(),
                                   [](unsigned char a, unsigned char b)
                                   { return std::tolower(a) == std::tolower(b); });
    if (match == s.end() && !sub.empty())
    {
        return nullptr;
    }
    return string + (match - s.begin());
}

namespace
{
struct PathPositions
{
    std::ptrdiff_t extension; // index of the last '.', or -1
    std::ptrdiff_t directory; // index of the slash ending the directory part, or -1
};

std::ptrdiff_t LastIndexOf(std::string_view s, char c)
{
    const std::size_t i = s.rfind(c);
    return i == std::string_view::npos ? -1 : static_cast<std::ptrdiff_t>(i);
}

// Every path helper works from these two positions (the old getFilePositions).
PathPositions FilePositions(std::string_view path)
{
    PathPositions p{LastIndexOf(path, '.'), std::max(LastIndexOf(path, '/'), LastIndexOf(path, '\\'))};
    if (p.directory != -1)
    {
        if (p.directory > p.extension)
        {
            p.extension = -1; // the '.' is in a directory name
        }

        // cover the case where we were passed a directory - get 2nd-to-last slash
        if (p.directory == static_cast<std::ptrdiff_t>(path.size()) - 1)
        {
            do
            {
                --p.directory;
            } while (p.directory > -1 && path[p.directory] != '/' && path[p.directory] != '\\');
        }
    }
    return p;
}
} // namespace

std::string PathWithDefaultExtension(std::string_view path, std::string_view extension)
{
    std::string out(path);
    if (FilePositions(path).extension == -1)
    {
        out += extension;
    }
    return out;
}

std::string PathWithoutExtension(std::string_view path)
{
    const std::ptrdiff_t extension = FilePositions(path).extension;
    return std::string(extension == -1 ? path : path.substr(0, extension));
}

std::string PathDirectory(std::string_view path)
{
    const std::ptrdiff_t directory = FilePositions(path).directory;
    return std::string(directory == -1 ? std::string_view() : path.substr(0, directory + 1)); // include directory slash
}

std::string PathFile(std::string_view path)
{
    return std::string(path.substr(FilePositions(path).directory + 1)); // exclude directory slash
}

std::string PathWithSystemSlashes(std::string_view path)
{
    std::string out(path);
    std::replace(out.begin(), out.end(), SYSTEM_SLASH_CHAR == '\\' ? '/' : '\\', SYSTEM_SLASH_CHAR);
    return out;
}

std::string StringVPrintf(const char *const format, va_list args)
{
    va_list measure;
    va_copy(measure, args);
    const int length = std::vsnprintf(nullptr, 0, format, measure);
    va_end(measure);
    if (length <= 0)
    {
        return {};
    }
    std::string text(length, '\0');
    std::vsnprintf(text.data(), text.size() + 1, format, args); // the +1 is the string's own terminator
    return text;
}

std::string StringPrintf(const char *const format, ...)
{
    va_list args;
    va_start(args, format);
    std::string text = StringVPrintf(format, args);
    va_end(args);
    return text;
}

bool safe_snprintf(char *const dest, const std::size_t count, const char *const args, ...)
{
    if (count == 0)
    {
        return false;
    }

    va_list argptr;
    va_start(argptr, args);
    const int amt = std::vsnprintf(dest, count, args, argptr); // always terminates within count
    va_end(argptr);

    return amt >= 0 && static_cast<std::size_t>(amt) < count;
}

bool safe_strncpy(char *const dest, const char *const src, const std::size_t count)
{
    return safe_snprintf(dest, count, "%s", src);
}

bool safe_strncat(char *const dest, const char *const src, const std::size_t count)
{
    if (!count)
    {
        Warning("safe_strncat passed empty count");
        return false;
    }

    const std::size_t len = strnlen(dest, count);
    if (len == count)
    {
        dest[count - 1] = 0; // dest was not terminated within count
        return false;
    }
    const std::size_t srclen = std::strlen(src);
    const std::size_t n = std::min(srclen, count - 1 - len);
    std::memcpy(dest + len, src, n);
    dest[len + n] = 0;
    return n == srclen;
}

bool TerminatedString(const char *buffer, const int size)
{
    return size > 0 && std::memchr(buffer, 0, size) != nullptr;
}
