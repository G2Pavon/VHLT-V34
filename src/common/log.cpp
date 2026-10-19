#include "common/log.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common/cmdlib.h"
#include "common/filelib.h"
#include "common/hlassert.h"
#include "common/messages.h"

const char *g_Program = "Uninitialized variable ::g_Program";
std::string g_Mapname = "Uninitialized variable ::g_Mapname";

void SetMapname(const char *mapname_from_arg)
{
    g_Mapname = PathWithoutExtension(PathWithSystemSlashes(mapname_from_arg));
}

bool g_log = DEFAULT_LOG;

static UniqueFile CompileLog;
static std::atomic<bool> fatal{false};

static thread_local LogCapture *t_capture = nullptr;

void CaptureWriteLog(const char *message)
{
    t_capture->m_events.push_back({false, message, {}});
}

void CaptureLogError(const char *message)
{
    t_capture->m_events.push_back({true, message, {}});
}

ScopedLogCapture::ScopedLogCapture(LogCapture &capture) : m_previous(t_capture)
{
    t_capture = &capture;
}

ScopedLogCapture::~ScopedLogCapture()
{
    t_capture = m_previous;
}

// Messages have no length limit. The oracle cut them at 2047 bytes; that only ever affected log,
// .err and console text, never the BSP.
static std::string AssumeMessage(assume_msgs msgid)
{
    const MessageTable_t *msg = GetAssume(msgid);
    return std::string(msg->title) + "\nDescription: " + msg->text + "\nHowto Fix: " + msg->howto + "\n";
}

static void DeleteMapFile(const char *const extension)
{
    std::remove((g_Mapname + extension).c_str());
}

void ResetTmpFiles()
{
    if (g_log)
    {
        for (const char *extension : {".bsp", ".inc", ".p0", ".p1", ".p2", ".p3", ".prt", ".pts", ".lin", ".hsz",
                                      ".pln", ".b0", ".b1", ".b2", ".b3", ".wa_", ".ext"})
        {
            DeleteMapFile(extension);
        }
    }
}

void ResetLog()
{
    if (g_log)
    {
        DeleteMapFile(".log");
    }
}

void ResetErrorLog()
{
    if (g_log)
    {
        DeleteMapFile(".err");
    }
}

void CheckForErrorLog()
{
    if (g_log)
    {
        if (q_exists((g_Mapname + ".err").c_str()))
        {
            Log(">> There was a problem compiling the map.\n"
                ">> Check the file %s.log for the cause.\n",
                g_Mapname.c_str());
            std::exit(1);
        }
    }
}

void LogError(const char *const message)
{
    if (t_capture)
    {
        CaptureLogError(message);
        return;
    }
    if (g_log && CompileLog)
    {
        const std::string logfilename = g_Mapname + ".err";
        UniqueFile ErrorLog(std::fopen(logfilename.c_str(), "a"));

        if (ErrorLog)
        {
            std::fprintf(ErrorLog.get(), "%s: %s\n", g_Program, message);
        }
        else
        {
            std::fprintf(stderr, "ERROR: Could not open error logfile %s", logfilename.c_str());
            std::fflush(stderr);
        }
    }
}

void OpenLog()
{
    if (g_log)
    {
        const std::string logfilename = g_Mapname + ".log";
        CompileLog.reset(std::fopen(logfilename.c_str(), "a"));

        if (!CompileLog)
        {
            std::fprintf(stderr, "ERROR: Could not open logfile %s", logfilename.c_str());
            std::fflush(stderr);
        }
    }
}

void CloseLog()
{
    if (g_log && CompileLog)
    {
        LogEnd();
        CompileLog.reset();
    }
}
// Every function up to this point should check g_log, the functions below should not
// AJM: fprintf/flush wasnt printing newline chars correctly (prefixed with \r) under win32
//      due to the fact that those streams are in byte mode, so this function prefixes
//      all \n with \r automatically.
static void Safe_WriteLog(const char *const message)
{
    if (!CompileLog)
        return;

    for (const char *c = message; *c; c++)
    {
        if (*c == '\n')
            std::fputc('\r', CompileLog.get());

        std::fputc(*c, CompileLog.get());
    }
}

static void WriteLog(const char *const message)
{
    if (t_capture)
    {
        CaptureWriteLog(message);
        return;
    }
    Safe_WriteLog(message);
    std::fprintf(stdout, "%s", message);
    std::fflush(stdout);
}

void CheckFatal()
{
    if (fatal)
    {
        hlassert(false);
        std::exit(1);
    }
}

void FORMAT_PRINTF(1, 2) Error(const char *const error, ...)
{
    va_list argptr;
    va_start(argptr, error);
    const std::string message = "Error: " + StringVPrintf(error, argptr) + "\n";
    va_end(argptr);

    WriteLog(message.c_str());
    LogError(message.c_str());

    if (t_capture)
    {
        throw CapturedError{}; // the owner of the capture replays it and exits (ExitAfterError)
    }
    ExitAfterError();
}

void ExitAfterError()
{
    fatal = true;
    CheckFatal();
    std::exit(1); // not reached: CheckFatal() exits once fatal is set
}

void LogInOrder(std::function<void()> action)
{
    if (t_capture)
    {
        t_capture->m_events.push_back({false, {}, std::move(action)});
        return;
    }
    action();
}

void LogCapture::Replay() const
{
    for (const Event &e : m_events)
    {
        if (e.action)
        {
            e.action();
        }
        else if (e.errorlog)
        {
            LogError(e.text.c_str());
        }
        else
        {
            WriteLog(e.text.c_str());
        }
    }
}

void FORMAT_PRINTF(2, 3) Fatal(assume_msgs msgid, const char *const warning, ...)
{
    va_list argptr;
    va_start(argptr, warning);
    const std::string message = "Error: " + StringVPrintf(warning, argptr) + "\n";
    va_end(argptr);

    WriteLog(message.c_str());
    LogError(message.c_str());

    PrintOnce("%s", AssumeMessage(msgid).c_str());

    fatal = true;
}

void FORMAT_PRINTF(1, 2) PrintOnce(const char *const warning, ...)
{
    va_list argptr;
    va_start(argptr, warning);
    std::string message = "Error: " + StringVPrintf(warning, argptr) + "\n";
    va_end(argptr);

    // which call is the first is decided in work order (see LogInOrder)
    LogInOrder([text = std::move(message)]()
               {
                   static std::atomic<bool> printed{false};
                   if (printed.exchange(true)) // make sure it only gets called once
                   {
                       return;
                   }
                   WriteLog(text.c_str());
                   LogError(text.c_str()); });
}

void FORMAT_PRINTF(1, 2) Warning(const char *const warning, ...)
{
    va_list argptr;
    va_start(argptr, warning);
    const std::string message = "Warning: " + StringVPrintf(warning, argptr) + "\n";
    va_end(argptr);

    WriteLog(message.c_str());
}

void FORMAT_PRINTF(1, 2) Log(const char *const warning, ...)
{
    va_list argptr;
    va_start(argptr, warning);
    const std::string message = StringVPrintf(warning, argptr);
    va_end(argptr);

    WriteLog(message.c_str());
}

static void LogArgs(int argc, char **argv)
{
    Log("Command line: ");
    for (int i = 0; i < argc; i++)
    {
        if (strchr(argv[i], ' '))
        {
            Log("\"%s\" ", argv[i]);
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
        g_Program, VHLT_BUILD_DATE);

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

void HlassumeFailed(assume_msgs msgid)
{
    Error("%s", AssumeMessage(msgid).c_str());
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

void FORMAT_PRINTF(1, 2) PrintConsole(const char *const warning, ...)
{
    va_list argptr;
    va_start(argptr, warning);
    const std::string message = StringVPrintf(warning, argptr);
    va_end(argptr);

    std::fprintf(stdout, "%s", message.c_str());
}