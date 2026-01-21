#include <errno.h>
#include <string.h>

#include "filelib.h"
#include "log.h"
#include "blockmem.h"

/*
 * ================
 * filelength
 * ================
 */
int q_filelength(FILE *f)
{
    int pos = ftell(f);
    fseek(f, 0, SEEK_END);
    int end = ftell(f);
    fseek(f, pos, SEEK_SET);

    return end;
}

/*
 * ================
 * exists
 * ================
 */
bool q_exists(const char *const filename)
{
    FILE *f = fopen(filename, "rb");

    if (!f)
    {
        IfDebug(Developer(DEVELOPER_LEVEL_SPAM, "Checking for existance of file %s (failed)\n", filename));
        return false;
    }
    else
    {
        fclose(f);
        IfDebug(Developer(DEVELOPER_LEVEL_SPAM, "Checking for existance of file %s (success)\n", filename));
        return true;
    }
}

FILE *SafeOpenWrite(const char *const filename)
{
    FILE *f = fopen(filename, "wb");

    if (!f)
        Error("Error opening %s: %s", filename, strerror(errno));

    return f;
}

FILE *SafeOpenRead(const char *const filename)
{
    FILE *f = fopen(filename, "rb");

    if (!f)
        Error("Error opening %s: %s", filename, strerror(errno));

    return f;
}

void SafeRead(FILE *f, void *buffer, int count)
{
    if (fread(buffer, 1, count, f) != (size_t)count)
        Error("File read failure");
}

void SafeWrite(FILE *f, const void *const buffer, int count)
{
    if (fwrite(buffer, 1, count, f) != (size_t)count)
        Error("File write failure"); //Error("File read failure"); //--vluzacn
}

/*
 * ==============
 * LoadFile
 * ==============
 */
int LoadFile(const char *const filename, char **bufferptr)
{
    FILE *f = SafeOpenRead(filename);
    int length = q_filelength(f);
    char *buffer = (char *)Alloc(length + 1);
    SafeRead(f, buffer, length);
    fclose(f);

    *bufferptr = buffer;
    return length;
}
