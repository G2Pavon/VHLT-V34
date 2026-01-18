#pragma once

#include "cmdlib.h" //--vluzacn

#include <malloc.h>

/////////////////////////////
#ifdef SYSTEM_WIN32

#define alloca _alloca

#define strncasecmp _strnicmp
#define strcasecmp _stricmp

#define finite _finite

#define rotl _rotl
#define rotr _rotr

#undef STDCALL
#undef FASTCALL
#undef CDECL

#define STDCALL __stdcall
#define FASTCALL __fastcall
#define CDECL __cdecl

#define INLINE __inline

#define FORCEINLINE __forceinline                   //--vluzacn
#define FORMAT_PRINTF(STRING_INDEX, FIRST_TO_CHECK) //--vluzacn

#endif