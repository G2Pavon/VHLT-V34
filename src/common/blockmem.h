#pragma once

#include <cstddef>

void *AllocBlock(size_t size);
bool FreeBlock(void *pointer);