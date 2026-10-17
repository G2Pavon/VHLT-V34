#include "doctest/doctest.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "common/bspfile.h"
#include "common/entity.h"

#ifdef HLCSG
extern bool g_nolightopt;
#endif

namespace
{
// ParseEntities() resets g_numentities but not the slots, so tests start from empty slots.
void ClearEntities()
{
    for (int i = 0; i < MAX_MAP_ENTITIES; i++)
    {
        while (g_entities[i].epairs)
        {
            DeleteKey(&g_entities[i], g_entities[i].epairs->key);
        }
        std::memset(&g_entities[i], 0, sizeof(entity_t));
    }
    g_numentities = 0;
}

void Parse(const std::string &text)
{
    ClearEntities();
    std::memcpy(g_dentdata, text.c_str(), text.size() + 1);
    g_entdatasize = static_cast<int>(text.size()) + 1;
    ParseEntities();
}

// Entity lump text without the trailing NUL that g_entdatasize counts.
std::string Unparse()
{
    UnparseEntities();
    REQUIRE(g_entdatasize >= 1);
    CHECK(g_dentdata[g_entdatasize - 1] == '\0');
    return std::string(g_dentdata, g_entdatasize - 1);
}

std::vector<std::string> Keys(const entity_t *ent)
{
    std::vector<std::string> keys;
    for (const epair_t *ep = ent->epairs; ep; ep = ep->next)
    {
        keys.emplace_back(ep->key);
    }
    return keys;
}

using V = std::vector<std::string>;
} // namespace

TEST_SUITE("entity")
{
    TEST_CASE("SetKeyValue prepends new keys, updates in place, deletes on empty value")
    {
        entity_t ent{};
        SetKeyValue(&ent, "a", "1");
        SetKeyValue(&ent, "b", "2");
        CHECK(Keys(&ent) == V{"b", "a"});
        SetKeyValue(&ent, "a", "3");
        CHECK(Keys(&ent) == V{"b", "a"});
        CHECK(std::string(ValueForKey(&ent, "a")) == "3");
        SetKeyValue(&ent, "b", "");
        CHECK(Keys(&ent) == V{"a"});
        DeleteKey(&ent, "missing");
        DeleteKey(&ent, "a");
        CHECK(ent.epairs == nullptr);
    }

    TEST_CASE("ValueForKey is case-sensitive and returns \"\" for missing keys")
    {
        entity_t ent{};
        SetKeyValue(&ent, "Origin", "1 2 3");
        CHECK(std::string(ValueForKey(&ent, "origin")).empty());
        CHECK(std::string(ValueForKey(&ent, "Origin")) == "1 2 3");
        DeleteKey(&ent, "Origin");
    }

    TEST_CASE("numeric keys use atoi/atof/sscanf semantics")
    {
        // compat: std::from_chars would reject or parse several of these differently.
        entity_t ent{};
        auto f = [&](const char *v) { SetKeyValue(&ent, "k", v); return FloatForKey(&ent, "k"); };
        auto n = [&](const char *v) { SetKeyValue(&ent, "k", v); return IntForKey(&ent, "k"); };
        CHECK(f(" 3.5") == 3.5);
        CHECK(f("+2") == 2);
        CHECK(f("1e3x") == 1000);
        CHECK(f("0x10") == 16);
        CHECK(std::isinf(f("inf")));
        CHECK(f("abc") == 0);
        CHECK(n(" 12abc") == 12);
        CHECK(n("-7") == -7);
        CHECK(n("3.9") == 3);

        vec3_t v;
        SetKeyValue(&ent, "k", "1 2");
        GetVectorForKey(&ent, "k", v);
        CHECK(v[0] == 1);
        CHECK(v[1] == 2);
        CHECK(v[2] == 0);
        SetKeyValue(&ent, "k", "1,2,3");
        GetVectorForKey(&ent, "k", v);
        CHECK(v[0] == 1);
        CHECK(v[1] == 0);
        GetVectorForKey(&ent, "missing", v);
        CHECK(v[0] == 0);
        DeleteKey(&ent, "k");
    }

    TEST_CASE("compat: parse + unparse reverses key order on every pass")
    {
        const std::string text =
            "{\n\"classname\" \"worldspawn\"\n\"wad\" \"a.wad\"\n}\n"
            "{\n\"classname\" \"info_player_start\"\n\"origin\" \"0 0 0\"\n}\n";
        const std::string reversed =
            "{\n\"wad\" \"a.wad\"\n\"classname\" \"worldspawn\"\n}\n"
            "{\n\"origin\" \"0 0 0\"\n\"classname\" \"info_player_start\"\n}\n";
        Parse(text);
        CHECK(g_numentities == 2);
        CHECK(Keys(&g_entities[0]) == V{"wad", "classname"});
        CHECK(Unparse() == reversed);
        Parse(reversed);
        CHECK(Unparse() == text);
    }

    TEST_CASE("entities without key/values are dropped by UnparseEntities")
    {
        Parse("{\n\"classname\" \"worldspawn\"\n}\n{\n}\n{\n\"classname\" \"info_null\"\n}\n");
        CHECK(g_numentities == 3);
        CHECK(Unparse() == "{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"info_null\"\n}\n");
    }

    TEST_CASE("light with _tex becomes light_surface while parsed and light again when unparsed")
    {
        Parse("{\n\"classname\" \"light\"\n\"_tex\" \"foo\"\n}\n");
        CHECK(std::string(ValueForKey(&g_entities[0], "classname")) == "light_surface");
        CHECK(std::string(ValueForKey(&g_entities[0], "convertto")) == "light");
        CHECK(Unparse() == "{\n\"_tex\" \"foo\"\n\"classname\" \"light\"\n}\n");
    }

    TEST_CASE("light_shadow/light_bounce round trip through convertfrom")
    {
        Parse("{\n\"classname\" \"light_spot\"\n\"convertfrom\" \"light_shadow\"\n}\n");
        CHECK(std::string(ValueForKey(&g_entities[0], "classname")) == "light_shadow");
        CHECK(std::string(ValueForKey(&g_entities[0], "convertto")) == "light_spot");
        CHECK(Unparse() == "{\n\"convertfrom\" \"light_shadow\"\n\"classname\" \"light_spot\"\n}\n");
    }

    TEST_CASE("light_environment _fake 1 is parsed as info_sunlight and unparsed as a new last entity")
    {
        Parse("{\n\"classname\" \"worldspawn\"\n}\n"
              "{\n\"classname\" \"light_environment\"\n\"_fake\" \"1\"\n\"angles\" \"0 90 0\"\n\"pitch\" \"-45\"\n}\n"
              "{\n\"classname\" \"info_null\"\n}\n");
        CHECK(std::string(ValueForKey(&g_entities[1], "classname")) == "info_sunlight");
        CHECK(Unparse() ==
              "{\n\"classname\" \"worldspawn\"\n}\n"
              "{\n\"classname\" \"info_null\"\n}\n"
              "{\n\"angles\" \"-45 90 0\"\n\"_fake\" \"1\"\n\"classname\" \"light_environment\"\n}\n");
    }

    TEST_CASE("info_sunlight-converted light_environment is dropped when parsed")
    {
        Parse("{\n\"classname\" \"worldspawn\"\n}\n"
              "{\n\"classname\" \"light_environment\"\n\"convertfrom\" \"info_sunlight\"\n}\n");
        CHECK(g_numentities == 1);
    }

    TEST_CASE("light_environment aimed at a target gets its angles from the direction")
    {
        Parse("{\n\"classname\" \"light_environment\"\n\"target\" \"t\"\n\"origin\" \"0 0 0\"\n}\n"
              "{\n\"classname\" \"info_target\"\n\"targetname\" \"t\"\n\"origin\" \"0 100 -100\"\n}\n");
        CHECK(Unparse() ==
              "{\n\"angles\" \"-45 90 0\"\n\"origin\" \"0 0 0\"\n\"target\" \"t\"\n\"classname\" \"light_environment\"\n}\n"
              "{\n\"origin\" \"0 100 -100\"\n\"targetname\" \"t\"\n\"classname\" \"info_target\"\n}\n");
    }

#ifdef HLCSG
    TEST_CASE("hlcsg: redundant named lights lose their targetname unless -nolightopt")
    {
        const std::string text =
            "{\n\"classname\" \"light\"\n\"targetname\" \"t\"\n\"style\" \"32\"\n}\n"
            "{\n\"classname\" \"light\"\n\"targetname\" \"t\"\n\"style\" \"32\"\n}\n"
            "{\n\"classname\" \"light\"\n\"targetname\" \"t\"\n\"style\" \"33\"\n}\n";
        Parse(text);
        g_nolightopt = false;
        const std::string out = Unparse();
        g_nolightopt = true;
        CHECK(out ==
              "{\n\"style\" \"32\"\n\"classname\" \"light\"\n}\n"
              "{\n\"style\" \"32\"\n\"targetname\" \"t\"\n\"classname\" \"light\"\n}\n"
              "{\n\"style\" \"33\"\n\"targetname\" \"t\"\n\"classname\" \"light\"\n}\n");

        Parse(text); // g_nolightopt == true: all three keep their targetname
        const std::string kept = Unparse();
        size_t count = 0;
        for (size_t pos = kept.find("\"targetname\""); pos != std::string::npos; pos = kept.find("\"targetname\"", pos + 1))
        {
            count++;
        }
        CHECK(count == 3);
    }
#endif

    TEST_CASE("FindTargetEntity and EntityForModel")
    {
        Parse("{\n\"classname\" \"worldspawn\"\n}\n"
              "{\n\"classname\" \"func_wall\"\n\"model\" \"*1\"\n\"targetname\" \"w\"\n}\n");
        CHECK(FindTargetEntity("w") == &g_entities[1]);
        CHECK(FindTargetEntity("nope") == nullptr);
        CHECK(EntityForModel(1) == &g_entities[1]);
        CHECK(EntityForModel(7) == &g_entities[0]); // compat: falls back to worldspawn
        ClearEntities();
    }
}
