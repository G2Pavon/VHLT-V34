#include "hlcsg/hlcsg.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <optional>
#include <cmath>
#include <string>
#include <vector>

#include "common/cmdlib.h"
#include "common/filelib.h"
#include "common/bspfile.h"
#include "common/log.h"
#include "common/mathtypes.h"
#include "common/mathlib.h"
#include "common/tokenizer.h"

int g_nummapbrushes;
brush_t g_mapbrushes[MAX_MAP_BRUSHES];

static int g_numbrushsides;
bside_t g_brushsides[MAX_MAP_SIDES];

static int g_nMapFileVersion; // map file version 220
static int g_numparsedentities;
static int g_numparsedbrushes;

static const vec3_t s_baseaxis[18] = {
    {0, 0, 1},
    {1, 0, 0},
    {0, -1, 0}, // floor
    {0, 0, -1},
    {1, 0, 0},
    {0, -1, 0}, // ceiling
    {1, 0, 0},
    {0, 1, 0},
    {0, 0, -1}, // west wall
    {-1, 0, 0},
    {0, 1, 0},
    {0, 0, -1}, // east wall
    {0, 1, 0},
    {1, 0, 0},
    {0, 0, -1}, // south wall
    {0, -1, 0},
    {1, 0, 0},
    {0, 0, -1}, // north wall
};

static brush_t *CopyCurrentBrush(entity_t *entity, const brush_t *brush)
{
    if (entity->firstbrush + entity->numbrushes != g_nummapbrushes)
    {
        Error("CopyCurrentBrush: internal error.");
    }
    brush_t *newb = &g_mapbrushes[g_nummapbrushes];
    g_nummapbrushes++;
    hlassume(g_nummapbrushes <= MAX_MAP_BRUSHES, assume_MAX_MAP_BRUSHES);
    *newb = *brush;
    newb->firstside = g_numbrushsides;
    g_numbrushsides += brush->numsides;
    hlassume(g_numbrushsides <= MAX_MAP_SIDES, assume_MAX_MAP_SIDES);
    std::memcpy(&g_brushsides[newb->firstside], &g_brushsides[brush->firstside], brush->numsides * sizeof(bside_t));
    newb->entitynum = entity - g_entities;
    newb->brushnum = entity->numbrushes;
    entity->numbrushes++;
    return newb;
}

static void DeleteCurrentEntity(entity_t *entity)
{
    if (entity != &g_entities[g_numentities - 1])
    {
        Error("DeleteCurrentEntity: internal error.");
    }
    if (entity->firstbrush + entity->numbrushes != g_nummapbrushes)
    {
        Error("DeleteCurrentEntity: internal error.");
    }
    for (int i = entity->numbrushes - 1; i >= 0; i--)
    {
        brush_t *b = &g_mapbrushes[entity->firstbrush + i];
        if (b->firstside + b->numsides != g_numbrushsides)
        {
            Error("DeleteCurrentEntity: internal error. (Entity %i, Brush %i)",
                  b->originalentitynum, b->originalbrushnum);
        }
        std::memset(&g_brushsides[b->firstside], 0, b->numsides * sizeof(bside_t));
        g_numbrushsides -= b->numsides;
    }
    for (int i = 0; i < entity->numbrushes; i++)
    {
        g_mapbrushes[entity->firstbrush + i] = brush_t{};
    }
    g_nummapbrushes -= entity->numbrushes;
    *entity = entity_t{};
    g_numentities--;
}

static bool CheckForInvisible(entity_t *mapent)
{
    std::string keyval(ValueForKey(mapent, "classname"));

    keyval.assign(ValueForKey(mapent, "zhlt_invisible"));
    if (!keyval.empty() && std::strcmp(keyval.c_str(), "0"))
    {
        return true;
    }

    return false;
}

static void ParseBrush(Tokenizer &tokens, entity_t *mapent)
{
    bool nullify = CheckForInvisible(mapent);
    hlassume(g_nummapbrushes < MAX_MAP_BRUSHES, assume_MAX_MAP_BRUSHES);

    brush_t *b = &g_mapbrushes[g_nummapbrushes];
    g_nummapbrushes++;
    b->firstside = g_numbrushsides;
    b->originalentitynum = g_numparsedentities;
    b->originalbrushnum = g_numparsedbrushes;
    b->entitynum = g_numentities - 1;
    b->brushnum = g_nummapbrushes - mapent->firstbrush - 1;

    b->noclip = 0;
    if (IntForKey(mapent, "zhlt_noclip"))
    {
        b->noclip = 1;
    }
    b->cliphull = 0;
    b->bevel = false;
    {
        b->detaillevel = IntForKey(mapent, "zhlt_detaillevel");
        b->chopdown = IntForKey(mapent, "zhlt_chopdown");
        b->chopup = IntForKey(mapent, "zhlt_chopup");
        b->clipnodedetaillevel = IntForKey(mapent, "zhlt_clipnodedetaillevel");
        b->coplanarpriority = IntForKey(mapent, "zhlt_coplanarpriority");
        bool wrong = false;
        if (b->detaillevel < 0)
        {
            wrong = true;
            b->detaillevel = 0;
        }
        if (b->chopdown < 0)
        {
            wrong = true;
            b->chopdown = 0;
        }
        if (b->chopup < 0)
        {
            wrong = true;
            b->chopup = 0;
        }
        if (b->clipnodedetaillevel < 0)
        {
            wrong = true;
            b->clipnodedetaillevel = 0;
        }
        if (wrong)
        {
            Warning("Entity %i, Brush %i: incorrect settings for detail brush.",
                    b->originalentitynum, b->originalbrushnum);
        }
    }
    for (int h = 0; h < NUM_HULLS; h++)
    {
        char key[16];
        std::sprintf(key, "zhlt_hull%d", h);
        const char *value = ValueForKey(mapent, key);
        b->hullshapes[h] = value;
    }

    mapent->numbrushes++;

    bool ok = tokens.Next(true);
    while (ok)
    {
        if (tokens.token() == "}")
        {
            break;
        }

        hlassume(g_numbrushsides < MAX_MAP_SIDES, assume_MAX_MAP_SIDES);
        bside_t *side = &g_brushsides[g_numbrushsides];
        g_numbrushsides++;

        b->numsides++;

        side->bevel = false;
        // read the three point plane definition
        for (int i = 0; i < 3; i++)
        {
            if (i != 0)
            {
                tokens.Next(true);
            }
            if (tokens.token() != "(")
            {
                Error("Parsing Entity %i, Brush %i, Side %i : Expecting '(' got '%s'",
                      b->originalentitynum, b->originalbrushnum,
                      b->numsides, tokens.token().c_str());
            }

            for (int j = 0; j < 3; j++)
            {
                tokens.Next(false);
                side->planepts[i][j] = std::atof(tokens.token().c_str());
            }

            tokens.Next(false);
            if (tokens.token() != ")")
            {
                Error("Parsing	Entity %i, Brush %i, Side %i : Expecting ')' got '%s'",
                      b->originalentitynum, b->originalbrushnum,
                      b->numsides, tokens.token().c_str());
            }
        }

        // read the     texturedef
        tokens.Next(false);
        std::string name = tokens.token();
        for (char &c : name)
        {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        // name is upper case, so these prefix tests match what strncasecmp matched
        {
            if (name.starts_with("NOCLIP") || name.starts_with("NULLNOCLIP"))
            {
                name = "NULL";
                b->noclip = true;
            }
            if (name.starts_with("BEVELBRUSH"))
            {
                name = "NULL";
                b->bevel = true;
            }
            if (name.starts_with("BEVEL"))
            {
                name = "NULL";
                side->bevel = true;
            }
            if (name.starts_with("CLIP"))
            {
                b->cliphull |= (1 << NUM_HULLS); // arbitrary nonexistent hull
                int h;
                if (name.starts_with("CLIPHULL") && (h = name[8] - '0', 0 < h && h < NUM_HULLS)) // name[8] is '\0' for "CLIPHULL"
                {
                    b->cliphull |= (1 << h); // hull h
                }
                if (name.starts_with("CLIPBEVEL"))
                {
                    side->bevel = true;
                }
                if (name.starts_with("CLIPBEVELBRUSH"))
                {
                    b->bevel = true;
                }
                name = "SKIP";
            }
        }
        safe_strncpy(side->td.name, name.c_str(), sizeof(side->td.name));

        // texture U axis
        tokens.Next(false);
        if (tokens.token() != "[")
        {
            hlassume(false, assume_MISSING_BRACKET_IN_TEXTUREDEF);
        }

        tokens.Next(false);
        side->td.vects.UAxis[0] = std::atof(tokens.token().c_str());
        tokens.Next(false);
        side->td.vects.UAxis[1] = std::atof(tokens.token().c_str());
        tokens.Next(false);
        side->td.vects.UAxis[2] = std::atof(tokens.token().c_str());
        tokens.Next(false);
        side->td.vects.shift[0] = std::atof(tokens.token().c_str());

        tokens.Next(false);
        if (tokens.token() != "]")
        {
            Error("missing ']' in texturedef (U)");
        }

        // texture V axis
        tokens.Next(false);
        if (tokens.token() != "[")
        {
            Error("missing '[' in texturedef (V)");
        }

        tokens.Next(false);
        side->td.vects.VAxis[0] = std::atof(tokens.token().c_str());
        tokens.Next(false);
        side->td.vects.VAxis[1] = std::atof(tokens.token().c_str());
        tokens.Next(false);
        side->td.vects.VAxis[2] = std::atof(tokens.token().c_str());
        tokens.Next(false);
        side->td.vects.shift[1] = std::atof(tokens.token().c_str());

        tokens.Next(false);
        if (tokens.token() != "]")
        {
            Error("missing ']' in texturedef (V)");
        }

        // Texture rotation is implicit in U/V axes.
        tokens.Next(false);
        side->td.vects.rotate = 0;

        // texure scale
        tokens.Next(false);
        side->td.vects.scale[0] = std::atof(tokens.token().c_str());
        tokens.Next(false);
        side->td.vects.scale[1] = std::atof(tokens.token().c_str());

        ok = tokens.Next(true); // Done with line, this reads the first item from the next line
    };
    if (b->cliphull != 0) // has CLIP* texture
    {
        unsigned int mask_anyhull = 0;
        for (int h = 1; h < NUM_HULLS; h++)
        {
            mask_anyhull |= (1 << h);
        }
        if ((b->cliphull & mask_anyhull) == 0) // no CLIPHULL1 or CLIPHULL2 or CLIPHULL3 texture
        {
            b->cliphull |= mask_anyhull; // CLIP all hulls
        }
    }

    contents_t contents = CheckBrushContents(b);
    b->contents = contents;
    for (int j = 0; j < b->numsides; j++)
    {
        bside_t *side = &g_brushsides[b->firstside + j];
        if (nullify && strncasecmp(side->td.name, "BEVEL", 5) && strncasecmp(side->td.name, "ORIGIN", 6) && strncasecmp(side->td.name, "HINT", 4) && strncasecmp(side->td.name, "SKIP", 4) && strncasecmp(side->td.name, "SOLIDHINT", 9) && strncasecmp(side->td.name, "SPLITFACE", 9) && strncasecmp(side->td.name, "BOUNDINGBOX", 11) && strncasecmp(side->td.name, "CONTENT", 7) && strncasecmp(side->td.name, "SKY", 3))
        {
            safe_strncpy(side->td.name, "NULL", sizeof(side->td.name));
        }
    }
    for (int j = 0; j < b->numsides; j++)
    {
        // change to SKIP now that we have set brush content.
        bside_t *side = &g_brushsides[b->firstside + j];
        if (!strncasecmp(side->td.name, "SPLITFACE", 9))
        {
            std::strcpy(side->td.name, "SKIP");
        }
    }
    for (int j = 0; j < b->numsides; j++)
    {
        bside_t *side = &g_brushsides[b->firstside + j];
        if (!strncasecmp(side->td.name, "CONTENT", 7))
        {
            std::strcpy(side->td.name, "NULL");
        }
    }

    for (int j = 0; j < b->numsides; j++)
    {
        bside_t *side = &g_brushsides[b->firstside + j];
        if (!strncasecmp(side->td.name, "AAATRIGGER", 10))
        {
            std::strcpy(side->td.name, "NULL");
        }
    }

    //
    // origin brushes are removed, but they set
    // the rotation origin for the rest of the brushes
    // in the entity
    //

    if (contents == CONTENTS_ORIGIN)
    {
        if (*ValueForKey(mapent, "origin"))
        {
            Error("Entity %i, Brush %i: Only one ORIGIN brush allowed.",
                  b->originalentitynum, b->originalbrushnum);
        }
        char string[MAXTOKEN];
        vec3_t origin;

        b->contents = CONTENTS_SOLID;
        CreateBrush(mapent->firstbrush + b->brushnum); // to get sizes
        b->contents = contents;

        for (int i = 0; i < NUM_HULLS; i++)
        {
            b->hulls[i].ClearFaces();
        }

        if (b->entitynum != 0) // Ignore for WORLD (code elsewhere enforces no ORIGIN in world message)
        {
            VectorAdd(b->hulls[0].bounds.m_Mins, b->hulls[0].bounds.m_Maxs, origin);
            VectorScale(origin, 0.5, origin);

            safe_snprintf(string, MAXTOKEN, "%i %i %i", (int)origin[0], (int)origin[1], (int)origin[2]);
            SetKeyValue(&g_entities[b->entitynum], "origin", string);
        }
    }
    if (*ValueForKey(&g_entities[b->entitynum], "zhlt_usemodel"))
    {
        std::memset(&g_brushsides[b->firstside], 0, b->numsides * sizeof(bside_t));
        g_numbrushsides -= b->numsides;
        *b = brush_t{};
        g_nummapbrushes--;
        mapent->numbrushes--;
        return;
    }
    if (!std::strcmp(ValueForKey(&g_entities[b->entitynum], "classname"), "info_hullshape"))
    {
        // all brushes should be erased, but not now.
        return;
    }
    if (contents == CONTENTS_BOUNDINGBOX)
    {
        if (*ValueForKey(mapent, "zhlt_minsmaxs"))
        {
            Error("Entity %i, Brush %i: Only one BoundingBox brush allowed.",
                  b->originalentitynum, b->originalbrushnum);
        }
        char string[MAXTOKEN];
        vec3_t mins, maxs;
        std::optional<std::string> origin;
        if (*ValueForKey(mapent, "origin"))
        {
            origin = ValueForKey(mapent, "origin");
            SetKeyValue(mapent, "origin", "");
        }

        b->contents = CONTENTS_SOLID;
        CreateBrush(mapent->firstbrush + b->brushnum); // to get sizes
        b->contents = contents;

        for (int i = 0; i < NUM_HULLS; i++)
        {
            b->hulls[i].ClearFaces();
        }

        if (b->entitynum != 0) // Ignore for WORLD (code elsewhere enforces no ORIGIN in world message)
        {
            VectorCopy(b->hulls[0].bounds.m_Mins, mins);
            VectorCopy(b->hulls[0].bounds.m_Maxs, maxs);

            safe_snprintf(string, MAXTOKEN, "%.0f %.0f %.0f %.0f %.0f %.0f", mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2]);
            SetKeyValue(&g_entities[b->entitynum], "zhlt_minsmaxs", string);
        }

        if (origin)
        {
            SetKeyValue(mapent, "origin", origin->c_str());
        }
    }
    if (g_skyclip && b->contents == CONTENTS_SKY && !b->noclip)
    {
        brush_t *newb = CopyCurrentBrush(mapent, b);
        newb->contents = CONTENTS_SOLID;
        newb->cliphull = ~0;
        for (int j = 0; j < newb->numsides; j++)
        {
            bside_t *side = &g_brushsides[newb->firstside + j];
            std::strcpy(side->td.name, "NULL");
        }
    }
    if (b->cliphull != 0 && b->contents == CONTENTS_TOEMPTY)
    {
        // check for mix of CLIP and normal texture
        bool mixed = false;
        for (int j = 0; j < b->numsides; j++)
        {
            bside_t *side = &g_brushsides[b->firstside + j];
            if (!strncasecmp(side->td.name, "NULL", 4))
            { // this is not supposed to be a HINT brush, so remove all invisible faces from hull 0.
                std::strcpy(side->td.name, "SKIP");
            }
            if (strncasecmp(side->td.name, "SKIP", 4))
                mixed = true;
        }
        if (mixed)
        {
            brush_t *newb = CopyCurrentBrush(mapent, b);
            newb->cliphull = 0;
        }
        b->contents = CONTENTS_SOLID;
        for (int j = 0; j < b->numsides; j++)
        {
            bside_t *side = &g_brushsides[b->firstside + j];
            std::strcpy(side->td.name, "NULL");
        }
    }
}

static bool ParseMapEntity(Tokenizer &tokens)
{
    g_numparsedbrushes = 0;
    if (!tokens.Next(true))
    {
        return false;
    }

    int this_entity = g_numentities;

    if (tokens.token() != "{")
    {
        Error("Parsing Entity %i, expected '{' got '%s'",
              g_numparsedentities,
              tokens.token().c_str());
    }

    hlassume(g_numentities < MAX_MAP_ENTITIES, assume_MAX_MAP_ENTITIES);
    g_numentities++;

    entity_t *mapent = &g_entities[this_entity];
    mapent->firstbrush = g_nummapbrushes;
    mapent->numbrushes = 0;

    while (1)
    {
        if (!tokens.Next(true))
            Error("ParseEntity: EOF without closing brace");

        if (tokens.token() == "}") // end of our context
            break;

        if (tokens.token() == "{") // must be a brush
        {
            ParseBrush(tokens, mapent);
            g_numparsedbrushes++;
        }
        else // else assume an epair
        {
            EntityKeyValue e = ParseEpair(tokens);
            if (mapent->numbrushes > 0)
                Warning("Error: ParseEntity: Keyvalue comes after brushes."); //--vluzacn

            if (!std::strcmp(e.key.c_str(), "mapversion"))
            {
                g_nMapFileVersion = std::atoi(e.value.c_str());
            }

            SetKeyValue(mapent, e.key.c_str(), e.value.c_str());
        }
    }

    if (*ValueForKey(mapent, "zhlt_usemodel"))
    {
        if (!*ValueForKey(mapent, "origin"))
            Warning("Entity %i: 'zhlt_usemodel' requires the entity to have an origin brush.",
                    g_numparsedentities);
        mapent->numbrushes = 0;
    }
    if (std::strcmp(ValueForKey(mapent, "classname"), "info_hullshape")) // info_hullshape is not affected by '-scale'
    {
        bool ent_move_b = false;
        bool ent_scale_b = false;
        bool ent_gscale_b = false;
        vec3_t ent_move = {0, 0, 0};
        vec3_t ent_scale_origin = {0, 0, 0};
        vec_t ent_scale = 1;
        vec_t ent_gscale = 1;

        double v[4] = {0, 0, 0, 0};
        if (*ValueForKey(mapent, "zhlt_transform"))
        {
            switch (std::sscanf(ValueForKey(mapent, "zhlt_transform"), "%lf %lf %lf %lf", v, v + 1, v + 2, v + 3))
            {
            case 1:
                ent_scale_b = true;
                ent_scale = v[0];
                break;
            case 3:
                ent_move_b = true;
                VectorCopy(v, ent_move);
                break;
            case 4:
                ent_scale_b = true;
                ent_scale = v[0];
                ent_move_b = true;
                VectorCopy(v + 1, ent_move);
                break;
            default:
                Warning("bad value '%s' for key 'zhlt_transform'", ValueForKey(mapent, "zhlt_transform"));
            }
            DeleteKey(mapent, "zhlt_transform");
        }
        GetVectorForKey(mapent, "origin", ent_scale_origin);

        if (ent_move_b || ent_scale_b || ent_gscale_b)
        {

            int ibrush, iside;
            brush_t *brush;
            bside_t *side;
            vec_t *point;
            for (ibrush = 0, brush = g_mapbrushes + mapent->firstbrush; ibrush < mapent->numbrushes; ++ibrush, ++brush)
            {
                for (iside = 0, side = g_brushsides + brush->firstside; iside < brush->numsides; ++iside, ++side)
                {
                    for (int ipoint = 0; ipoint < 3; ++ipoint)
                    {
                        point = side->planepts[ipoint];
                        if (ent_scale_b)
                        {
                            VectorSubtract(point, ent_scale_origin, point);
                            VectorScale(point, ent_scale, point);
                            VectorAdd(point, ent_scale_origin, point);
                        }
                        if (ent_move_b)
                        {
                            VectorAdd(point, ent_move, point);
                        }
                        if (ent_gscale_b)
                        {
                            VectorScale(point, ent_gscale, point);
                        }
                    }
                    // note that  tex->vecs = td.vects.valve.Axis / td.vects.valve.scale
                    //            tex->vecs[3] = vects.valve.shift + Dot(origin, tex->vecs)
                    //      and   texcoordinate = Dot(worldposition, tex->vecs) + tex->vecs[3]
                    bool zeroscale = false;
                    if (!side->td.vects.scale[0])
                    {
                        side->td.vects.scale[0] = 1;
                    }
                    if (!side->td.vects.scale[1])
                    {
                        side->td.vects.scale[1] = 1;
                    }
                    if (ent_scale_b)
                    {
                        vec_t coord[2];
                        if (std::abs(side->td.vects.scale[0]) > NORMAL_EPSILON)
                        {
                            coord[0] = DotProduct(ent_scale_origin, side->td.vects.UAxis) / side->td.vects.scale[0] + side->td.vects.shift[0];
                            side->td.vects.scale[0] *= ent_scale;
                            if (std::abs(side->td.vects.scale[0]) > NORMAL_EPSILON)
                            {
                                side->td.vects.shift[0] = coord[0] - DotProduct(ent_scale_origin, side->td.vects.UAxis) / side->td.vects.scale[0];
                            }
                            else
                            {
                                zeroscale = true;
                            }
                        }
                        else
                        {
                            zeroscale = true;
                        }
                        if (std::abs(side->td.vects.scale[1]) > NORMAL_EPSILON)
                        {
                            coord[1] = DotProduct(ent_scale_origin, side->td.vects.VAxis) / side->td.vects.scale[1] + side->td.vects.shift[1];
                            side->td.vects.scale[1] *= ent_scale;
                            if (std::abs(side->td.vects.scale[1]) > NORMAL_EPSILON)
                            {
                                side->td.vects.shift[1] = coord[1] - DotProduct(ent_scale_origin, side->td.vects.VAxis) / side->td.vects.scale[1];
                            }
                            else
                            {
                                zeroscale = true;
                            }
                        }
                        else
                        {
                            zeroscale = true;
                        }
                    }
                    if (ent_move_b)
                    {
                        if (std::abs(side->td.vects.scale[0]) > NORMAL_EPSILON)
                        {
                            side->td.vects.shift[0] -= DotProduct(ent_move, side->td.vects.UAxis) / side->td.vects.scale[0];
                        }
                        else
                        {
                            zeroscale = true;
                        }
                        if (std::abs(side->td.vects.scale[1]) > NORMAL_EPSILON)
                        {
                            side->td.vects.shift[1] -= DotProduct(ent_move, side->td.vects.VAxis) / side->td.vects.scale[1];
                        }
                        else
                        {
                            zeroscale = true;
                        }
                    }
                    if (ent_gscale_b)
                    {
                        side->td.vects.scale[0] *= ent_gscale;
                        side->td.vects.scale[1] *= ent_gscale;
                    }
                    if (zeroscale)
                    {
                        Error("Entity %i, Brush %i: invalid texture scale.\n",
                              brush->originalentitynum, brush->originalbrushnum);
                    }
                }
            }
            if (ent_gscale_b)
            {
                if (*ValueForKey(mapent, "origin"))
                {
                    double v[3];
                    int origin[3];
                    char string[MAXTOKEN];
                    GetVectorForKey(mapent, "origin", v);
                    VectorScale(v, ent_gscale, v);
                    for (int i = 0; i < 3; ++i)
                        origin[i] = (int)(v[i] >= 0 ? v[i] + 0.5 : v[i] - 0.5);
                    safe_snprintf(string, MAXTOKEN, "%d %d %d", origin[0], origin[1], origin[2]);
                    SetKeyValue(mapent, "origin", string);
                }
            }
            {
                double b[2][3];
                if (std::sscanf(ValueForKey(mapent, "zhlt_minsmaxs"), "%lf %lf %lf %lf %lf %lf", &b[0][0], &b[0][1], &b[0][2], &b[1][0], &b[1][1], &b[1][2]) == 6)
                {
                    for (int i = 0; i < 2; i++)
                    {
                        vec_t *point = b[i];
                        if (ent_scale_b)
                        {
                            VectorSubtract(point, ent_scale_origin, point);
                            VectorScale(point, ent_scale, point);
                            VectorAdd(point, ent_scale_origin, point);
                        }
                        if (ent_move_b)
                        {
                            VectorAdd(point, ent_move, point);
                        }
                        if (ent_gscale_b)
                        {
                            VectorScale(point, ent_gscale, point);
                        }
                    }
                    char string[MAXTOKEN];
                    safe_snprintf(string, MAXTOKEN, "%.0f %.0f %.0f %.0f %.0f %.0f", b[0][0], b[0][1], b[0][2], b[1][0], b[1][1], b[1][2]);
                    SetKeyValue(mapent, "zhlt_minsmaxs", string);
                }
            }
        }
    }

    CheckFatal();
    if (this_entity == 0)
    {
        // Let the map tell which version of the compiler it comes from, to help tracing compiler bugs.
        char versionstring[128];
        std::snprintf(versionstring, sizeof(versionstring), "ZHLT " ZHLT_VERSIONSTRING " " HACK_VERSIONSTRING " (%s)", VHLT_BUILD_DATE);
        SetKeyValue(mapent, "compiler", versionstring);
    }

    if (!std::strcmp(ValueForKey(mapent, "classname"), "info_compile_parameters"))
    {
        GetParamsFromEnt(mapent);
    }

    GetVectorForKey(mapent, "origin", mapent->origin);

    if (!std::strcmp("func_group", ValueForKey(mapent, "classname")) || !std::strcmp("func_detail", ValueForKey(mapent, "classname")))
    {
        // this is pretty gross, because the brushes are expected to be
        // in linear order for each entity

        int newbrushes = mapent->numbrushes;
        int worldbrushes = g_entities[0].numbrushes;

        // this entity's brushes are the last ones: rotate them down to the end of the world's
        // brushes, moving the other entities' brushes up
        if (mapent->firstbrush + newbrushes != g_nummapbrushes)
        {
            Error("ParseMapEntity: internal error: func_group brushes are not the last ones.");
        }
        std::rotate(g_mapbrushes + worldbrushes, g_mapbrushes + mapent->firstbrush, g_mapbrushes + g_nummapbrushes);
        for (int i = 0; i < newbrushes; i++)
        {
            g_mapbrushes[worldbrushes + i].entitynum = 0;
            g_mapbrushes[worldbrushes + i].brushnum += worldbrushes;
        }

        // fix up indexes
        g_numentities--;
        g_entities[0].numbrushes += newbrushes;
        for (int i = 1; i < g_numentities; i++)
        {
            g_entities[i].firstbrush += newbrushes;
        }
        *mapent = entity_t{};
        return true;
    }

    if (!std::strcmp(ValueForKey(mapent, "classname"), "info_hullshape"))
    {
        bool disabled = IntForKey(mapent, "disabled");
        const char *id = ValueForKey(mapent, "targetname");
        int defaulthulls = IntForKey(mapent, "defaulthulls");
        CreateHullShape(this_entity, disabled, id, defaulthulls);
        DeleteCurrentEntity(mapent);
        return true;
    }
    if (std::abs(mapent->origin[0]) > ENGINE_ENTITY_RANGE + ON_EPSILON ||
        std::abs(mapent->origin[1]) > ENGINE_ENTITY_RANGE + ON_EPSILON ||
        std::abs(mapent->origin[2]) > ENGINE_ENTITY_RANGE + ON_EPSILON)
    {
        const char *classname = ValueForKey(mapent, "classname");
        if (std::strncmp(classname, "light", 5))
        {
            Warning("Entity %i (classname \"%s\"): origin outside +/-%.0f: (%.0f,%.0f,%.0f)",
                    g_numparsedentities,
                    classname, (double)ENGINE_ENTITY_RANGE, mapent->origin[0], mapent->origin[1], mapent->origin[2]);
        }
    }
    return true;
}

static unsigned int CountEngineEntities()
{
    unsigned num_engine_entities = 0;
    entity_t *mapent = g_entities;

    // for each entity in the map
    for (unsigned int x = 0; x < g_numentities; x++, mapent++)
    {
        const char *classname = ValueForKey(mapent, "classname");

        // if its a light_spot or light_env, dont include it as an engine entity!
        if (classname)
        {
            if (!strncasecmp(classname, "light", 5) || !strncasecmp(classname, "light_spot", 10) || !strncasecmp(classname, "light_environment", 17))
            {
                const char *style = ValueForKey(mapent, "style");
                const char *targetname = ValueForKey(mapent, "targetname");

                // lightspots and lightenviroments dont have a targetname or style
                if (!std::strlen(targetname) && !std::atoi(style))
                {
                    continue;
                }
            }
        }

        num_engine_entities++;
    }

    return num_engine_entities;
}

void LoadMapFile(const char *const filename)
{
    const std::vector<char> text = ReadFile(filename);
    Tokenizer tokens(std::string(text.begin(), text.end()));

    g_numentities = 0;

    g_numparsedentities = 0;
    while (ParseMapEntity(tokens))
    {
        g_numparsedentities++;
    }

    unsigned num_engine_entities = CountEngineEntities();

    hlassume(num_engine_entities < MAX_ENGINE_ENTITIES, assume_MAX_ENGINE_ENTITIES);

    CheckFatal();
}