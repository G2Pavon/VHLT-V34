#include "hlcsg/hlcsg.h"

#include <utility>

void FreeFaceList(bface_t *faces)
{
    while (faces)
    {
        delete std::exchange(faces, faces->next);
    }
}

bface_t *CopyFaceList(const bface_t *faces)
{
    bface_t *head = nullptr;
    bface_t **tail = &head;
    for (const bface_t *f = faces; f; f = f->next)
    {
        bface_t *n = new bface_t{};
        n->planenum = f->planenum;
        n->plane = f->plane;
        if (f->w)
        {
            n->w.reset(f->w->Copy());
        }
        n->texinfo = f->texinfo;
        n->contents = f->contents;
        n->backcontents = f->backcontents;
        n->bevel = f->bevel;
        n->bounds = f->bounds;
        *tail = n;
        tail = &n->next;
    }
    return head;
}
