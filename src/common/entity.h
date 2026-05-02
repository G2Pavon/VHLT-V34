#pragma once

#include "mathtypes.h"

struct epair_t
{
    struct epair_t *next;
    char *key;
    char *value;
};

struct entity_t
{
    vec3_t origin;
    int firstbrush;
    int numbrushes;
    epair_t *epairs;
};

void DeleteKey(entity_t *ent, const char *const key);
void SetKeyValue(entity_t *ent, const char *const key, const char *const value);
const char *ValueForKey(const entity_t *const ent, const char *const key);
int IntForKey(const entity_t *const ent, const char *const key);
vec_t FloatForKey(const entity_t *const ent, const char *const key);
void GetVectorForKey(const entity_t *const ent, const char *const key, vec3_t vec);
