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

        const std::vector<char> data = ReadFile(path.string().c_str());
        CHECK(std::string(data.begin(), data.end()) == content);

        {
            UniqueFile f = SafeOpen(path.string().c_str(), "rb");
            std::fgetc(f.get());
            CHECK(q_filelength(f.get()) == 8);
            CHECK(std::ftell(f.get()) == 1); // position restored
        }

        CHECK(q_exists(path.string().c_str()));
        CHECK(!q_exists((dir / "missing").string().c_str()));

        const fs::path empty = dir / "empty.bin";
        std::ofstream(empty, std::ios::binary).flush();
        CHECK(ReadFile(empty.string().c_str()).empty());

        fs::remove_all(dir);
    }

    TEST_CASE("UniqueFile closes on scope exit and can release ownership")
    {
        const fs::path path = fs::temp_directory_path() / ("vhlt_filelib_w_" + std::to_string(getpid()));
        {
            UniqueFile f = SafeOpen(path.string().c_str(), "wb");
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

        RemoveFile(file.string());
        CHECK(!fs::exists(file));
        RemoveFile(file.string()); // already gone: no error
        RemoveFile((dir / "never" / "there.lin").string());
        RemoveFile((dir / "empty.prt").string());
        CHECK(fs::is_directory(dir / "empty.prt")); // unlink() never removed directories

        fs::remove_all(dir);
    }
}
