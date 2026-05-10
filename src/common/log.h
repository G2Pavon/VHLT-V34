#pragma once

#include "common/win32fix.h"
#include "common/messages.h"

extern char *g_Program;
extern char g_Mapname[_MAX_PATH];

#define DEFAULT_LOG true

extern bool g_log;

void ResetTmpFiles();
void ResetLog();
void ResetErrorLog();
void CheckForErrorLog();
void LogError(const char *const message);
void CDECL OpenLog();
void CDECL CloseLog();
void CheckFatal();

void CDECL FORMAT_PRINTF(1, 2) Error(const char *const error, ...);
void CDECL FORMAT_PRINTF(2, 3) Fatal(assume_msgs msgid, const char *const error, ...);
void CDECL FORMAT_PRINTF(1, 2) PrintOnce(const char *const message, ...);
void CDECL FORMAT_PRINTF(1, 2) Warning(const char *const warning, ...);

void CDECL FORMAT_PRINTF(1, 2) Log(const char *const message, ...);
void Banner();
void LogStart(const int argc, char **argv);
void LogEnd();
void hlassume(bool exp, assume_msgs msgid); // Should be in hlassert.h, but well so what
void LogTimeElapsed(float elapsed_time);

void CDECL FORMAT_PRINTF(1, 2) PrintConsole(const char *const message, ...);