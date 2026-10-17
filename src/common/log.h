#pragma once

#include <string>
#include <vector>

#include "common/win32fix.h"
#include "common/messages.h"

extern const char *g_Program;
extern char g_Mapname[_MAX_PATH];

#define DEFAULT_LOG true

extern bool g_log;

void ResetTmpFiles();
void ResetLog();
void ResetErrorLog();
void CheckForErrorLog();
void LogError(const char *const message);
void OpenLog();
void CloseLog();
void CheckFatal();

[[noreturn]] void FORMAT_PRINTF(1, 2) Error(const char *const error, ...);
void FORMAT_PRINTF(2, 3) Fatal(assume_msgs msgid, const char *const error, ...);
void FORMAT_PRINTF(1, 2) PrintOnce(const char *const message, ...);
void FORMAT_PRINTF(1, 2) Warning(const char *const warning, ...);

void FORMAT_PRINTF(1, 2) Log(const char *const message, ...);
void Banner();
void LogStart(const int argc, char **argv);
void LogEnd();
void hlassume(bool exp, assume_msgs msgid); // Should be in hlassert.h, but well so what
void LogTimeElapsed(float elapsed_time);

void FORMAT_PRINTF(1, 2) PrintConsole(const char *const message, ...);

// Log output of one unit of work on a worker thread, held back so that output can be emitted in
// a fixed order (making multi-threaded runs write exactly what -threads 1 writes).
//
// While a capture is active on a thread, everything Log/Warning/Error/Fatal/PrintOnce would write to
// the log, stdout and the .err file is recorded instead. Error() records its message and throws
// CapturedError rather than exiting; the caller replays the capture in order and then calls
// ExitAfterError() from the main thread (exiting from a worker would tear down globals that other
// threads are still using). PrintConsole (progress output) is not captured.
class LogCapture
{
  public:
    void Replay() const; // writes the recorded output as if it had been written directly
    bool empty() const { return m_events.empty(); }

  private:
    friend void CaptureWriteLog(const char *message);
    friend void CaptureLogError(const char *message);
    struct Event
    {
        bool errorlog; // LogError (the .err file) rather than WriteLog
        std::string text;
    };
    std::vector<Event> m_events;
};

struct CapturedError
{
};

// Captures this thread's log output into capture until destroyed.
class ScopedLogCapture
{
  public:
    explicit ScopedLogCapture(LogCapture &capture);
    ~ScopedLogCapture();
    ScopedLogCapture(const ScopedLogCapture &) = delete;
    ScopedLogCapture &operator=(const ScopedLogCapture &) = delete;

  private:
    LogCapture *m_previous;
};

// What Error() does after printing: mark the run as failed and exit.
[[noreturn]] void ExitAfterError();