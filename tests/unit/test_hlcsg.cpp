#include "doctest/doctest.h"

#include <string>
#include <utility>
#include <vector>

#include "hlcsg/ansitoutf8.h"
#include "hlcsg/hlcsg.h"

namespace
{
// A hull whose faces have plane numbers `planes`, in list order, each with a one-point-off square.
brushhull_t Hull(const std::vector<int> &planes)
{
    brushhull_t h;
    bface_t **tail = &h.faces;
    for (int p : planes)
    {
        bface_t *f = new bface_t{};
        f->planenum = p;
        f->texinfo = p * 10;
        vec3_t pts[4] = {{0, 0, (vec_t)p}, {0, 1, (vec_t)p}, {1, 1, (vec_t)p}, {1, 0, (vec_t)p}};
        f->w = std::make_unique<Winding>(pts, 4);
        *tail = f;
        tail = &f->next;
    }
    return h;
}

std::vector<int> Planes(const brushhull_t &h)
{
    std::vector<int> out;
    for (const bface_t *f = h.faces; f; f = f->next)
    {
        out.push_back(f->planenum);
    }
    return out;
}
} // namespace

TEST_SUITE("hlcsg")
{
    TEST_CASE("brushhull_t copies are deep and keep the face order")
    {
        brushhull_t a = Hull({4, 2, 9});
        brushhull_t b = a;
        CHECK(Planes(b) == std::vector<int>{4, 2, 9});
        CHECK(b.faces != a.faces);
        CHECK(b.faces->w.get() != a.faces->w.get());
        CHECK(b.faces->next->texinfo == 20);
        CHECK(b.faces->next->w->m_Points[0][2] == 2);

        b.faces->w->m_Points[0][2] = 77; // the copy owns its windings
        CHECK(a.faces->w->m_Points[0][2] == 4);

        brushhull_t c = Hull({1});
        c = a;
        CHECK(Planes(c) == std::vector<int>{4, 2, 9});
        c = c; // self-assignment keeps the faces
        CHECK(Planes(c) == std::vector<int>{4, 2, 9});
    }

    TEST_CASE("brushhull_t moves hand over the faces")
    {
        brushhull_t a = Hull({3, 5});
        const bface_t *faces = a.faces;
        brushhull_t b = std::move(a);
        CHECK(b.faces == faces);
        CHECK(a.faces == nullptr);

        brushhull_t c = Hull({8});
        c = std::move(b);
        CHECK(c.faces == faces);
        CHECK(b.faces == nullptr);

        c.ClearFaces();
        CHECK(c.faces == nullptr);
    }

    TEST_CASE("brush_t copies hull faces through brushhull_t")
    {
        brush_t b{};
        b.hulls[2] = Hull({6, 7});
        b.hullshapes[1] = "shape";
        brush_t copy = b;
        CHECK(Planes(copy.hulls[2]) == std::vector<int>{6, 7});
        CHECK(copy.hulls[2].faces != b.hulls[2].faces);
        CHECK(copy.hullshapes[1] == "shape");
        CHECK(copy.hulls[0].faces == nullptr);
    }

    TEST_CASE("ANSItoUTF8 maps Windows-1252 to UTF-8")
    {
        CHECK(ANSItoUTF8("plain") == "plain");
        CHECK(ANSItoUTF8("") == "");
        CHECK(ANSItoUTF8("\x80") == "\xE2\x82\xAC");   // euro sign
        CHECK(ANSItoUTF8("caf\xE9") == "caf\xC3\xA9"); // latin-1 range
        CHECK(ANSItoUTF8("a\x81z") == "az");           // undefined in 1252: dropped
        CHECK(IsValidUTF8(ANSItoUTF8("\x93quoted\x94").c_str()));
    }
}
