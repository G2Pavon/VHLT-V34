#include "common/entity.h"

#include <cstring>
#include <cstdlib>
#include <cstdio>

// =====================================================================================
//  SetKeyValue
//      makes a keyvalue
// =====================================================================================
void DeleteKey(entity_t *ent, const char *const key)
{
    for (epair_t **pep = &ent->epairs; *pep; pep = &(*pep)->next)
    {
        if (!std::strcmp((*pep)->key, key))
        {
            epair_t *ep = *pep;
            *pep = ep->next;
            std::free(ep->key);
            std::free(ep->value);
            std::free(ep);
            return;
        }
    }
}
void SetKeyValue(entity_t *ent, const char *const key, const char *const value)
{
    epair_t *ep;

    if (!value[0])
    {
        DeleteKey(ent, key);
        return;
    }
    for (ep = ent->epairs; ep; ep = ep->next)
    {
        if (!std::strcmp(ep->key, key))
        {
            char *value2 = strdup(value);
            std::free(ep->value);
            ep->value = value2;
            return;
        }
    }
    ep = (epair_t *)std::calloc(1, sizeof(*ep));
    ep->next = ent->epairs;
    ent->epairs = ep;
    ep->key = strdup(key);
    ep->value = strdup(value);
}

// =====================================================================================
//  ValueForKey
//      returns the value for a passed entity and key
// =====================================================================================
const char *ValueForKey(const entity_t *const ent, const char *const key)
{
    for (epair_t *ep = ent->epairs; ep; ep = ep->next)
    {
        if (!std::strcmp(ep->key, key))
        {
            return ep->value;
        }
    }
    return "";
}

int IntForKey(const entity_t *const ent, const char *const key)
{
    return std::atoi(ValueForKey(ent, key));
}

vec_t FloatForKey(const entity_t *const ent, const char *const key)
{
    return std::atof(ValueForKey(ent, key));
}

// =====================================================================================
//  GetVectorForKey
//      returns value for key in vec[0-2]
// =====================================================================================
void GetVectorForKey(const entity_t *const ent, const char *const key, vec3_t vec)
{
    const char *k = ValueForKey(ent, key);
    // scanf into doubles, then assign, so it is vec_t size independent
    double v1 = 0.0;
    double v2 = 0.0;
    double v3 = 0.0;
    std::sscanf(k, "%lf %lf %lf", &v1, &v2, &v3);
    vec[0] = v1;
    vec[1] = v2;
    vec[2] = v3;
}
