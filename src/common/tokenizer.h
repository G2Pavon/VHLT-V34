#pragma once

#include <cstddef>
#include <string>
#include <utility>

constexpr int MAXTOKEN = 4096;

// Splits .map and entity-lump text into tokens: quoted strings, or runs of non-blank characters
// ending at a blank or ';'. ';', '#' and "//" start a comment that runs to the end of the line.
// Malformed input (an incomplete line, a token of MAXTOKEN characters or more) exits via Error().
class Tokenizer
{
  public:
    Tokenizer() = default;
    explicit Tokenizer(std::string text) { Reset(std::move(text)); }

    void Reset(std::string text);

    // The next token, or false at the end of the text. With crossline false the token must be on
    // the current line, otherwise the line is reported as incomplete.
    bool Next(bool crossline);

    const std::string &token() const { return m_token; }
    // compat: the line before the one being read once a newline has been passed (it is what the
    // error messages have always printed).
    int currentline() const { return m_currentline; }

  private:
    bool EndOfStream(bool crossline) const;

    std::string m_text;
    std::size_t m_cursor = 0;
    int m_line = 1;
    int m_currentline = 1;
    std::string m_token;
};
