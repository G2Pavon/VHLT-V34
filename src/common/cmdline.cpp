#include "common/cmdline.h"

#include "common/log.h"
#include "common/win32fix.h"

const char *ParseCommandLine(int argc, char **argv, std::span<const CommandLineOption> options, void (*usage)(),
                             const char *unknownoption)
{
    const char *positional = nullptr;
    for (int i = 1; i < argc; i++)
    {
        const CommandLineOption *option = nullptr;
        for (const CommandLineOption &o : options)
        {
            if (!strcasecmp(argv[i], o.name))
            {
                option = &o;
                break;
            }
        }

        if (option)
        {
            if (!option->takesvalue)
            {
                option->apply(nullptr);
            }
            else if (i + 1 < argc)
            {
                option->apply(argv[++i]);
            }
            else
            {
                if (option->missingvalue)
                {
                    Log("%s", option->missingvalue);
                }
                usage();
            }
        }
        else if (argv[i][0] == '-')
        {
            Log(unknownoption, argv[i]);
            usage();
        }
        else if (positional)
        {
            Log("Unknown option \"%s\"\n", argv[i]);
            usage();
        }
        else
        {
            positional = argv[i];
        }
    }
    return positional;
}
