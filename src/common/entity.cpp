#include "common/entity.h"

#include <cstdio>
#include <cstdlib>

#include "common/cmdlib.h"

EntityKeys::Storage::iterator EntityKeys::FindEntry(std::string_view key)
{
    for (auto it = m_entries.end(); it != m_entries.begin();)
    {
        --it;
        if ((*it)->key == key)
        {
            return it;
        }
    }
    return m_entries.end();
}

const EntityKeyValue *EntityKeys::Find(std::string_view key) const
{
    for (auto it = m_entries.crbegin(); it != m_entries.crend(); ++it)
    {
        if ((*it)->key == key)
        {
            return it->get();
        }
    }
    return nullptr;
}

void EntityKeys::Prepend(std::string key, std::string value)
{
    m_entries.push_back(std::make_unique<EntityKeyValue>(EntityKeyValue{std::move(key), std::move(value)}));
}

void EntityKeys::Set(std::string_view key, std::string_view value)
{
    if (value.empty())
    {
        Erase(key);
        return;
    }
    // key and value may point into this entity's own entries: copy them before changing anything
    std::string newvalue(value);
    const auto it = FindEntry(key);
    if (it != m_entries.end())
    {
        (*it)->value = std::move(newvalue);
        return;
    }
    Prepend(std::string(key), std::move(newvalue));
}

bool EntityKeys::Erase(std::string_view key)
{
    const auto it = FindEntry(key);
    if (it == m_entries.end())
    {
        return false;
    }
    m_entries.erase(it);
    return true;
}

void DeleteKey(entity_t *ent, const char *const key)
{
    ent->keys.Erase(key);
}

void SetKeyValue(entity_t *ent, const char *const key, const char *const value)
{
    ent->keys.Set(key, value);
}

const char *ValueForKey(const entity_t *const ent, const char *const key)
{
    const EntityKeyValue *kv = ent->keys.Find(key);
    return kv ? kv->value.c_str() : "";
}

int IntForKey(const entity_t *const ent, const char *const key)
{
    return ParseInt(ValueForKey(ent, key));
}

vec_t FloatForKey(const entity_t *const ent, const char *const key)
{
    return std::atof(ValueForKey(ent, key));
}

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
