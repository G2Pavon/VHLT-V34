#include "doctest/doctest.h"

#include <cstring>
#include <string>

#include "common/cmdlib.h"
#include "common/messages.h"
#include "common/stringlib.h"

namespace
{
std::string Default(std::string path, const char *ext)
{
    char buf[256];
    std::strcpy(buf, path.c_str());
    DefaultExtension(buf, ext);
    return buf;
}

std::string Strip(std::string path)
{
    char buf[256];
    std::strcpy(buf, path.c_str());
    StripExtension(buf);
    return buf;
}

std::string FilePath(const char *path)
{
    char buf[256];
    ExtractFilePath(path, buf);
    return buf;
}

std::string File(const char *path)
{
    char buf[256];
    ExtractFile(path, buf);
    return buf;
}
} // namespace

TEST_SUITE("cmdlib")
{
    TEST_CASE("DefaultExtension appends only when there is no extension in the file name")
    {
        CHECK(Default("maps/foo", ".bsp") == "maps/foo.bsp");
        CHECK(Default("foo.map", ".bsp") == "foo.map");
        CHECK(Default("dir.v2/foo", ".bsp") == "dir.v2/foo.bsp");
        CHECK(Default("dir.v2\\foo", ".bsp") == "dir.v2\\foo.bsp");
    }

    TEST_CASE("StripExtension removes the last extension of the file name")
    {
        CHECK(Strip("a/b.c.d") == "a/b.c");
        CHECK(Strip("a.b/c") == "a.b/c");
        CHECK(Strip("noext") == "noext");
        CHECK(Strip(".hidden") == "");
    }

    TEST_CASE("ExtractFilePath keeps the trailing slash; either slash counts")
    {
        CHECK(FilePath("a/b/c.map") == "a/b/");
        CHECK(FilePath("c.map") == "");
        CHECK(FilePath("a\\b/c") == "a\\b/");
        CHECK(FilePath("a/b\\c") == "a/b\\");
        CHECK(FilePath("/abs/x") == "/abs/");
    }

    TEST_CASE("compat: a path ending in a slash is treated as a directory name")
    {
        CHECK(FilePath("a/b/") == "a/");
        CHECK(File("a/b/") == "b/");
        CHECK(FilePath("b/") == "");
        CHECK(File("b/") == "b/");
    }

    TEST_CASE("ExtractFile")
    {
        CHECK(File("a/b/c.map") == "c.map");
        CHECK(File("c.map") == "c.map");
        CHECK(File("a\\c.map") == "c.map");
    }

    TEST_CASE("FlipSlashes converts to the system slash")
    {
        char buf[] = "a\\b/c\\d";
        FlipSlashes(buf);
        if (SYSTEM_SLASH_CHAR == '/')
        {
            CHECK(std::string(buf) == "a/b/c/d");
        }
        else
        {
            CHECK(std::string(buf) == "a\\b\\c\\d");
        }
    }

    TEST_CASE("stristr returns a pointer into the original string")
    {
        const char *s = "Hello World";
        CHECK(stristr(s, "WORLD") == s + 6);
        CHECK(stristr(s, "o w") == s + 4);
        CHECK(stristr(s, "xyz") == nullptr);
        CHECK(stristr(s, "") == s);
    }

    TEST_CASE("safe_snprintf / safe_strncpy always terminate")
    {
        char buf[8];
        CHECK(safe_snprintf(buf, sizeof(buf), "%d-%s", 42, "x"));
        CHECK(std::string(buf) == "42-x");
        safe_snprintf(buf, sizeof(buf), "%s", "abcdefghij"); // return value on truncation is a known bug (Phase 4)
        CHECK(std::string(buf) == "abcdefg");
        safe_strncpy(buf, "0123456789", sizeof(buf));
        CHECK(std::string(buf) == "0123456");
    }

    TEST_CASE("safe_strncat result is cut at count - 1 characters")
    {
        // compat for the result only: strncat's limit is on src, so the current code can write
        // past dest[count - 1] before cutting; the buffer here is large enough for that.
        char buf[32] = "abc";
        CHECK(safe_strncat(buf, "defgh", 6));
        CHECK(std::string(buf) == "abcde");
        char buf2[32] = "ab";
        safe_strncat(buf2, "c", 6);
        CHECK(std::string(buf2) == "abc");
    }

    TEST_CASE("TerminatedString")
    {
        const char a[4] = {'a', 'b', 0, 'c'};
        const char b[3] = {'a', 'b', 'c'};
        CHECK(TerminatedString(a, 4));
        CHECK(!TerminatedString(b, 3));
        CHECK(!TerminatedString(a, 2));
    }

    TEST_CASE("str_casecmp / str_ncasecmp")
    {
        CHECK(str_casecmp("AAAtrigger", "aaatrigger"));
        CHECK(!str_casecmp("abc", "abcd"));
        CHECK(str_ncasecmp("SKYbox", "sky", 3));
        CHECK(!str_ncasecmp("ab", "abc", 3));
        CHECK(str_ncasecmp("", "", 5));
    }

    TEST_CASE("GetAssume maps out-of-range ids to the first entry")
    {
        CHECK(std::string(GetAssume(assume_NoMemory)->title) == "Memory allocation failure");
        CHECK(std::string(GetAssume(assume_LEAK)->title) == "LEAK in the map");
        CHECK(GetAssume(assume_last) == GetAssume(assume_first));
        CHECK(GetAssume(static_cast<assume_msgs>(-1)) == GetAssume(assume_first));
        CHECK(std::string(GetAssume(assume_NO_EXTENT_FILE)->title) == "Couldn't open extent file");
    }
}
