#pragma once

void *AllocBlock(unsigned long size);
bool FreeBlock(void *pointer);

void *Alloc(unsigned long size);