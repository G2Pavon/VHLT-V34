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
typedef struct
{
    int stack;
    bool skip;
    int skipstack;
} execute_t;

void ParseParamFile(const int argc, char **const argv, int &argcnew, char **&argvnew);