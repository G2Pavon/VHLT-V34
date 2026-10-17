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
        CHECK(!safe_snprintf(buf, sizeof(buf), "%s", "abcdefghij")); // false when cut (Phase 4; used to be true)
        CHECK(std::string(buf) == "abcdefg");
        CHECK(!safe_snprintf(buf, sizeof(buf), "%s", "abcdefgh")); // exactly one too long
        CHECK(safe_snprintf(buf, sizeof(buf), "%s", "abcdefg"));   // exactly fits
        CHECK(!safe_strncpy(buf, "0123456789", sizeof(buf)));
        CHECK(std::string(buf) == "0123456");
        CHECK(!safe_snprintf(buf, 0, "%s", "x")); // nothing written
    }

    TEST_CASE("safe_strncat result is cut at count - 1 characters")
    {
        // compat: same resulting string as before; it no longer writes past dest[count - 1]
        // on the way, and returns false when it had to cut.
        char buf[32] = "abc";
        CHECK(!safe_strncat(buf, "defgh", 6));
        CHECK(std::string(buf) == "abcde");
        char buf2[32] = "ab";
        CHECK(safe_strncat(buf2, "c", 6));
        CHECK(std::string(buf2) == "abc");
        char exact[6] = "abc";
        CHECK(safe_strncat(exact, "de", sizeof(exact)));
        CHECK(std::string(exact) == "abcde");
        char guard[8] = {'a', 'b', 'c', 0, 'X', 'X', 'X', 'X'};
        CHECK(!safe_strncat(guard, "defghij", 6));
        CHECK(std::string(guard) == "abcde");
        CHECK(guard[6] == 'X'); // nothing written past count
        CHECK(guard[7] == 'X');
    }

    TEST_CASE("string path API matches the char-buffer helpers")
    {
        for (const char *p : {"maps/foo", "foo.map", "dir.v2/foo", "a/b/c.map", "c.map", "a\\b/c", "a/b/", "b/", ".hidden", "/abs/x", ""})
        {
            CAPTURE(p);
            CHECK(PathWithDefaultExtension(p, ".bsp") == Default(p, ".bsp"));
            CHECK(PathWithoutExtension(p) == Strip(p));
            CHECK(PathDirectory(p) == FilePath(p));
            CHECK(PathFile(p) == File(p));
        }
    }

    TEST_CASE("the fixed-size wrappers fill the whole array")
    {
        char exact[8] = "abc";
        DefaultExtension(exact, ".bsp"); // 7 characters + NUL: fits exactly
        CHECK(std::string(exact) == "abc.bsp");
        char dir[5];
        ExtractFilePath("abc/def", dir);
        CHECK(std::string(dir) == "abc/");
        char file[4];
        ExtractFile("x/abc", file);
        CHECK(std::string(file) == "abc");
    }

    TEST_CASE("I_FloatTime is monotonic seconds")
    {
        const double a = I_FloatTime();
        const double b = I_FloatTime();
        CHECK(b >= a);
        CHECK(a > 1.0); // threads.cpp uses < 1.0 to mean "not recorded yet"
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
