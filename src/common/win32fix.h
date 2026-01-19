#pragma once

#include "cmdlib.h" //--vluzacn

#include <malloc.h>

#define alloca _alloca

#define strncasecmp _strnicmp
#define strcasecmp _stricmp

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