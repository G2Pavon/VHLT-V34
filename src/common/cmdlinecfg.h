#pragma once

typedef enum
{
    IFDEF,
    IFNDEF,
    ELSE,
    ENDIF,
    DEFINE,
    UNDEF
} command_t;

struct execute_t
{
    int stack;
    bool skip;
    int skipstack;
};

void ParseParamFile(const int argc, char **const argv, int &argcnew, char **&argvnew);