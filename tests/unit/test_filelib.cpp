#include "doctest/doctest.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

#include "common/filelib.h"

namespace fs = std::filesystem;

TEST_SUITE("filelib")
{
    TEST_CASE("ReadFile, q_filelength and q_exists")
    {
        const fs::path dir = fs::temp_directory_path() / ("vhlt_filelib_tests_" + std::to_string(getpid()));
        fs::create_directories(dir);
        const fs::path path = dir / "data.bin";
        const std::string content("abc\0def\n", 8);
        std::ofstream(path, std::ios::binary) << content;

        const std::vector<char> data = ReadFile(path);
        CHECK(std::string(data.begin(), data.end()) == content);

        {
            UniqueFile f = SafeOpen(path, "rb");
            std::fgetc(f.get());
            CHECK(q_filelength(f.get()) == 8);
            CHECK(std::ftell(f.get()) == 1); // position restored
        }

        CHECK(q_exists(path));
        CHECK(!q_exists(dir / "missing"));

        const fs::path empty = dir / "empty.bin";
        std::ofstream(empty, std::ios::binary).flush();
        CHECK(ReadFile(empty).empty());

        fs::remove_all(dir);
    }

    TEST_CASE("UniqueFile closes on scope exit and can release ownership")
    {
        const fs::path path = fs::temp_directory_path() / ("vhlt_filelib_w_" + std::to_string(getpid()));
        {
            UniqueFile f = SafeOpen(path, "wb");
            SafeWrite(f.get(), "xyz", 3);
        } // flushed and closed here
        CHECK(fs::file_size(path) == 3);
        std::FILE *raw = std::fopen(path.string().c_str(), "rb");
        char buf[3];
        SafeRead(raw, buf, 3);
        CHECK(std::memcmp(buf, "xyz", 3) == 0);
        std::fclose(raw);
        fs::remove(path);
    }

    TEST_CASE("RemoveFile deletes files, ignores missing ones and leaves directories")
    {
        const fs::path dir = fs::temp_directory_path() / ("vhlt_removefile_tests_" + std::to_string(getpid()));
        fs::create_directories(dir / "empty.prt");
        const fs::path file = dir / "map.pts";
        std::ofstream(file) << "x";

        RemoveFile(file);
        CHECK(!fs::exists(file));
        RemoveFile(file); // already gone: no error
        RemoveFile(dir / "never" / "there.lin");
        RemoveFile(dir / "empty.prt");
        CHECK(fs::is_directory(dir / "empty.prt")); // unlink() never removed directories

        fs::remove_all(dir);
    }

    TEST_CASE("OpenFile returns null instead of exiting, and uses the mode it is given")
    {
        const fs::path dir = fs::temp_directory_path() / ("vhlt_openfile_tests_" + std::to_string(getpid()));
        fs::create_directories(dir);
        const fs::path path = dir / "new.txt";

        CHECK(OpenFile(path, "rb") == nullptr); // missing: no exit, null
        CHECK(OpenFile(dir / "no_such_dir" / "x", "wb") == nullptr);
        {
            UniqueFile f = OpenFile(path, "w");
            REQUIRE(f != nullptr);
            std::fputs("line\n", f.get());
        }
        {
            UniqueFile f = OpenFile(path, "a"); // append keeps what is there
            REQUIRE(f != nullptr);
            std::fputs("more\n", f.get());
        }
        CHECK(fs::file_size(path) == 10);
        CHECK(OpenFile(path, "rb") != nullptr);

        fs::remove_all(dir);
    }

    TEST_CASE("q_exists means 'can be opened for reading', not fs::exists")
    {
        const fs::path dir = fs::temp_directory_path() / ("vhlt_qexists_tests_" + std::to_string(getpid()));
        fs::create_directories(dir / "sub");
        CHECK(q_exists(dir / "sub")); // compat: a directory can be opened for reading on POSIX
        CHECK(!q_exists(dir / "missing"));
        CHECK(!q_exists("")); // an empty name is no file

        if (geteuid() != 0) // root can read anything
        {
            const fs::path locked = dir / "locked.wad";
            std::ofstream(locked) << "x";
            fs::permissions(locked, fs::perms::none);
            CHECK(fs::exists(locked));    // exists ...
            CHECK(!q_exists(locked));     // ... but a wad search must go on to the next candidate
            fs::permissions(locked, fs::perms::owner_all);
        }
        fs::remove_all(dir);
    }

    TEST_CASE("names are used as they are written: a backslash is a character, not a separator")
    {
        // GoldSrc names keep their backslashes (they end up in the BSP's "wad" key); on POSIX that is one file name.
        const fs::path dir = fs::temp_directory_path() / ("vhlt_names_tests_" + std::to_string(getpid()));
        fs::create_directories(dir);
        const std::string name = (dir / "valve\\half life.wad").string(); // a space and a backslash in one name
        {
            UniqueFile f = SafeOpen(name, "wb");
            SafeWrite(f.get(), "abc", 3);
        }
        CHECK(fs::exists(fs::path(dir) / "valve\\half life.wad"));
        CHECK(!fs::exists(dir / "valve")); // no directory was made out of the backslash
        CHECK(q_exists(name));
        CHECK(ReadFile(name).size() == 3);
        RemoveFile(name);
        CHECK(!q_exists(name));
        fs::remove_all(dir);
    }
}
