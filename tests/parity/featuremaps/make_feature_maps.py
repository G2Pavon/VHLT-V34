#!/usr/bin/env python3
"""Generate the synthetic feature maps used by the parity corpus.

Each map exercises features that the Valve maps in the corpus never use (measured with gcov):
for hlcsg info_hullshape, CLIP*/BEVEL/HINT/SKIP/CONTENT* textures, detail brushes, zhlt_* keys,
info_compile_parameters, light styles, game_text conversion and its error paths; for hlbsp
(fm_bsp_*) a leak, brush models without visible or without any faces, and dense geometry for the
T-junction, subdivision and clip-hull brink code.

    make_feature_maps.py [OUTDIR]     (default: ./maps next to this script)

Every working map is a sealed room with a player start and a light, so the whole pipeline runs,
except fm_bsp_leak, whose room has a hole: hlbsp writes a point file instead of a portal file and
hlvis then stops. Maps named fm_err_* are expected to stop with an error in hlcsg. Output is
deterministic.
"""

import math
import sys
from pathlib import Path

ROOM_MIN = (-1024, -1024, -256)
ROOM_MAX = (1024, 1024, 768)
WALL = 32


# ---------------------------------------------------------------------------
# geometry


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def oriented(p0, p1, p2, outward):
    """Order three face points so that (p0 - p1) x (p2 - p1) points along the outward normal,
    the convention hlcsg's PlaneFromPoints uses."""
    n = cross(sub(p0, p1), sub(p2, p1))
    if dot(n, outward) < 0:
        p0, p2 = p2, p0
    assert dot(cross(sub(p0, p1), sub(p2, p1)), outward) > 0
    return p0, p1, p2


def tex_axes(normal):
    ax, ay, az = (abs(c) for c in normal)
    if az >= ax and az >= ay:
        return (1, 0, 0), (0, -1, 0)
    if ax >= ay:
        return (0, 1, 0), (0, 0, -1)
    return (1, 0, 0), (0, 0, -1)


class Face:
    def __init__(self, points, outward, texture):
        self.points = oriented(*points, outward)
        self.normal = outward
        self.texture = texture


def box(mins, maxs, texture):
    """Axis-aligned box. texture is a name, or a dict {"+x": ..., "-z": ..., "*": default}."""
    (x0, y0, z0), (x1, y1, z1) = mins, maxs

    def t(key):
        if isinstance(texture, str):
            return texture
        return texture.get(key, texture.get("*"))

    return [
        Face(((x1, y0, z0), (x1, y0, z1), (x1, y1, z0)), (1, 0, 0), t("+x")),
        Face(((x0, y0, z0), (x0, y1, z0), (x0, y0, z1)), (-1, 0, 0), t("-x")),
        Face(((x0, y1, z1), (x0, y1, z0), (x1, y1, z1)), (0, 1, 0), t("+y")),
        Face(((x1, y0, z1), (x1, y0, z0), (x0, y0, z1)), (0, -1, 0), t("-y")),
        Face(((x0, y0, z1), (x0, y1, z1), (x1, y0, z1)), (0, 0, 1), t("+z")),
        Face(((x0, y1, z0), (x0, y0, z0), (x1, y1, z0)), (0, 0, -1), t("-z")),
    ]


def prism(center, radius, sides, z0, z1, texture, rotation=0.0):
    """Regular polygon (vertices rounded to the grid) extruded from z0 to z1."""
    cx, cy = center
    verts = []
    for k in range(sides):
        a = rotation + 2 * math.pi * k / sides
        verts.append((round(cx + radius * math.cos(a)), round(cy + radius * math.sin(a))))
    faces = []
    for k in range(sides):
        (ax, ay), (bx, by) = verts[k], verts[(k + 1) % sides]
        outward = (by - ay, -(bx - ax), 0)  # edge rotated clockwise: points away from the center
        if dot(outward, (ax - cx, ay - cy, 0)) < 0:
            outward = tuple(-c for c in outward)
        faces.append(Face(((ax, ay, z0), (bx, by, z0), (ax, ay, z1)), outward, texture))
    (ax, ay), (bx, by), (qx, qy) = verts[0], verts[1], verts[2]
    faces.append(Face(((ax, ay, z1), (bx, by, z1), (qx, qy, z1)), (0, 0, 1), texture))
    faces.append(Face(((ax, ay, z0), (bx, by, z0), (qx, qy, z0)), (0, 0, -1), texture))
    return faces


def wedge(mins, maxs, texture):
    """A ramp rising along +x: the top face slopes (normal z > 0.7 for gentle ramps)."""
    (x0, y0, z0), (x1, y1, z1) = mins, maxs
    slope_n = (-(z1 - z0), 0, x1 - x0)
    return [
        Face(((x0, y0, z0), (x0, y1, z0), (x1, y0, z1)), slope_n, texture),  # slope
        Face(((x1, y0, z0), (x1, y0, z1), (x1, y1, z0)), (1, 0, 0), texture),
        Face(((x0, y1, z0), (x1, y1, z1), (x1, y1, z0)), (0, 1, 0), texture),
        Face(((x0, y0, z0), (x1, y0, z0), (x1, y0, z1)), (0, -1, 0), texture),
        Face(((x0, y1, z0), (x0, y0, z0), (x1, y1, z0)), (0, 0, -1), texture),
    ]


def sphere(radius, planes, texture):
    """Convex polyhedron of planes tangent to a sphere (Fibonacci directions); off-grid points."""
    faces = []
    golden = math.pi * (3 - math.sqrt(5))
    for i in range(planes):
        z = 1 - 2 * (i + 0.5) / planes
        r = math.sqrt(1 - z * z)
        n = (r * math.cos(golden * i), r * math.sin(golden * i), z)
        c = tuple(radius * x for x in n)
        a = (1, 0, 0) if abs(n[0]) < 0.9 else (0, 1, 0)
        u = cross(n, a)
        length = math.sqrt(dot(u, u))
        u = tuple(x / length for x in u)
        v = cross(n, u)
        pts = (tuple(round(c[k] + 64 * u[k], 3) for k in range(3)), tuple(round(x, 3) for x in c),
               tuple(round(c[k] + 64 * v[k], 3) for k in range(3)))
        faces.append(Face(pts, n, texture))
    return faces


def room(texture="C1A0_W1", floor="GENERIC015"):
    (x0, y0, z0), (x1, y1, z1) = ROOM_MIN, ROOM_MAX
    w = WALL
    return [
        box((x0 - w, y0 - w, z0 - w), (x1 + w, y1 + w, z0), floor),  # floor
        box((x0 - w, y0 - w, z1), (x1 + w, y1 + w, z1 + w), texture),  # ceiling
        box((x0 - w, y0, z0), (x0, y1, z1), texture),
        box((x1, y0, z0), (x1 + w, y1, z1), texture),
        box((x0, y0 - w, z0), (x1, y0, z1), texture),
        box((x0, y1, z0), (x1, y1 + w, z1), texture),
    ]


# ---------------------------------------------------------------------------
# map writer


def fmt(v):
    v = float(v)
    return str(int(v)) if v == int(v) else f"{v:.6f}".rstrip("0")


def entity(keys, brushes=()):
    return {"keys": keys, "brushes": list(brushes)}


def write_map(path, entities):
    out = []
    for i, ent in enumerate(entities):
        out.append(f"// entity {i}")
        out.append("{")
        for k, v in ent["keys"]:
            out.append(f'"{k}" "{v}"')
        for j, brush in enumerate(ent["brushes"]):
            out.append(f"// brush {j}")
            out.append("{")
            for f in brush:
                u, v = tex_axes(f.normal)
                pts = " ".join("( " + " ".join(fmt(c) for c in p) + " )" for p in f.points)
                out.append(f"{pts} {f.texture} [ {' '.join(map(str, u))} 0 ] [ {' '.join(map(str, v))} 0 ] 0 1 1")
            out.append("}")
        out.append("}")
    path.write_bytes(("\n".join(out) + "\n").encode("latin-1"))


def world(*brushes, extra=()):
    return entity([("classname", "worldspawn"), ("mapversion", "220"), *extra], [*room(), *brushes])


def start_and_light():
    return [
        entity([("classname", "info_player_start"), ("origin", "-512 -512 36"), ("angles", "0 0 0")]),
        entity([("classname", "light"), ("origin", "0 0 640"), ("_light", "255 255 255 400")]),
    ]


def origin_brush(cx, cy, cz):
    return box((cx - 8, cy - 8, cz - 8), (cx + 8, cy + 8, cz + 8), "ORIGIN")


# ---------------------------------------------------------------------------
# the maps


def fm_specials():
    """Special textures, contents, detail brushes and brush entities."""
    w = world(
        box((-900, -900, -256), (-800, -800, -156), "CRATE01"),
        box((-760, -900, -256), (-660, -800, -156), "+0BUTTON1"),  # animated texture
        box((-620, -900, -256), (-520, -800, -156), "-0BABTECH7"),  # random tiling
        box((-480, -900, -256), (-380, -800, -156), "TRANSLUCENT"),
        box((-340, -900, -256), (-240, -800, -100), "{FENCE"),
        box((-900, -700, -256), (-800, -600, 300), "HINT"),
        box((-760, -700, -256), (-660, -600, 300), {"*": "SKIP", "+z": "HINT"}),
        box((-620, -700, -256), (-520, -600, 300), "SOLIDHINT"),
        box((-480, -700, -256), (-380, -600, 300), "SPLITFACE"),
        box((-340, -700, -256), (-240, -600, -100), {"*": "C1A0_W1", "+z": "CONTENTWATER"}),
        box((-200, -700, -256), (-100, -600, -100), {"*": "C1A0_W1", "+z": "CONTENTEMPTY"}),
        box((-900, -500, -256), (-700, -300, -150), "!WATERBLUE"),
        box((-600, -500, 600), (-400, -300, 700), "SKY"),  # sky brush: hlcsg adds a clip copy
        box((-300, -500, -256), (-200, -400, 0), "CLIP"),
        box((-150, -500, -256), (-50, -400, 0), "CLIPHULL1"),
        box((0, -500, -256), (100, -400, 0), {"*": "CRATE01", "+z": "CLIPHULL2"}),  # mixed CLIP and visible
        box((150, -500, -256), (250, -400, 0), "CLIPBEVEL"),
        box((300, -500, -256), (400, -400, 0), "CLIPBEVELBRUSH"),
        box((450, -500, -256), (550, -400, 0), {"*": "CRATE01", "+x": "BEVEL"}),
        box((600, -500, -256), (700, -400, 0), {"*": "CRATE01", "-x": "BEVELBRUSH"}),
        box((750, -500, -256), (850, -400, 0), {"*": "CRATE01", "+y": "NOCLIP"}),
        box((900, -500, -256), (1000, -400, 0), {"*": "CRATE01", "-y": "NULLNOCLIP"}),
        box((-900, -200, -256), (-800, -100, -150), "AAATRIGGER"),
        prism((-500, 0), 96, 8, -256, -64, "CRATE01", rotation=math.pi / 8),  # non-axial planes
        prism((-200, 0), 80, 7, -256, -100, "CRATE01"),
        wedge((0, -100, -256), (300, 100, -200), "GENERIC015"),  # gentle slope (floor-like)
        wedge((400, -100, -256), (500, 100, -56), "CRATE01"),  # steep slope
    )
    detail = entity([("classname", "func_detail"), ("zhlt_detaillevel", "1"), ("zhlt_chopdown", "1"),
                     ("zhlt_clipnodedetaillevel", "1"), ("zhlt_coplanarpriority", "1")],
                    [box((-950, 300, -256), (-750, 500, -100), "CRATE01"),
                     box((-850, 400, -200), (-650, 600, -50), "CRATE01")])
    detail2 = entity([("classname", "func_detail"), ("zhlt_detaillevel", "2"), ("zhlt_chopup", "1")],
                     [box((-800, 350, -150), (-700, 450, 0), "C1A0_W1")])
    group = entity([("classname", "func_group")], [box((-500, 300, -256), (-400, 400, -150), "CRATE01")])
    ents = [
        w, detail, detail2, group,
        entity([("classname", "func_wall"), ("zhlt_noclip", "1")], [box((-300, 300, -256), (-200, 400, -100), "CRATE01")]),
        entity([("classname", "func_wall"), ("zhlt_invisible", "1")], [box((-150, 300, -256), (-50, 400, -100), "CRATE01")]),
        entity([("classname", "func_wall"), ("light_origin", "lo"), ("targetname", "w3")],
               [box((0, 300, -256), (100, 400, -100), "CRATE01")]),
        entity([("classname", "func_illusionary")], [box((150, 300, -256), (250, 400, -100), "{FENCE")]),
        entity([("classname", "func_door"), ("spawnflags", "8")],
               [box((300, 300, -256), (400, 400, -100), "CRATE01")]),
        entity([("classname", "func_door"), ("skin", "-3")],
               [box((450, 300, -256), (550, 400, -100), "CRATE01"),
                box((440, 290, -256), (560, 410, -90), "BOUNDINGBOX")]),
        entity([("classname", "func_conveyor"), ("spawnflags", "2")], [box((600, 300, -256), (700, 400, -200), "CRATE01")]),
        entity([("classname", "func_rotating"), ("spawnflags", "64")],
               [prism((800, 350), 50, 6, -256, -150, "CRATE01"), origin_brush(800, 350, -200)]),
        entity([("classname", "func_rot_button"), ("spawnflags", "1")],
               [box((900, 600, -256), (950, 650, -200), "+0BUTTON1"), origin_brush(925, 625, -228)]),
        entity([("classname", "trigger_multiple")], [box((-900, 700, -256), (-700, 900, 0), "AAATRIGGER")]),
        entity([("classname", "func_water")], [box((-600, 700, -256), (-400, 900, -150), "!WATERBLUE")]),
        entity([("classname", "env_bubbles")], [box((-300, 700, -256), (-200, 800, -150), "AAATRIGGER")]),
        entity([("classname", "info_null"), ("targetname", "lo"), ("origin", "50 350 0")]),
        *start_and_light(),
    ]
    return ents


def fm_hullshape():
    """info_hullshape: custom, default-replacing, empty and disabled hull shapes."""
    def shape(name, keys, brushes):
        return entity([("classname", "info_hullshape"), ("targetname", name), *keys], brushes)
    ents = [
        world(box((-900, -900, -256), (-700, -700, -100), "CRATE01"),
              wedge((-600, -900, -256), (-300, -700, -180), "GENERIC015")),
        shape("oct", [], [prism((0, 0), 16, 8, -36, 36, "NULL", rotation=math.pi / 8), origin_brush(0, 0, 0)]),
        shape("big", [("defaulthulls", "4")], [box((-40, -40, -40), (40, 40, 40), "NULL"), origin_brush(0, 0, 0)]),
        shape("none", [], [origin_brush(0, 0, 0)]),
        shape("off", [("disabled", "1")], [box((-16, -16, -16), (16, 16, 16), "NULL"), origin_brush(0, 0, 0)]),
        # replaces default hull 2 again: the brushes copied in by "big" are deleted
        shape("big2", [("defaulthulls", "4")], [box((-36, -36, -36), (36, 36, 36), "NULL"), origin_brush(0, 0, 0)]),
        # a second shape named "oct": brushes using it get a duplicate-name warning
        shape("oct", [], [box((-16, -16, -36), (16, 16, 36), "NULL"), origin_brush(0, 0, 0)]),
        entity([("classname", "func_wall"), ("zhlt_hull1", "oct")],
               [box((-200, -200, -256), (0, 0, -100), "CRATE01"), prism((200, 0), 64, 6, -256, -100, "CRATE01")]),
        entity([("classname", "func_wall"), ("zhlt_hull3", "none")], [box((300, 300, -256), (400, 400, -150), "CRATE01")]),
        entity([("classname", "func_wall"), ("zhlt_hull1", "off"), ("zhlt_hull2", "big")],
               [box((-400, 300, -256), (-300, 400, -150), "CRATE01")]),
        *start_and_light(),
    ]
    return ents


def fm_compileparams():
    """info_compile_parameters: hlcsg settings parsed from the map, including a larger texdata."""
    return [
        world(box((-900, -900, -256), (-700, -700, -100), "CRATE01"),
              box((-600, -500, 600), (-400, -300, 700), "SKY"),
              wedge((0, -100, -256), (300, 100, -200), "GENERIC015")),
        entity([("classname", "info_compile_parameters"), ("hlcsg", "1"), ("hlbsp", "1"), ("hlvis", "1"),
                ("hlrad", "1"), ("texdata", "65536"), ("estimate", "1"), ("cliptype", "3"),
                ("noclipeconomy", "0"), ("noskyclip", "0"), ("wadautodetect", "1"), ("bounce", "2"),
                ("chop", "64"), ("texchop", "32"), ("smooth", "50"), ("dscale", "1")]),
        *start_and_light(),
    ]


def fm_lightstyles():
    """Switchable texlights (style -1/-2/-3) before named lights, zhlt_usestyle, duplicate names."""
    def brushent(cls, keys, x):
        return entity([("classname", cls), *keys], [box((x, -900, -256), (x + 64, -836, -192), "+0BUTTON1")])
    return [
        world(box((-900, 0, -256), (-700, 200, -100), "CRATE01")),
        brushent("func_wall", [("style", "-1")], -900),
        brushent("func_wall", [("style", "-2")], -800),
        brushent("func_wall", [("style", "-3"), ("targetname", "lamp")], -700),
        brushent("func_wall", [("style", "-1")], -600),
        entity([("classname", "light"), ("targetname", "lamp"), ("origin", "-400 -400 100")]),
        entity([("classname", "light"), ("targetname", "lamp2"), ("origin", "-300 -400 100")]),
        entity([("classname", "light_spot"), ("targetname", "lamp"), ("origin", "-200 -400 100"),
                ("pitch", "-90"), ("_cone", "30"), ("_cone2", "45")]),
        entity([("classname", "light"), ("zhlt_usestyle", "lamp2"), ("origin", "-100 -400 100")]),
        entity([("classname", "light"), ("zhlt_usestyle", "null"), ("targetname", "lamp3"), ("origin", "0 -400 100")]),
        entity([("classname", "light"), ("targetname", "dup"), ("style", "0"), ("origin", "100 -400 100")]),
        entity([("classname", "light"), ("targetname", "dup"), ("style", "0"), ("origin", "200 -400 100")]),
        entity([("classname", "light_environment"), ("origin", "300 -400 100"), ("pitch", "-45"),
                ("_light", "255 255 200 100")]),
        *start_and_light(),
    ]


def fm_usemodel():
    """zhlt_usemodel: an entity reusing another's model (listed first, so it is moved), and "null"."""
    return [
        world(box((-900, 0, -256), (-700, 200, -100), "CRATE01")),
        entity([("classname", "func_wall"), ("zhlt_usemodel", "src")],
               [box((-100, -100, -256), (0, 0, -150), "CRATE01"), origin_brush(-50, -50, -200)]),
        entity([("classname", "func_wall"), ("zhlt_usemodel", "null")], [origin_brush(200, 200, -200)]),
        entity([("classname", "func_wall"), ("targetname", "src")], [box((300, -100, -256), (400, 0, -150), "CRATE01")]),
        *start_and_light(),
    ]


def fm_transform():
    """zhlt_transform: scale, move, scale+move, with an origin brush and a BOUNDINGBOX brush, and a bad value."""
    return [
        world(box((-900, 0, -256), (-700, 200, -100), "CRATE01")),
        entity([("classname", "func_wall"), ("zhlt_transform", "2")], [box((-600, -600, -256), (-550, -550, -200), "CRATE01")]),
        entity([("classname", "func_wall"), ("zhlt_transform", "0 32 0")], [box((-400, -600, -256), (-350, -550, -200), "CRATE01")]),
        entity([("classname", "func_wall"), ("zhlt_transform", "1.5 0 0 16")],
               [box((-200, -600, -256), (-150, -550, -200), "CRATE01"), origin_brush(-175, -575, -228)]),
        entity([("classname", "func_door"), ("zhlt_transform", "0.5 8 8 0")],
               [box((0, -600, -256), (64, -536, -192), "CRATE01"), box((-8, -608, -256), (72, -528, -184), "BOUNDINGBOX")]),
        entity([("classname", "func_wall"), ("zhlt_transform", "1 2")], [box((200, -600, -256), (250, -550, -200), "CRATE01")]),
        *start_and_light(),
    ]


def fm_gametext():
    """game_text messages in Windows-1252 (converted to UTF-8 by hlcsg), already UTF-8, and ASCII."""
    return [
        world(box((-900, 0, -256), (-700, 200, -100), "CRATE01")),
        entity([("classname", "game_text"), ("origin", "0 0 0"), ("message", "\x93Hello\x94 caf\xe9 \x80 100\x89")]),
        entity([("classname", "game_text"), ("origin", "0 0 0"), ("message", "caf\xc3\xa9 already utf-8")]),
        entity([("classname", "game_text"), ("origin", "0 0 0"), ("message", "plain ascii")]),
        *start_and_light(),
    ]


def leaky_room(gap):
    """room() with a gap of `gap` units in the +x wall, centred on y=0."""
    (x0, y0, z0), (x1, y1, z1) = ROOM_MIN, ROOM_MAX
    w = WALL
    walls = room()
    walls[3] = box((x1, y0, z0), (x1 + w, -gap // 2, z1), "C1A0_W1")  # +x wall, south part
    walls.append(box((x1, gap // 2, z0), (x1 + w, y1, z1), "C1A0_W1"))  # north part
    return walls


def fm_bsp_leak():
    """A 16-unit gap in the +x wall: hull 0 leaks (hlbsp writes .pts/.lin and no .prt); the expanded
    clip hulls close the gap. A leak in a second hull is undefined behaviour in the oracle (it writes
    the trail to the already closed point file), so that case is a candidate-only regress scenario."""
    walls = leaky_room(16)
    worldspawn = entity([("classname", "worldspawn"), ("mapversion", "220")],
                        [*walls, box((-200, -200, -256), (200, 200, -100), "CRATE01")])
    return [worldspawn, *start_and_light(),
            entity([("classname", "func_wall")], [box((600, 600, -256), (700, 700, -100), "CRATE01")])]


def fm_bsp_models():
    """Brush models hlbsp special-cases: no visible faces (the head node becomes a leaf and is split
    on an arbitrary plane), no faces at all ("Empty solid entity"), and empty clip hulls."""
    return [
        world(box((-900, -900, -256), (-800, -800, -156), "CRATE01")),
        entity([("classname", "func_wall"), ("targetname", "nulls")],
               [box((-600, -600, -256), (-500, -500, -100), "NULL")]),
        entity([("classname", "func_illusionary"), ("targetname", "clips")],
               [box((-400, -600, -256), (-300, -500, -100), "CLIP")]),
        entity([("classname", "func_wall"), ("zhlt_noclip", "1"), ("targetname", "empty")],
               [box((-200, -600, -256), (-100, -500, -100), "NULL")]),
        entity([("classname", "func_wall"), ("zhlt_noclip", "1"), ("targetname", "noclip")],
               [box((0, -600, -256), (100, -500, -100), "CRATE01")]),
        entity([("classname", "func_door"), ("targetname", "multi")],
               [box((200, -600, -256), (300, -500, -100), "CRATE01"),
                box((200, -450, -256), (300, -350, -100), "CRATE01"),
                prism((450, -550), 60, 5, -256, -100, "CRATE01")]),
        entity([("classname", "func_door_rotating"), ("targetname", "rot")],
               [box((600, -600, -256), (700, -500, 0), "CRATE01"), origin_brush(600, -550, -128)]),
        *start_and_light(),
    ]


def fm_bsp_geometry():
    """Dense geometry: stairs, slopes, a 40-sided column (faces above MAXPOINTS are split by tjunc),
    small boxes on large faces (T-junctions), rotated and off-grid brushes (clip-hull brinks)."""
    stairs = [box((-900 + 32 * i, -900, -256), (-868 + 32 * i, -700, -240 + 16 * i), "CRATE01") for i in range(16)]
    crates = [box((-300 + 40 * i, 400 + 40 * j, -256), (-276 + 40 * i, 424 + 40 * j, -232), "CRATE01")
              for i in range(8) for j in range(4)]
    column = prism((600, 600), 128, 40, -256, 768, "C1A0_W1")
    slopes = [wedge((-900, -600 + 120 * i, -256), (-900 + 100 + 60 * i, -500 + 120 * i, -256 + 30 * (i + 1)), "GENERIC015")
              for i in range(5)]
    rotated = [prism((200 + 150 * i, -700), 60, 4, -256, -100 - 20 * i, "CRATE01", rotation=0.2 * (i + 1)) for i in range(4)]
    ball = [Face(f.points, f.normal, f.texture) for f in sphere(90, 30, "CRATE01")]
    ball = [Face(tuple(tuple(round(c + d, 3) for c, d in zip(pt, (0, 0, 300))) for pt in f.points), f.normal, f.texture)
            for f in ball]
    ledge = box((-1024, 900, 200), (1024, 1024, 232), "C1A0_W1")  # long thin ledge along a wall
    return [world(*stairs, *crates, column, *slopes, *rotated, ball, ledge), *start_and_light()]


def err_map(*brushes, ents=()):
    return [world(*brushes), *ents, *start_and_light()]


def fm_err_coplanar():
    b = box((-100, -100, -256), (0, 0, -150), "CRATE01")
    b.append(b[0])  # the +x face twice
    return err_map(b)


def fm_err_mixed():
    return err_map(box((-100, -100, -256), (0, 0, -150), {"*": "CRATE01", "+z": "!WATERBLUE"}))


def fm_err_origin_world():
    return err_map(origin_brush(0, 0, -200))


def fm_err_outside():
    return err_map(box((40000, 0, 0), (40064, 64, 64), "CRATE01"))


def fm_err_noplane():
    b = box((-100, -100, -256), (0, 0, -150), "CRATE01")
    b[0].points = ((0, -100, -256), (0, -50, -256), (0, 0, -256))  # collinear
    return err_map(b)


def fm_err_two_origins():
    return err_map(ents=[entity([("classname", "func_rotating")],
                                [box((-100, -100, -256), (0, 0, -150), "CRATE01"),
                                 origin_brush(-50, -50, -200), origin_brush(-40, -40, -200)])])


def fm_err_detail_water():
    return err_map(ents=[entity([("classname", "func_detail"), ("zhlt_detaillevel", "1")],
                                [box((-100, -100, -256), (0, 0, -150), "!WATERBLUE")])])


def fm_err_entity_contents():
    return err_map(ents=[entity([("classname", "func_wall")], [box((-100, -100, -256), (0, 0, -150), "SKY")])])


def fm_err_hull_overflow():
    """A 256-plane info_hullshape brush has about 500 vertices: CreateHullBrush's vertex check was
    `> MAXSIZE`, so the oracle writes one element past its stack array before rejecting the brush."""
    return err_map(ents=[entity([("classname", "info_hullshape"), ("targetname", "ball")],
                                [sphere(200, 256, "NULL"), origin_brush(0, 0, 0)])])


MAPS = {f.__name__: f for f in [fm_specials, fm_hullshape, fm_compileparams, fm_lightstyles, fm_usemodel,
                                 fm_transform, fm_gametext, fm_err_coplanar, fm_err_mixed, fm_err_origin_world,
                                 fm_err_outside, fm_err_noplane, fm_err_two_origins, fm_err_detail_water,
                                 fm_err_entity_contents, fm_err_hull_overflow,
                                 fm_bsp_leak, fm_bsp_models, fm_bsp_geometry]}


def main() -> int:
    outdir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).with_name("maps")
    outdir.mkdir(parents=True, exist_ok=True)
    for name, make in MAPS.items():
        write_map(outdir / f"{name}.map", make())
    print(f"wrote {len(MAPS)} maps to {outdir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
