#include "common/filelib.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>

#include "common/log.h"
#include "common/messages.h"
#include "common/win32fix.h" // GetModuleFileNameA (Windows)

UniqueFile SafeOpen(const char *const filename, const char *const mode)
{
    UniqueFile f(std::fopen(filename, mode));

    if (!f)
        Error("Error opening %s: %s", filename, std::strerror(errno));

    return f;
}

std::vector<char> ReadFile(const char *const filename)
{
    UniqueFile f = SafeOpen(filename, "rb");
    const int length = q_filelength(f.get());
    std::vector<char> data(length > 0 ? length : 0);
    SafeRead(f.get(), data.data(), static_cast<int>(data.size()));
    return data;
}

int q_filelength(std::FILE *f)
{
    long pos = std::ftell(f);
    std::fseek(f, 0, SEEK_END);
    long end = std::ftell(f);
    std::fseek(f, pos, SEEK_SET);

    return static_cast<int>(end);
}

bool q_exists(const char *const filename)
{
    // compat: "can be opened for reading", which on POSIX is also true for a directory
    return UniqueFile(std::fopen(filename, "rb")) != nullptr;
}

void SafeRead(std::FILE *f, void *buffer, int count)
{
    if (std::fread(buffer, 1, count, f) != (std::size_t)count)
        Error("File read failure");
}

void SafeWrite(std::FILE *f, const void *const buffer, int count)
{
    if (std::fwrite(buffer, 1, count, f) != (std::size_t)count)
        Error("File write failure"); //Error("File read failure"); //--vluzacn
}

std::FILE *SafeOpenWrite(const char *const filename)
{
    return SafeOpen(filename, "wb").release();
}

std::FILE *SafeOpenRead(const char *const filename)
{
    return SafeOpen(filename, "rb").release();
}

int LoadFile(const char *const filename, char **bufferptr)
{
    const std::vector<char> data = ReadFile(filename);
    char *buffer = static_cast<char *>(std::calloc(1, data.size() + 1));
    hlassume(buffer != nullptr, assume_NoMemory);
    if (!data.empty())
    {
        std::memcpy(buffer, data.data(), data.size());
    }

    *bufferptr = buffer;
    return static_cast<int>(data.size());
}

std::string ExecutablePath()
{
#if defined(_WIN32)
    char buffer[_MAX_PATH];
    const DWORD length = GetModuleFileNameA(nullptr, buffer, sizeof(buffer));
    return std::string(buffer, length);
#else
    std::error_code error;
    const std::filesystem::path path = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::string() : path.string();
#endif
}
