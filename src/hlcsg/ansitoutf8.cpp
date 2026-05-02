#include "ansitoutf8.h"
#include <vector>
#include <string>
#include <cstdlib>
#include <cstring>
#include <cstdint>

static const uint32_t w1252_to_unicode[32] = {
    0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, // 0x80 - 0x87
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0000, 0x017D, 0x0000, // 0x88 - 0x8F
    0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, // 0x90 - 0x97
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0000, 0x017E, 0x0178  // 0x98 - 0x9F
};

static void push_utf8(std::string &s, uint32_t cp)
{
    if (cp == 0)
        return;
    if (cp < 0x80)
    {
        s.push_back(static_cast<char>(cp));
    }
    else if (cp < 0x800)
    {
        s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else if (cp < 0x10000)
    {
        s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else
    {
        s.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

char *ANSItoUTF8(const char *string)
{
    if (!string || !*string)
        return nullptr;

    std::string utf8_result;
    utf8_result.reserve(std::strlen(string) * 2);

    for (const char *p = string; *p; ++p)
    {
        uint8_t c = static_cast<uint8_t>(*p);

        if (c < 0x80)
        {
            utf8_result.push_back(static_cast<char>(c));
        }
        else if (c >= 0x80 && c <= 0x9F)
        {
            push_utf8(utf8_result, w1252_to_unicode[c - 0x80]);
        }
        else
        {
            push_utf8(utf8_result, static_cast<uint32_t>(c));
        }
    }

    char *final_ptr = static_cast<char *>(std::malloc(utf8_result.size() + 1));
    if (final_ptr)
    {
        std::memcpy(final_ptr, utf8_result.c_str(), utf8_result.size() + 1);
    }
    return final_ptr;
}

bool IsValidUTF8(const char *string)
{
    if (!string)
        return true;

    const unsigned char *bytes = reinterpret_cast<const unsigned char *>(string);
    while (*bytes)
    {
        if (bytes[0] < 0x80) // ASCII 1-byte (0xxxxxxx)
        {
            bytes++;
        }
        else if ((bytes[0] & 0xE0) == 0xC0) // 2-bytes (110xxxxx 10xxxxxx)
        {
            if ((bytes[1] & 0xC0) != 0x80)
                return false;
            bytes += 2;
        }
        else if ((bytes[0] & 0xF0) == 0xE0) // 3-bytes (1110xxxx 10xxxxxx 10xxxxxx)
        {
            if ((bytes[1] & 0xC0) != 0x80 || (bytes[2] & 0xC0) != 0x80)
                return false;
            bytes += 3;
        }
        else if ((bytes[0] & 0xF8) == 0xF0) // 4-bytes (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx)
        {
            if ((bytes[1] & 0xC0) != 0x80 || (bytes[2] & 0xC0) != 0x80 || (bytes[3] & 0xC0) != 0x80)
                return false;
            bytes += 4;
        }
        else
        {
            return false;
        }
    }
    return true;
}