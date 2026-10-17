#pragma once

// Table-driven command-line parsing for the tools.

#include <functional>
#include <span>

struct CommandLineOption
{
    const char *name; // e.g. "-threads"; matched case-insensitively
    // Called with the option's value (the next argument), or nullptr for an option without one.
    std::function<void(const char *value)> apply;
    bool takesvalue = false;
    const char *missingvalue = nullptr; // logged before usage() when the value is missing
};

// Applies argv[1..argc-1] in order and returns the one argument that is not an option (the map
// name), or nullptr if there is none. Calls usage() (which is expected not to return) when an
// option's value is missing, and after logging `Unknown option "..."` for an argument starting
// with '-' that is not in the table or for a second non-option argument.
const char *ParseCommandLine(int argc, char **argv, std::span<const CommandLineOption> options, void (*usage)());
