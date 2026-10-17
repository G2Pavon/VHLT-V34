#include "doctest/doctest.h"

#include <string>
#include <type_traits>
#include <vector>

#include "common/tokenizer.h"

namespace
{
// Next(true) until end of stream.
std::vector<std::string> Tokens(std::string text)
{
    Tokenizer t(std::move(text));
    std::vector<std::string> out;
    while (t.Next(true))
    {
        out.push_back(t.token());
    }
    return out;
}

using V = std::vector<std::string>;
} // namespace

TEST_SUITE("tokenizer")
{
    TEST_CASE("whitespace-separated and quoted tokens")
    {
        CHECK(Tokens("{ \"classname\" \"worldspawn\" }\n") == V{"{", "classname", "worldspawn", "}"});
        CHECK(Tokens("  a\tb\r\nc  ") == V{"a", "b", "c"});
        CHECK(Tokens("\"two words\" \"semi;colon\" \"\"") == V{"two words", "semi;colon", ""});
        CHECK(Tokens("( 1 2 3 ) [ 1 0 0 0 ] 0.25") == V{"(", "1", "2", "3", ")", "[", "1", "0", "0", "0", "]", "0.25"});
    }

    TEST_CASE("quotes only delimit at the start of a token")
    {
        CHECK(Tokens("ab\"cd ef\"") == V{"ab\"cd", "ef\""});
    }

    TEST_CASE("comments: //, ; and # run to end of line")
    {
        CHECK(Tokens("a // b c\nd") == V{"a", "d"});
        CHECK(Tokens("; whole line\na") == V{"a"});
        CHECK(Tokens("# whole line\na") == V{"a"});
        CHECK(Tokens("a/b") == V{"a/b"}); // a single slash is not a comment
    }

    TEST_CASE("compat: ';' ends an unquoted token and starts a comment")
    {
        CHECK(Tokens("foo;bar baz\nqux") == V{"foo", "qux"});
    }

    TEST_CASE("unterminated quote runs to end of stream")
    {
        CHECK(Tokens("\"abc def") == V{"abc def"});
    }

    TEST_CASE("compat: bytes >= 0x80 are whitespace outside quotes, on every platform")
    {
        // The original compared a plain char with 32, which on x86 (signed char) treats high-bit
        // bytes as separators; since Phase 5 that is explicit, so unsigned-char platforms match.
        CHECK(Tokens("caf\xE9 x") == V{"caf", "x"});
        CHECK(Tokens("\xC3\xA9t\xC3\xA9") == V{"t"});
        CHECK(Tokens("\"caf\xE9\" x") == V{"caf\xE9", "x"}); // kept inside quotes
    }

    TEST_CASE("a '/' at the very end of the text is a token, not a comment")
    {
        CHECK(Tokens("a /") == V{"a", "/"});
    }

    TEST_CASE("Tokenizer can be used on its own")
    {
        Tokenizer t;
        t.Reset("{ \"k\" \"v\" }");
        std::vector<std::string> toks;
        while (t.Next(true))
        {
            toks.push_back(t.token());
        }
        CHECK(toks == V{"{", "k", "v", "}"});
        CHECK(!t.Next(true)); // stays at the end
    }

    TEST_CASE("compat: currentline lags one line behind the token just read")
    {
        Tokenizer t("a\nb\n\nc");
        REQUIRE(t.Next(true));
        CHECK(t.currentline() == 1);
        REQUIRE(t.Next(true));
        CHECK(t.token() == "b");
        CHECK(t.currentline() == 1);
        REQUIRE(t.Next(true));
        CHECK(t.token() == "c");
        CHECK(t.currentline() == 3);
        CHECK(!t.Next(true));
    }

    TEST_CASE("Next(false) continues on the same line")
    {
        Tokenizer t("\"key\" \"value\"\n");
        REQUIRE(t.Next(true));
        REQUIRE(t.Next(false));
        CHECK(t.token() == "value");
    }

    TEST_CASE("compat: a quoted token ends at its first NUL; outside quotes NUL is a blank")
    {
        using namespace std::string_literals;
        CHECK(Tokens("\"ab\0cd\" next"s) == V{"ab", "next"});
        CHECK(Tokens("ab\0cd"s) == V{"ab", "cd"});
    }

    TEST_CASE("two tokenizers do not share state")
    {
        Tokenizer a("x y"), b("1 2");
        REQUIRE(a.Next(true));
        REQUIRE(b.Next(true));
        REQUIRE(a.Next(true));
        CHECK(a.token() == "y");
        CHECK(b.token() == "1");
    }

    TEST_CASE("a token of MAXTOKEN-1 characters fits")
    {
        std::string text(MAXTOKEN - 1, 'x');
        auto toks = Tokens(text);
        REQUIRE(toks.size() == 1);
        CHECK(toks[0].size() == static_cast<size_t>(MAXTOKEN - 1));
    }

    TEST_CASE("empty input")
    {
        CHECK(Tokens("").empty());
        CHECK(Tokens("   \n\n  ").empty());
        CHECK(Tokens("// only a comment").empty());
    }
}
