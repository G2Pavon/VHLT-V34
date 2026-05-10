#include "common/blockmem.h"

#include <cstdlib>

#include "common/messages.h"
#include "common/log.h"

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

bool FreeBlock(void *pointer)
{
    std::free(pointer);
    return true;
}