#include "common/tokenizer.h"

#include <cstring>
#include <utility>

#include "common/log.h"

namespace
{
// compat: the original compared a plain char with 32, so on x86 (signed char) bytes >= 0x80
// count as blanks outside quotes. That behaviour is kept on every platform.
bool IsBlank(char c)
{
    return static_cast<signed char>(c) <= 32;
}
} // namespace

void Tokenizer::Reset(std::string text)
{
    m_text = std::move(text);
    m_cursor = 0;
    m_line = 1;
    m_currentline = 1;
    m_token.clear();
}

bool Tokenizer::EndOfStream(bool crossline) const
{
    if (!crossline)
        Error("Line %i is incomplete (did you place a \" inside an entity string?) \n", m_currentline);
    return false;
}

bool Tokenizer::Next(bool crossline)
{
    const std::size_t end = m_text.size();

    if (m_cursor >= end)
        return EndOfStream(crossline);

    for (;;) // skip blanks and comments
    {
        while (m_cursor < end && IsBlank(m_text[m_cursor]))
        {
            if (m_text[m_cursor++] == '\n')
            {
                if (!crossline)
                    Error("Line %i is incomplete\n", m_currentline);
                m_currentline = m_line++;
            }
        }

        if (m_cursor >= end)
            return EndOfStream(crossline);

        const char c = m_text[m_cursor];
        if (!(c == ';' || c == '#' || (c == '/' && m_cursor + 1 < end && m_text[m_cursor + 1] == '/')))
            break;

        if (!crossline)
            Error("Line %i is incomplete\n", m_currentline);

        while (m_cursor < end && m_text[m_cursor] != '\n')
            m_cursor++;

        if (m_cursor < end)
        {
            m_currentline = m_line++;
            m_cursor++;
        }
    }

    m_token.clear();
    if (m_text[m_cursor] == '"')
    {
        m_cursor++;
        while (m_cursor < end && m_text[m_cursor] != '"')
        {
            if (m_token.size() == MAXTOKEN - 1)
                Error("Token too large on line %i\n", m_currentline);
            m_token += m_text[m_cursor++];
        }
        if (m_cursor < end)
            m_cursor++;
        // compat: tokens were handed out as C strings, so a quoted token ends at its first NUL
        // (NUL is a blank outside quotes)
        m_token.resize(std::strlen(m_token.c_str()));
    }
    else
    {
        while (m_cursor < end && !IsBlank(m_text[m_cursor]) && m_text[m_cursor] != ';')
        {
            if (m_token.size() == MAXTOKEN - 1)
                Error("Token too large on line %i\n", m_currentline);
            m_token += m_text[m_cursor++];
        }
    }
    return true;
}
