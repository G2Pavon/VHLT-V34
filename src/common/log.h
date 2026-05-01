#pragma once

#include "win32fix.h"
#include "messages.h"

typedef enum
{
    DEVELOPER_LEVEL_ALWAYS,
    DEVELOPER_LEVEL_ERROR,
    DEVELOPER_LEVEL_WARNING,
    DEVELOPER_LEVEL_MESSAGE,
    DEVELOPER_LEVEL_FLUFF,
    DEVELOPER_LEVEL_SPAM,
    DEVELOPER_LEVEL_MEGASPAM
} developer_level_t;

//
// log.c globals
//

extern char *g_Program;
extern char g_Mapname[_MAX_PATH];

#define DEFAULT_DEVELOPER DEVELOPER_LEVEL_ALWAYS
#define DEFAULT_VERBOSE false
#define DEFAULT_LOG true

extern developer_level_t g_developer;
extern bool g_verbose;
extern bool g_log;
extern unsigned long g_clientid;     // Client id of this program
extern unsigned long g_nextclientid; // Client id of next client to spawn from this server

//
// log.c Functions
//

void ResetTmpFiles();
void ResetLog();
void ResetErrorLog();
void CheckForErrorLog();
void LogError(const char *const message);
void CDECL OpenLog(int clientid);
void CDECL CloseLog();
void CheckFatal();

void CDECL FORMAT_PRINTF(1, 2) Error(const char *const error, ...);
void CDECL FORMAT_PRINTF(2, 3) Fatal(assume_msgs msgid, const char *const error, ...);
void CDECL FORMAT_PRINTF(1, 2) PrintOnce(const char *const message, ...);
void CDECL FORMAT_PRINTF(1, 2) Warning(const char *const warning, ...);
void CDECL FORMAT_PRINTF(1, 2) Verbose(const char *const message, ...);

void CDECL FORMAT_PRINTF(2, 3) Developer(developer_level_t level, const char *const message, ...);
void CDECL FORMAT_PRINTF(1, 2) Log(const char *const message, ...);
void Banner();
void LogStart(const int argc, char **argv);
void LogEnd();
void hlassume(bool exp, assume_msgs msgid); // Should be in hlassert.h, but well so what
void LogTimeElapsed(float elapsed_time);

int InitConsole(int argc, char **argv);
void CDECL FORMAT_PRINTF(1, 2) PrintConsole(const char *const message, ...);
