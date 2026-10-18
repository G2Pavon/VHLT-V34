#pragma once

// Table-driven command-line parsing for the tools.

#include <cstdlib>
#include <functional>
#include <span>

struct CommandLineOption
{
    const char *name; // e.g. "-threads"; matched case-insensitively
    // Called with the option's value (the next argument), or nullptr for an option without one.
    std::function<void(const char *value)> apply;
    bool takesvalue = false;
    const char *missingvalue = nullptr; // logged before usage() when the value is missing
    // An option with several values (numvalues > 1, takesvalue unused): applymany gets them; when
    // fewer follow, missing() is called instead of usage() (and is not expected to return either).
    int numvalues = 1;
    std::function<void(char *const *values)> applymany = nullptr;
    std::function<void()> missing = nullptr;
};

// An option without a value that sets target to value.
inline CommandLineOption CommandLineFlag(const char *name, bool &target, bool value)
{
    return CommandLineOption{name, [&target, value](const char *) { target = value; }};
}

// A size given in kilobytes, in bytes. compat: the old `atoi(value) * 1024` overflowed a signed int
// (undefined) for values above 2 GB; this wraps the way it did in practice.
inline int KilobytesToBytes(int kilobytes)
{
    return static_cast<int>(static_cast<unsigned>(kilobytes) * 1024u);
}
inline int KilobytesToBytes(const char *value)
{
    return KilobytesToBytes(std::atoi(value));
}

// Applies argv[1..argc-1] in order and returns the one argument that is not an option (the map
// name), or nullptr if there is none. Calls usage() (which is expected not to return) when an
// option's value is missing, and after logging `Unknown option "..."` for an argument starting
// with '-' that is not in the table or for a second non-option argument.
// unknownoption is the format for an unknown '-' argument (hlvis prints it without a newline).
const char *ParseCommandLine(int argc, char **argv, std::span<const CommandLineOption> options, void (*usage)(),
                             const char *unknownoption = "Unknown option \"%s\"\n");
