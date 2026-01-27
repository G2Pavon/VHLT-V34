#include <cstdlib>

#include "blockmem.h"
#include "messages.h"
#include "log.h"

// =====================================================================================
//  AllocBlock
// =====================================================================================
void *AllocBlock(size_t size)
{
    if (size == 0)
    {
        Warning("Attempting to allocate 0 bytes");
    }

    void *p = std::calloc(1, size);

    hlassume(p != nullptr, assume_NoMemory);

    return p;
}

// =====================================================================================
//  FreeBlock
// =====================================================================================
bool FreeBlock(void *pointer)
{
    std::free(pointer);
    return true;
}