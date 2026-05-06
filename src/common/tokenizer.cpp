#include "common/tokenizer.h"

#include <cstring>
#include <cstdlib>

#include "common/cmdlib.h"
#include "common/filelib.h"
#include "common/messages.h"
#include "common/log.h"

char g_token[MAXTOKEN];

static TokenStream s_token_stream;
TokenStream *s_stream = &s_token_stream;

int g_currentline;
bool g_endofstream;

void OpenTokenStream(const char *const filename)
{
    strcpy_s(s_stream->filename, filename);

    int size = LoadFile(s_stream->filename, (char **)&s_stream->buffer);

    s_stream->line = 1;
    s_stream->cursor = s_stream->buffer;
    s_stream->end = s_stream->buffer + size;
    g_currentline = 1;
    g_endofstream = false;
}

void ParseFromMemory(char *buffer, const int size)
{
    strcpy_s(s_stream->filename, "memory buffer");

    s_stream->buffer = buffer;
    s_stream->line = 1;
    s_stream->cursor = s_stream->buffer;
    s_stream->end = s_stream->buffer + size;
    g_currentline = 1;
    g_endofstream = false;
}

void CloseTokenStream()
{
    if (s_stream->buffer && std::strcmp(s_stream->filename, "memory buffer"))
    {
        std::free(s_stream->buffer);
        s_stream->buffer = nullptr;
    }
}

static bool EndOfStream(const bool crossline)
{
    if (!crossline)
        Error("Line %i is incomplete (did you place a \" inside an entity string?) \n", g_currentline);

    g_endofstream = true;
    CloseTokenStream();
    return false;
}

bool GetToken(const bool crossline)
{
    char *token_p;

    if (s_stream->cursor >= s_stream->end)
        return EndOfStream(crossline);

skipspace:
    while (s_stream->cursor < s_stream->end && *s_stream->cursor <= 32)
    {
        if (*s_stream->cursor++ == '\n')
        {
            if (!crossline)
                Error("Line %i is incomplete\n", g_currentline);
            g_currentline = s_stream->line++;
        }
    }

    if (s_stream->cursor >= s_stream->end)
        return EndOfStream(crossline);

    if (*s_stream->cursor == ';' || *s_stream->cursor == '#' ||
        (*s_stream->cursor == '/' && *(s_stream->cursor + 1) == '/'))
    {
        if (!crossline)
            Error("Line %i is incomplete\n", g_currentline);

        while (s_stream->cursor < s_stream->end && *s_stream->cursor != '\n')
            s_stream->cursor++;

        if (s_stream->cursor < s_stream->end)
        {
            g_currentline = s_stream->line++;
            s_stream->cursor++;
        }
        goto skipspace;
    }

    token_p = g_token;

    if (*s_stream->cursor == '"')
    {
        s_stream->cursor++;
        while (s_stream->cursor < s_stream->end && *s_stream->cursor != '"')
        {
            if (token_p == &g_token[MAXTOKEN - 1])
                Error("Token too large on line %i\n", g_currentline);
            *token_p++ = *s_stream->cursor++;
        }
        if (s_stream->cursor < s_stream->end)
            s_stream->cursor++;
    }
    else
    {
        while (s_stream->cursor < s_stream->end &&
               *s_stream->cursor > 32 && *s_stream->cursor != ';')
        {
            if (token_p == &g_token[MAXTOKEN - 1])
                Error("Token too large on line %i\n", g_currentline);
            *token_p++ = *s_stream->cursor++;
        }
    }

    *token_p = 0;
    return true;
}