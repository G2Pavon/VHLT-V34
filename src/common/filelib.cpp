#include <cstdlib>
#include <cstdio>
#include <cerrno>
#include <cstring>

#include "filelib.h"
#include "log.h"
#include "blockmem.h"

/*
 * ================
 * filelength
 * ================
 */
int q_filelength(std::FILE *f)
{
    int pos = std::ftell(f);
    std::fseek(f, 0, SEEK_END);
    int end = std::ftell(f);
    std::fseek(f, pos, SEEK_SET);

    return end;
}

/*
 * ================
 * exists
 * ================
 */
bool q_exists(const char *const filename)
{
    std::FILE *f = std::fopen(filename, "rb");

    if (!f)
    {
        return false;
    }
    else
    {
        std::fclose(f);
        return true;
    }
}

std::FILE *SafeOpenWrite(const char *const filename)
{
    std::FILE *f = std::fopen(filename, "wb");

    if (!f)
        Error("Error opening %s: %s", filename, std::strerror(errno));

    return f;
}

std::FILE *SafeOpenRead(const char *const filename)
{
    std::FILE *f = std::fopen(filename, "rb");

    if (!f)
        Error("Error opening %s: %s", filename, std::strerror(errno));

    return f;
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

/*
 * ==============
 * LoadFile
 * ==============
 */
int LoadFile(const char *const filename, char **bufferptr)
{
    std::FILE *f = SafeOpenRead(filename);
    int length = q_filelength(f);
    char *buffer = (char *)std::calloc(1, length + 1);
    SafeRead(f, buffer, length);
    std::fclose(f);

    *bufferptr = buffer;
    return length;
}
