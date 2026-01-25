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

int plen(const char *p);
void ParseParamFile(const int argc, char **const argv, int &argcnew, char **&argvnew);
bool pvalid(const char *p);
bool pmatch(const char *cmdlineparam, const char *param);
char *pnext(char *p);
char *findparams(char *cmdlineparams, char *params);
void addparams(char *cmdline, char *params, unsigned int n);
void delparams(char *cmdline, char *params);
void parsecommand(execute_t &e, char *cmdline, char *words, unsigned int n);
const char *nextword(const char *s, char *token, unsigned int n);
void parsearg(int argc, char **argv, char *cmdline, unsigned int n);
void unparsearg(int &argc, char **&argv, char *cmdline);
void ParseParamFile(const int argc, char **const argv, int &argcnew, char **&argvnew);
