#pragma once

extern void *AllocBlock(unsigned long size);
extern bool FreeBlock(void *pointer);

extern void *Alloc(unsigned long size);
extern bool Free(void *pointer);