#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "blockmem.h"
#include "messages.h"
#include "log.h"

// =====================================================================================
//  AllocBlock
// =====================================================================================
void *AllocBlock(const unsigned long size)
{
    void *pointer;

    if (!size)
    {
        Warning("Attempting to allocate 0 bytes");
    }

    HANDLE h = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, size);

    hlassume(h != NULL, assume_NoMemory);

    if (h)
    {
        pointer = GlobalLock(h);
    }
    else
    {
        return nullptr;
    }

    return pointer;
}

// =====================================================================================
//  FreeBlock
// =====================================================================================
bool FreeBlock(void *pointer)
{
    if (pointer == nullptr)
    {
        Warning("Freeing a null pointer");
    }

    HANDLE h = GlobalHandle(pointer);

    if (h)
    {
        GlobalUnlock(h);
        GlobalFree(h);
        return true;
    }
    else
    {
        Warning("Could not translate pointer into handle");
        return false;
    }
}