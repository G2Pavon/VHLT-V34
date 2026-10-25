#include "doctest/doctest.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <unistd.h>

#include "common/cmdlib.h"
#include "common/log.h"
#include "common/messages.h"
#include "common/stringlib.h"

TEST_SUITE("cmdlib")
{
    TEST_CASE("PathWithDefaultExtension appends only when there is no extension in the file name")
    {
        CHECK(PathWithDefaultExtension("maps/foo", ".bsp") == "maps/foo.bsp");
        CHECK(PathWithDefaultExtension("foo.map", ".bsp") == "foo.map");
        CHECK(PathWithDefaultExtension("dir.v2/foo", ".bsp") == "dir.v2/foo.bsp");
        CHECK(PathWithDefaultExtension("dir.v2\\foo", ".bsp") == "dir.v2\\foo.bsp");
    }

    TEST_CASE("PathWithoutExtension removes the last extension of the file name")
    {
        CHECK(PathWithoutExtension("a/b.c.d") == "a/b.c");
        CHECK(PathWithoutExtension("a.b/c") == "a.b/c");
        CHECK(PathWithoutExtension("noext") == "noext");
        CHECK(PathWithoutExtension(".hidden") == "");
    }

    TEST_CASE("PathDirectory keeps the trailing slash; either slash counts")
    {
        CHECK(PathDirectory("a/b/c.map") == "a/b/");
        CHECK(PathDirectory("c.map") == "");
        CHECK(PathDirectory("a\\b/c") == "a\\b/");
        CHECK(PathDirectory("a/b\\c") == "a/b\\");
        CHECK(PathDirectory("/abs/x") == "/abs/");
    }

    TEST_CASE("compat: a path ending in a slash is treated as a directory name")
    {
        CHECK(PathDirectory("a/b/") == "a/");
        CHECK(PathFile("a/b/") == "b/");
        CHECK(PathDirectory("b/") == "");
        CHECK(PathFile("b/") == "b/");
    }

    TEST_CASE("PathFile")
    {
        CHECK(PathFile("a/b/c.map") == "c.map");
        CHECK(PathFile("c.map") == "c.map");
        CHECK(PathFile("a\\c.map") == "c.map");
    }

    TEST_CASE("PathWithSystemSlashes converts either slash to the system slash")
    {
        if (SYSTEM_SLASH_CHAR == '/')
        {
            CHECK(PathWithSystemSlashes("a\\b/c\\d") == "a/b/c/d");
        }
        else
        {
            CHECK(PathWithSystemSlashes("a\\b/c\\d") == "a\\b\\c\\d");
        }
        CHECK(PathWithSystemSlashes("no-slash") == "no-slash");
        CHECK(PathWithSystemSlashes("") == "");
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

    TEST_CASE("path helpers: pinned results, including where std::filesystem::path differs")
    {
        // input, PathWithDefaultExtension(".bsp"), PathWithoutExtension, PathDirectory, PathFile.
        // On POSIX fs::path gets the backslash rows wrong (no directory; the whole input is the
        // file name), keeps ".map" as a stem, gives "a/b/" an empty file name and drops the
        // trailing slash from the directory.
        struct Row
        {
            const char *path, *withdefault, *withoutext, *directory, *file;
        };
        const Row rows[] = {
        {"maps/foo", "maps/foo.bsp", "maps/foo", "maps/", "foo"},
        {"foo.map", "foo.map", "foo", "", "foo.map"},
        {"dir.v2/foo", "dir.v2/foo.bsp", "dir.v2/foo", "dir.v2/", "foo"},
        {"dir.v2\\foo", "dir.v2\\foo.bsp", "dir.v2\\foo", "dir.v2\\", "foo"},
        {"a/b/c.map", "a/b/c.map", "a/b/c", "a/b/", "c.map"},
        {"c.map", "c.map", "c", "", "c.map"},
        {"a\\b/c", "a\\b/c.bsp", "a\\b/c", "a\\b/", "c"},
        {"a/b\\c", "a/b\\c.bsp", "a/b\\c", "a/b\\", "c"},
        {"a/b/", "a/b/.bsp", "a/b/", "a/", "b/"},
        {"a/b.c/", "a/b.c/.bsp", "a/b.c/", "a/", "b.c/"},
        {"b/", "b/.bsp", "b/", "", "b/"},
        {".hidden", ".hidden", "", "", ".hidden"},
        {".map", ".map", "", "", ".map"},
        {"/abs/x", "/abs/x.bsp", "/abs/x", "/abs/", "x"},
        {"", ".bsp", "", "", ""},
        {"x.tar.gz", "x.tar.gz", "x.tar", "", "x.tar.gz"},
        {"foo.", "foo.", "foo", "", "foo."},
        {"maps\\de_dust2.map", "maps\\de_dust2.map", "maps\\de_dust2", "maps\\", "de_dust2.map"},
        {"C:\\hl\\cstrike\\maps\\x.map", "C:\\hl\\cstrike\\maps\\x.map", "C:\\hl\\cstrike\\maps\\x", "C:\\hl\\cstrike\\maps\\", "x.map"},
        {"valve\\halflife.wad", "valve\\halflife.wad", "valve\\halflife", "valve\\", "halflife.wad"},
        {"C:halflife.wad", "C:halflife.wad", "C:halflife", "", "C:halflife.wad"},
        };
        for (const Row &r : rows)
        {
            CAPTURE(r.path);
            CHECK(PathWithDefaultExtension(r.path, ".bsp") == r.withdefault);
            CHECK(PathWithoutExtension(r.path) == r.withoutext);
            CHECK(PathDirectory(r.path) == r.directory);
            CHECK(PathFile(r.path) == r.file);
        }
    }

    TEST_CASE("SetMapname: system slashes, no extension, no length limit")
    {
        const std::string saved = g_Mapname;
        SetMapname("maps\\sub/de_dust.map");
        CHECK(g_Mapname == PathWithSystemSlashes("maps/sub/de_dust"));
        // was copied into a char[_MAX_PATH] (260 on Windows) and silently cut
        const std::string deep(5000, 'd');
        SetMapname((deep + "/m.map").c_str());
        CHECK(g_Mapname == PathWithSystemSlashes(deep + "/m"));
        g_Mapname = saved;
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

    TEST_CASE("StringPrintf has no length limit")
    {
        CHECK(StringPrintf("%i %i %i", 1, -2, 3) == "1 -2 3");
        CHECK(StringPrintf("%s", "") == "");
        CHECK(StringPrintf("*%i", 511) == "*511");
        const std::string big(5000, 'x');
        const std::string out = StringPrintf("<%s>%d", big.c_str(), 7);
        CHECK(out.size() == 5003);
        CHECK(out == "<" + big + ">7");
    }

    TEST_CASE("Log and Warning write messages longer than 2047 bytes in full")
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / ("vhlt_log_tests_" + std::to_string(getpid()));
        fs::create_directories(dir);
        const std::string saved = g_Mapname;
        g_Mapname = (dir / "map").string();

        const std::string big(3000, 'y');
        OpenLog();
        Log("[%s]\n", big.c_str());
        Warning("%s", big.c_str());
        CloseLog();

        std::ifstream in(g_Mapname + ".log", std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        CHECK(text.str().find("[" + big + "]\r\n") != std::string::npos);
        CHECK(text.str().find("Warning: " + big + "\r\n") != std::string::npos);

        g_Mapname = saved;
        fs::remove_all(dir);
    }

    TEST_CASE("ProbeSearch lands on the same element as glibc's bsearch, duplicates included")
    {
        std::mt19937 rng(7);
        for (int n : {0, 1, 2, 3, 10, 100, 1000})
        {
            for (int keys : {1, 2, 5, 1000})
            {
                std::vector<int> v(n);
                for (int &x : v)
                {
                    x = static_cast<int>(rng() % keys);
                }
                std::sort(v.begin(), v.end());
                for (int key = -1; key <= keys; key++)
                {
                    CAPTURE(n);
                    CAPTURE(keys);
                    CAPTURE(key);
                    const auto cmp = [](const void *a, const void *b) { return *(const int *)a - *(const int *)b; };
                    const int *expected = static_cast<const int *>(std::bsearch(&key, v.data(), v.size(), sizeof(int), cmp));
                    const int *got = ProbeSearch(v.data(), v.size(), [key](const int &x) { return key - x; });
                    CHECK(got == expected);
                }
            }
        }
    }

    TEST_CASE("ParseInt is glibc's atoi, overflow included")
    {
        // literal values, so this means the same on a platform whose atoi differs
        CHECK(ParseInt("12") == 12);
        CHECK(ParseInt(" \t+12") == 12);
        CHECK(ParseInt("-12abc") == -12);
        CHECK(ParseInt("abc") == 0);
        CHECK(ParseInt("") == 0);
        CHECK(ParseInt("0x10") == 0);
        CHECK(ParseInt("12.9") == 12);
        CHECK(ParseInt("2147483647") == 2147483647);
        CHECK(ParseInt("2147483648") == -2147483647 - 1);
        CHECK(ParseInt("4294967298") == 2);
        CHECK(ParseInt("99999999999") == 1215752191);
        CHECK(ParseInt("-99999999999") == -1215752191);
        CHECK(ParseInt("99999999999999999999999") == -1); // strtoll clamps to LLONG_MAX
        CHECK(ParseInt("-99999999999999999999999") == 0); // LLONG_MIN
#if defined(__GLIBC__)
        for (const char *s : {"12", " +12", "-12abc", "abc", "2147483648", "4294967298", "99999999999", "-99999999999",
                              "99999999999999999999999", "-99999999999999999999999"})
        {
            CAPTURE(s);
            CHECK(ParseInt(s) == std::atoi(s));
        }
#endif
    }
}
