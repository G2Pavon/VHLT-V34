#include "doctest/doctest.h"

#include <string>
#include <vector>

#include "common/cmdline.h"

namespace
{
struct UsageCalled
{
};
void ThrowUsage()
{
    throw UsageCalled{};
}

// argv for ParseCommandLine: argv[0] is the program.
struct Args
{
    std::vector<std::string> storage;
    std::vector<char *> argv;
    explicit Args(std::vector<std::string> args) : storage(std::move(args))
    {
        storage.insert(storage.begin(), "tool");
        for (std::string &a : storage)
            argv.push_back(a.data());
    }
    int argc() const { return static_cast<int>(argv.size()); }
};

struct Seen
{
    std::vector<std::string> events;
    std::vector<CommandLineOption> options()
    {
        return {
            {"-flag", [this](const char *v) { events.push_back(v ? "flag=?" : "flag"); }},
            {"-value", [this](const char *v) { events.push_back(std::string("value=") + v); }, true, "missing\n"},
        };
    }
};

// The returned argument, or "(none)".
std::string Parse(Seen &seen, std::vector<std::string> args)
{
    Args a(std::move(args));
    const auto options = seen.options();
    const char *positional = ParseCommandLine(a.argc(), a.argv.data(), options, ThrowUsage);
    return positional ? positional : "(none)";
}
} // namespace

TEST_SUITE("cmdline")
{
    TEST_CASE("options apply in order; the non-option argument is returned")
    {
        Seen seen;
        CHECK(Parse(seen, {"-value", "1", "map", "-FLAG", "-value", "two"}) == "map");
        CHECK(seen.events == std::vector<std::string>{"value=1", "flag", "value=two"});
    }

    TEST_CASE("no arguments: nullptr, nothing applied")
    {
        Seen seen;
        CHECK(Parse(seen, {}) == "(none)");
        CHECK(seen.events.empty());
    }

    TEST_CASE("usage on a missing value, an unknown option or a second map")
    {
        Seen seen;
        CHECK_THROWS_AS(Parse(seen, {"map", "-value"}), UsageCalled);
        CHECK_THROWS_AS(Parse(seen, {"-other", "map"}), UsageCalled);
        CHECK_THROWS_AS(Parse(seen, {"map", "second"}), UsageCalled);
        CHECK_THROWS_AS(Parse(seen, {"-", "map"}), UsageCalled);
    }

    TEST_CASE("a value is taken as is, even when it looks like an option")
    {
        Seen seen;
        CHECK(Parse(seen, {"-value", "-flag"}) == "(none)");
        CHECK(seen.events == std::vector<std::string>{"value=-flag"});
    }
}
