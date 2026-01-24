#include <cstdlib>
#include <cstring>

#include "scriplib.h"
#include "cmdlib.h"
#include "filelib.h"
#include "messages.h"
#include "log.h"

char g_token[MAXTOKEN];
char g_TXcommand;

typedef struct
{
    char filename[_MAX_PATH];
    char *buffer;
    char *script_p;
    char *end_p;
    int line;
} script_t;

#define MAX_INCLUDES 8

static script_t s_scriptstack[MAX_INCLUDES];
script_t *s_script;
int s_scriptline;
bool s_endofscript;

//  AddScriptToStack
//  LoadScriptFile
//  ParseFromMemory
//  UnGetToken
//  EndOfScript
//  GetToken
//  TokenAvailable

// =====================================================================================
//  AddScriptToStack
// =====================================================================================
static void AddScriptToStack(const char *const filename)
{
    s_script++;

    if (s_script == &s_scriptstack[MAX_INCLUDES])
        Error("s_script file exceeded MAX_INCLUDES");

    strcpy_s(s_script->filename, filename);

    int size = LoadFile(s_script->filename, (char **)&s_script->buffer);

    Log("Entering %s\n", s_script->filename);

    s_script->line = 1;
    s_script->script_p = s_script->buffer;
    s_script->end_p = s_script->buffer + size;
}

// =====================================================================================
//  LoadScriptFile
// =====================================================================================
void LoadScriptFile(const char *const filename)
{
    s_script = s_scriptstack;
    AddScriptToStack(filename);

    s_endofscript = false;
}

// =====================================================================================
//  ParseFromMemory
// =====================================================================================
void ParseFromMemory(char *buffer, const int size)
{
    s_script = s_scriptstack;
    s_script++;

    if (s_script == &s_scriptstack[MAX_INCLUDES])
        Error("s_script file exceeded MAX_INCLUDES");

    strcpy_s(s_script->filename, "memory buffer");

    s_script->buffer = buffer;
    s_script->line = 1;
    s_script->script_p = s_script->buffer;
    s_script->end_p = s_script->buffer + size;

    s_endofscript = false;
}

// =====================================================================================
//  EndOfScript
// =====================================================================================
bool EndOfScript(const bool crossline)
{
    if (!crossline)
        Error("Line %i is incomplete (did you place a \" inside an entity string?) \n", s_scriptline);

    if (!std::strcmp(s_script->filename, "memory buffer"))
    {
        s_endofscript = true;
        return false;
    }

    std::free(s_script->buffer);

    if (s_script == s_scriptstack + 1)
    {
        s_endofscript = true;
        return false;
    }

    s_script--;
    s_scriptline = s_script->line;

    Log("returning to %s\n", s_script->filename);

    return GetToken(crossline);
}

// =====================================================================================
//  GetToken
// =====================================================================================
bool GetToken(const bool crossline)
{
    char *token_p;

    if (s_script->script_p >= s_script->end_p)
        return EndOfScript(crossline);

    // skip space
skipspace:
    while (*s_script->script_p <= 32 && *s_script->script_p >= 0)
    {
        if (s_script->script_p >= s_script->end_p)
            return EndOfScript(crossline);

        if (*s_script->script_p++ == '\n')
        {
            if (!crossline)
                Error("Line %i is incomplete (did you place a \" inside an entity string?) \n", s_scriptline);
            s_scriptline = s_script->line++;
        }
    }

    if (s_script->script_p >= s_script->end_p)
        return EndOfScript(crossline);

    // comment fields
    if (*s_script->script_p == ';' || *s_script->script_p == '#' ||         // semicolon and # is comment field
        (*s_script->script_p == '/' && *((s_script->script_p) + 1) == '/')) // also make // a comment field
    {
        if (!crossline)
            Error("Line %i is incomplete (did you place a \" inside an entity string?) \n", s_scriptline);

        //ets+++
        if (*s_script->script_p == '/')
            s_script->script_p++;
        if (s_script->script_p[1] == 'T' && s_script->script_p[2] == 'X')
            g_TXcommand = s_script->script_p[3]; // AR: "//TX#"-style comment

        //ets---
        while (*s_script->script_p++ != '\n')
        {
            if (s_script->script_p >= s_script->end_p)
                return EndOfScript(crossline);
        }
        //ets+++
        s_scriptline = s_script->line++; // AR: this line was missing
        //ets---
        goto skipspace;
    }

    // copy g_token
    token_p = g_token;

    if (*s_script->script_p == '"')
    {
        // quoted token
        s_script->script_p++;
        while (*s_script->script_p != '"')
        {
            *token_p++ = *s_script->script_p++;

            if (s_script->script_p == s_script->end_p)
                break;

            if (token_p == &g_token[MAXTOKEN])
                Error("Token too large on line %i\n", s_scriptline);
        }
        s_script->script_p++;
    }
    else
    {
        // regular token
        while ((*s_script->script_p > 32 || *s_script->script_p < 0) && *s_script->script_p != ';')
        {
            *token_p++ = *s_script->script_p++;

            if (s_script->script_p == s_script->end_p)
                break;

            if (token_p == &g_token[MAXTOKEN])
                Error("Token too large on line %i\n", s_scriptline);
        }
    }

    *token_p = 0;

    if (!std::strcmp(g_token, "$include"))
    {
        GetToken(false);
        AddScriptToStack(g_token);
        return GetToken(crossline);
    }

    return true;
}