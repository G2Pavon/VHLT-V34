#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdlib>

char *ANSItoUTF8(const char *string)
{
    int len = MultiByteToWideChar(CP_ACP, 0, string, -1, NULL, 0);
    wchar_t *unicode = (wchar_t *)calloc(len + 1, sizeof(wchar_t));
    MultiByteToWideChar(CP_ACP, 0, string, -1, unicode, len);
    len = WideCharToMultiByte(CP_UTF8, 0, unicode, -1, NULL, 0, NULL, NULL);
    char *utf8 = (char *)calloc(len + 1, sizeof(char));
    WideCharToMultiByte(CP_UTF8, 0, unicode, -1, utf8, len, NULL, NULL);
    free(unicode);
    return utf8;
}