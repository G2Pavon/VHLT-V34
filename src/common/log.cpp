#include "common/log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>

#include "common/messages.h"
#include "common/hlassert.h"
#include "common/filelib.h"
#include "common/cmdlib.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

char *g_Program = "Uninitialized variable ::g_Program";
char g_Mapname[_MAX_PATH] = "Uninitialized variable ::g_Mapname";

bool g_log = DEFAULT_LOG;

static std::FILE *CompileLog = nullptr;
static bool fatal = false;

bool twice = false;
std::FILE *conout = nullptr;

void ResetTmpFiles()
{
    if (g_log)
    {
        char filename[_MAX_PATH];

        safe_snprintf(filename, _MAX_PATH, "%s.bsp", g_Mapname);
        _unlink(filename);

        safe_snprintf(filename, _MAX_PATH, "%s.inc", g_Mapname);
        _unlink(filename);

        safe_snprintf(filename, _MAX_PATH, "%s.p0", g_Mapname);
        _unlink(filename);

        safe_snprintf(filename, _MAX_PATH, "%s.p1", g_Mapname);
        _unlink(filename);

        safe_snprintf(filename, _MAX_PATH, "%s.p2", g_Mapname);
        _unlink(filename);

        safe_snprintf(filename, _MAX_PATH, "%s.p3", g_Mapname);
        _unlink(filename);

        safe_snprintf(filename, _MAX_PATH, "%s.prt", g_Mapname);
        _unlink(filename);

        safe_snprintf(filename, _MAX_PATH, "%s.pts", g_Mapname);
        _unlink(filename);

        safe_snprintf(filename, _MAX_PATH, "%s.lin", g_Mapname);
        _unlink(filename);

        safe_snprintf(filename, _MAX_PATH, "%s.hsz", g_Mapname);
        _unlink(filename);

        safe_snprintf(filename, _MAX_PATH, "%s.pln", g_Mapname);
        _unlink(filename);

        safe_snprintf(filename, _MAX_PATH, "%s.b0", g_Mapname);
        _unlink(filename);

        safe_snprintf(filename, _MAX_PATH, "%s.b1", g_Mapname);
        _unlink(filename);

        safe_snprintf(filename, _MAX_PATH, "%s.b2", g_Mapname);
        _unlink(filename);

        safe_snprintf(filename, _MAX_PATH, "%s.b3", g_Mapname);
        _unlink(filename);

        safe_snprintf(filename, _MAX_PATH, "%s.wa_", g_Mapname);
        _unlink(filename);

        safe_snprintf(filename, _MAX_PATH, "%s.ext", g_Mapname);
        _unlink(filename);
    }
}

void ResetLog()
{
    if (g_log)
    {
        char logfilename[_MAX_PATH];

        safe_snprintf(logfilename, _MAX_PATH, "%s.log", g_Mapname);
        _unlink(logfilename);
    }
}

void ResetErrorLog()
{
    if (g_log)
    {
        char logfilename[_MAX_PATH];

        safe_snprintf(logfilename, _MAX_PATH, "%s.err", g_Mapname);
        _unlink(logfilename);
    }
}

void CheckForErrorLog()
{
    if (g_log)
    {
        char logfilename[_MAX_PATH];

        safe_snprintf(logfilename, _MAX_PATH, "%s.err", g_Mapname);
        if (q_exists(logfilename))
        {
            Log(">> There was a problem compiling the map.\n"
                ">> Check the file %s.log for the cause.\n",
                g_Mapname);
            std::exit(1);
        }
    }
}

void LogError(const char *const message)
{
    if (g_log && CompileLog)
    {
        char logfilename[_MAX_PATH];
        std::FILE *ErrorLog = nullptr;

        safe_snprintf(logfilename, _MAX_PATH, "%s.err", g_Mapname);
        ErrorLog = std::fopen(logfilename, "a");

        if (ErrorLog)
        {
            std::fprintf(ErrorLog, "%s: %s\n", g_Program, message);
            std::fflush(ErrorLog);
            std::fclose(ErrorLog);
            ErrorLog = nullptr;
        }
        else
        {
            std::fprintf(stderr, "ERROR: Could not open error logfile %s", logfilename);
            std::fflush(stderr);
            if (twice)
            {
                std::fprintf(conout, "ERROR: Could not open error logfile %s", logfilename);
                std::fflush(conout);
            }
        }
    }
}

void CDECL OpenLog()
{
    if (g_log)
    {
        char logfilename[_MAX_PATH];
        {
            safe_snprintf(logfilename, _MAX_PATH, "%s.log", g_Mapname);
        }
        CompileLog = std::fopen(logfilename, "a");

        if (!CompileLog)
        {
            std::fprintf(stderr, "ERROR: Could not open logfile %s", logfilename);
            std::fflush(stderr);
            if (twice)
            {
                std::fprintf(conout, "ERROR: Could not open logfile %s", logfilename);
                std::fflush(conout);
            }
        }
    }
}

void CDECL CloseLog()
{
    if (g_log && CompileLog)
    {
        LogEnd();
        std::fflush(CompileLog);
        std::fclose(CompileLog);
        CompileLog = nullptr;
    }
}

//
//  Every function up to this point should check g_log, the functions below should not
//

// AJM: fprintf/flush wasnt printing newline chars correctly (prefixed with \r) under win32
//      due to the fact that those streams are in byte mode, so this function prefixes
//      all \n with \r automatically.
//      NOTE: system load may be more with this method, but there isnt that much logging going
//      on compared to the time taken to compile the map, so its negligable.
static void Safe_WriteLog(const char *const message)
{
    if (!CompileLog)
        return;

    const char *c = &message[0];

    while (1)
    {
        if (!*c)
            return; // end of string

        if (*c == '\n')
            std::fputc('\r', CompileLog);

        std::fputc(*c, CompileLog);

        c++;
    }
}

static void WriteLog(const char *const message)
{
    Safe_WriteLog(message);
    std::fprintf(stdout, "%s", message); //std::fprintf(stdout, message); //--vluzacn
    std::fflush(stdout);
    if (twice)
    {
        std::fprintf(conout, "%s", message);
        std::fflush(conout);
    }
}

void CheckFatal()
{
    if (fatal)
    {
        hlassert(false);
        std::exit(1);
    }
}

#define MAX_ERROR 2048
#define MAX_WARNING 2048
#define MAX_MESSAGE 2048

void CDECL FORMAT_PRINTF(1, 2) Error(const char *const error, ...)
{
    char message[MAX_ERROR];
    char message2[MAX_ERROR];
    va_list argptr;

    va_start(argptr, error);
    std::vsnprintf(message, MAX_ERROR, error, argptr);
    va_end(argptr);

    safe_snprintf(message2, MAX_MESSAGE, "%s%s\n", "Error: ", message);
    WriteLog(message2);
    LogError(message2);

    fatal = 1;
    CheckFatal();
}

void CDECL FORMAT_PRINTF(2, 3) Fatal(assume_msgs msgid, const char *const warning, ...)
{
    char message[MAX_WARNING];
    char message2[MAX_WARNING];

    va_list argptr;

    va_start(argptr, warning);
    std::vsnprintf(message, MAX_WARNING, warning, argptr);
    va_end(argptr);

    safe_snprintf(message2, MAX_MESSAGE, "%s%s\n", "Error: ", message);
    WriteLog(message2);
    LogError(message2);

    {
        char message[MAX_MESSAGE];
        const MessageTable_t *msg = GetAssume(msgid);

        safe_snprintf(message, MAX_MESSAGE, "%s\n%s%s\n%s%s\n", msg->title, "Description: ", msg->text, "Howto Fix: ", msg->howto);
        PrintOnce(message);
    }

    fatal = 1;
}

void CDECL FORMAT_PRINTF(1, 2) PrintOnce(const char *const warning, ...)
{
    char message[MAX_WARNING];
    char message2[MAX_WARNING];
    va_list argptr;
    static int count = 0;

    if (count > 0) // make sure it only gets called once
    {
        return;
    }
    count++;

    va_start(argptr, warning);
    std::vsnprintf(message, MAX_WARNING, warning, argptr);
    va_end(argptr);

    safe_snprintf(message2, MAX_MESSAGE, "%s%s\n", "Error: ", message);
    WriteLog(message2);
    LogError(message2);
}

void CDECL FORMAT_PRINTF(1, 2) Warning(const char *const warning, ...)
{
    char message[MAX_WARNING];
    char message2[MAX_WARNING];

    va_list argptr;

    va_start(argptr, warning);
    std::vsnprintf(message, MAX_WARNING, warning, argptr);
    va_end(argptr);

    safe_snprintf(message2, MAX_MESSAGE, "%s%s\n", "Warning: ", message);
    WriteLog(message2);
}

void CDECL FORMAT_PRINTF(1, 2) Log(const char *const warning, ...)
{
    char message[MAX_MESSAGE];

    va_list argptr;

    va_start(argptr, warning);
    std::vsnprintf(message, MAX_MESSAGE, warning, argptr);
    va_end(argptr);

    WriteLog(message);
}

static void LogArgs(int argc, char **argv)
{
    Log("Command line: ");
    for (int i = 0; i < argc; i++)
    {
        if (strchr(argv[i], ' '))
        {
            Log("\"%s\" ", argv[i]); //Log("\"%s\"", argv[i]); //--vluzacn
        }
        else
        {
            Log("%s ", argv[i]);
        }
    }
    Log("\n");
}

void Banner()
{
    Log("%s " ZHLT_VERSIONSTRING " " HACK_VERSIONSTRING
        " " PLATFORM_VERSIONSTRING
        " (%s)\n",
        g_Program, __DATE__);
    //Log("BUGGY %s (built: %s)\nUse at own risk.\n", g_Program, __DATE__);

    Log("Zoner's Half-Life Compilation Tools -- Custom Build\n"
        "Based on code modifications by Sean 'Zoner' Cavanaugh\n"
        "Based on Valve's version, modified with permission.\n" MODIFICATIONS_STRING);
}

void LogStart(int argc, char **argv)
{
    Banner();
    Log("-----  BEGIN  %s -----\n", g_Program);
    LogArgs(argc, argv);
}

void LogEnd()
{
    Log("\n-----   END   %s -----\n\n\n\n", g_Program);
}

void hlassume(bool exp, assume_msgs msgid)
{
    if (!exp)
    {
        char message[MAX_MESSAGE];
        const MessageTable_t *msg = GetAssume(msgid);

        safe_snprintf(message, MAX_MESSAGE, "%s\n%s%s\n%s%s\n", msg->title, "Description: ", msg->text, "Howto Fix: ", msg->howto);
        Error(message);
    }
}

static void seconds_to_hhmm(unsigned int elapsed_time, unsigned &days, unsigned &hours, unsigned &minutes, unsigned &seconds)
{
    seconds = elapsed_time % 60;
    elapsed_time /= 60;

    minutes = elapsed_time % 60;
    elapsed_time /= 60;

    hours = elapsed_time % 24;
    elapsed_time /= 24;

    days = elapsed_time;
}

void LogTimeElapsed(float elapsed_time)
{
    unsigned days = 0;
    unsigned hours = 0;
    unsigned minutes = 0;
    unsigned seconds = 0;

    seconds_to_hhmm(elapsed_time, days, hours, minutes, seconds);

    if (days)
    {
        Log("%.2f seconds elapsed [%ud %uh %um %us]\n", elapsed_time, days, hours, minutes, seconds);
    }
    else if (hours)
    {
        Log("%.2f seconds elapsed [%uh %um %us]\n", elapsed_time, hours, minutes, seconds);
    }
    else if (minutes)
    {
        Log("%.2f seconds elapsed [%um %us]\n", elapsed_time, minutes, seconds);
    }
    else
    {
        Log("%.2f seconds elapsed\n", elapsed_time);
    }
}

void CDECL FORMAT_PRINTF(1, 2) PrintConsole(const char *const warning, ...)
{
    char message[MAX_MESSAGE];

    va_list argptr;

    va_start(argptr, warning);

    std::vsnprintf(message, MAX_MESSAGE, warning, argptr);
    va_end(argptr);

    std::fprintf(stdout, "%s", message);
}