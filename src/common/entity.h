#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "common/mathtypes.h"

struct EntityKeyValue
{
    std::string key;
    std::string value;
};

// An entity's key/values, in the order the old linked list kept them. compat: that order is
// written to the entity lump, so it is preserved exactly: a new key goes in front (a parsed
// entity therefore lists its keys in reverse file order), a key that is set again keeps its
// place, and duplicates added by Prepend are kept. Each entry has its own allocation, so a
// pointer returned by ValueForKey stays valid while other keys are added or removed.
class EntityKeys
{
    using Storage = std::vector<std::unique_ptr<EntityKeyValue>>; // back() is the front of the list

  public:
    class const_iterator
    {
      public:
        explicit const_iterator(Storage::const_reverse_iterator it) : m_it(it) {}
        const EntityKeyValue &operator*() const { return **m_it; }
        const EntityKeyValue *operator->() const { return m_it->get(); }
        const_iterator &operator++()
        {
            ++m_it;
            return *this;
        }
        bool operator==(const const_iterator &) const = default;

      private:
        Storage::const_reverse_iterator m_it;
    };

    EntityKeys() = default;
    EntityKeys(EntityKeys &&) = default;
    EntityKeys &operator=(EntityKeys &&) = default;
    EntityKeys(const EntityKeys &) = delete; // entities used to be copied shallowly, sharing keys
    EntityKeys &operator=(const EntityKeys &) = delete;

    const_iterator begin() const { return const_iterator(m_entries.crbegin()); }
    const_iterator end() const { return const_iterator(m_entries.crend()); }
    bool empty() const { return m_entries.empty(); }
    std::size_t size() const { return m_entries.size(); }

    // First match in list order, or nullptr.
    const EntityKeyValue *Find(std::string_view key) const;
    // Adds in front without looking for an existing key (how ParseEntity builds an entity).
    void Prepend(std::string key, std::string value);
    // SetKeyValue: replaces the first match's value, or adds in front; an empty value erases.
    void Set(std::string_view key, std::string_view value);
    // Removes the first match; false if there is none.
    bool Erase(std::string_view key);
    void Clear() { m_entries.clear(); }

  private:
    Storage::iterator FindEntry(std::string_view key);

    Storage m_entries;
};

struct entity_t
{
    vec3_t origin{};
    int firstbrush = 0;
    int numbrushes = 0;
    EntityKeys keys;
};

void DeleteKey(entity_t *ent, const char *const key);
void SetKeyValue(entity_t *ent, const char *const key, const char *const value);
const char *ValueForKey(const entity_t *const ent, const char *const key);
int IntForKey(const entity_t *const ent, const char *const key);
vec_t FloatForKey(const entity_t *const ent, const char *const key);
void GetVectorForKey(const entity_t *const ent, const char *const key, vec3_t vec);
