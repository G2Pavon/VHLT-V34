
#ifdef SYSTEM_WIN32
#include <sys/stat.h>
#include <io.h>
#include <fcntl.h>
#endif

#ifdef SYSTEM_POSIX
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#endif

#include "filelib.h"
#include "messages.h"
#include "log.h"
#include "blockmem.h"

/*
 * ================
 * filelength
 * ================
 */
int q_filelength(FILE *f)
{
    int pos;
    int end;

    pos = ftell(f);
    fseek(f, 0, SEEK_END);
    end = ftell(f);
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
    FILE *f;

    f = fopen(filename, "rb");

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
    FILE *f;

    f = fopen(filename, "wb");

    if (!f)
        Error("Error opening %s: %s", filename, strerror(errno));

    return f;
}

FILE *SafeOpenRead(const char *const filename)
{
    FILE *f;

    f = fopen(filename, "rb");

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
    FILE *f;
    int length;
    char *buffer;

    f = SafeOpenRead(filename);
    length = q_filelength(f);
    buffer = (char *)Alloc(length + 1);
    SafeRead(f, buffer, length);
    fclose(f);

    *bufferptr = buffer;
    return length;
}
