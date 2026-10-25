#!/usr/bin/env python3
"""VHLT parity harness: proves a candidate build produces byte-identical output to the oracle.

Workflow:
    parity.py build                       # oracle: git archive of tag oracle-base -> $DATA/builds/oracle
    parity.py corpus                      # .rmf/.map -> frozen corpus with canonical WAD lists
    parity.py ref --tier quick            # record oracle outputs after every stage
    parity.py build --tree --name cand    # build the current working tree with the oracle's flags
    parity.py check --bin $DATA/builds/cand/bin --tier quick
    parity.py bench --bin ... --maps cs_assault de_dust2 --repeat 3
    parity.py sanitize --tree             # ASan/UBSan build of the working tree, run on the corpus

Every tool runs with -threads 1: multi-threaded hlcsg is not deterministic even
between two runs of the same binary (plane order depends on thread scheduling).

Data lives outside the repo in $VHLT_PARITY_DATA (default: <repo>/../vhlt-parity).
"""

import argparse
import concurrent.futures
import datetime
import fnmatch
import functools
import hashlib
import itertools
import json
import os
import re
import shutil
import statistics
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import bspdiff
import mkwad
import stagecache

REPO = Path(__file__).resolve().parents[2]
DATA = Path(os.environ.get("VHLT_PARITY_DATA", REPO.parent / "vhlt-parity"))
GAME = Path(os.environ.get("VHLT_GAME_DIR", "~/.steam/steam/steamapps/common/Half-Life")).expanduser()
MESS = Path(os.environ.get("VHLT_MESS", REPO.parent / "mess_1.2.3_linux64" / "MESS"))
RMF_ROOT = Path(os.environ.get("VHLT_RMF_ROOT", REPO.parent / "GoldSrc-map-sources-restored"))
EXTRA_MAP_DIR = GAME / "cstrike" / "maps"
FEATURE_MAP_DIR = REPO / "tests" / "parity" / "featuremaps" / "maps"  # make_feature_maps.py

ORACLE_REF = "oracle-base"
# Pinned so that no compiler default (e.g. Clang's -ffp-contract=on) can change float results.
PARITY_CXX_FLAGS = "-ffp-contract=off -fno-fast-math"
SANITIZE_CXX_FLAGS = "-fsanitize=address,undefined -fsanitize-recover=address,undefined -fno-omit-frame-pointer -g"

STAGES = [("csg", "hlcsg"), ("bsp", "hlbsp"), ("vis", "hlvis"), ("rad", "hlrad")]
STAGE_TOOL = {"csg": "hlcsg", "ents": "hlcsg", "bsp": "hlbsp", "vis": "hlvis", "rad": "hlrad"}
PIPELINE = ["csg", "bsp", "vis", "rad"]
TIERS = {
    "quick": {"stages": PIPELINE, "csg": [], "bsp": [], "vis": ["-fast"], "rad": ["-fast"]},
    "full": {"stages": PIPELINE, "csg": [], "bsp": [], "vis": [], "rad": []},
}
# csg-only tiers: one per hlcsg option the default tiers never use. "ents" is a second hlcsg run.
CSG_VARIANTS = {
    "csg-default": [],  # hlcsg alone with default options (e.g. for --threads checks of hlcsg only)
    "csg-smallest": ["-cliptype", "smallest"],
    "csg-normalized": ["-cliptype", "normalized"],
    "csg-precise": ["-cliptype", "precise"],
    "csg-legacy": ["-cliptype", "legacy"],
    "csg-clipeconomy": ["-clipeconomy"],
    "csg-noskyclip": ["-noskyclip"],
    "csg-nolightopt": ["-nolightopt"],
    "csg-nowadtextures": ["-nowadtextures"],
    "csg-wadinclude": ["-wadinclude", "halflife.wad"],
    "csg-misc": ["-wadautodetect", "-notextconvert", "-chart", "-estimate"],
}
for _name, _args in CSG_VARIANTS.items():
    TIERS[_name] = {"stages": ["csg"], "csg": _args}
TIERS["csg-onlyents"] = {"stages": ["csg", "ents"], "csg": [], "ents": ["-onlyents"]}
VARIANT_TIERS = [*CSG_VARIANTS, "csg-onlyents"]
# hlbsp option tiers: hlcsg with default options, then hlbsp with one option set each.
BSP_VARIANTS = {
    "bsp-nohull2": ["-nohull2"],
    "bsp-viewportal": ["-viewportal"],
    "bsp-subdivide64": ["-subdivide", "64"],
    "bsp-subdivide512": ["-subdivide", "512"],
    "bsp-maxnodesize64": ["-maxnodesize", "64"],
    "bsp-maxnodesize65536": ["-maxnodesize", "65536"],
    "bsp-clamped": ["-subdivide", "16", "-maxnodesize", "32"],  # out-of-range values: warnings, then clamped
    "bsp-misc": ["-chart", "-estimate", "-texdata", "65536", "-lightdata", "65536"],
}
for _name, _args in BSP_VARIANTS.items():
    TIERS[_name] = {"stages": ["csg", "bsp"], "csg": [], "bsp": _args}
BSP_VARIANT_TIERS = [*BSP_VARIANTS]
# hlvis option tiers: hlcsg and hlbsp with default options, then hlvis with one option set each.
VIS_VARIANTS = {
    "vis-full": ["-full"],
    "vis-maxdistance512": ["-maxdistance", "512"],
    "vis-maxdistance2048": ["-maxdistance", "2048"],
    "vis-misc": ["-chart", "-estimate", "-texdata", "65536", "-lightdata", "65536"],
}
for _name, _args in VIS_VARIANTS.items():
    TIERS[_name] = {"stages": ["csg", "bsp", "vis"], "csg": [], "bsp": [], "vis": _args}
VIS_VARIANT_TIERS = [*VIS_VARIANTS]
# hlrad option tiers: hlrad alone, on the oracle's VIS-complete output of the full tier (pre-baked
# input, refs/full/<map>/vis), so neither the references nor a check run csg, bsp or vis again.
RAD_INPUT = ("full", "vis")
RAD_VARIANTS = {
    "rad-extra": ["-extra"],
    "rad-bounce0": ["-bounce", "0"],
    "rad-bounce1": ["-bounce", "1"],
    "rad-bounce16": ["-bounce", "16"],
    "rad-chop": ["-chop", "32", "-texchop", "16"],
    "rad-smooth": ["-smooth", "80", "-smooth2", "20"],
    "rad-vismatrix-normal": ["-vismatrix", "normal"],
    "rad-vismatrix-off": ["-vismatrix", "off"],
    "rad-compress0": ["-compress", "0"],  # FLOAT32 transfers (default FLOAT16)
    "rad-compress2": ["-compress", "2"],  # FLOAT8
    "rad-rgb": ["-rgbtransfers"],  # VECTOR32 (default)
    "rad-rgb0": ["-rgbtransfers", "-rgbcompress", "0"],  # VECTOR96
    "rad-rgb1": ["-rgbtransfers", "-rgbcompress", "1"],  # VECTOR48
    "rad-rgb3": ["-rgbtransfers", "-rgbcompress", "3"],  # VECTOR24
    "rad-customshadow": ["-customshadowwithbounce"],
    "rad-sky": ["-sky", "0.5", "-softsky", "0", "-nospread", "-noskyfix"],
    "rad-colour": ["-gamma", "0.8", "-scale", "1.5", "-limiter", "-1", "-minlight", "20", "-coring", "0.1",
                   "-ambient", "0.1", "0.1", "0.1", "-circus", "-dscale", "2", "-blur", "3"],
    "rad-colour2": ["-colourgamma", "0.6", "0.5", "0.4", "-colourscale", "1", "2", "3", "-texreflectgamma", "1.5",
                    "-texreflectscale", "0.9", "-fade", "2", "-depth", "4", "-blockopaque", "0", "-dlight", "5"],
    "rad-notextures": ["-notextures"],
    "rad-misc": ["-chart", "-estimate", "-texdata", "65536", "-lightdata", "65536", "-notexscale", "-nosubdivide",
                 "-texlightgap", "2"],
}
for _name, _args in RAD_VARIANTS.items():
    TIERS[_name] = {"stages": ["rad"], "rad": _args, "input": RAD_INPUT}
RAD_VARIANT_TIERS = [*RAD_VARIANTS]
TIER_GROUPS = {"csg-variants": VARIANT_TIERS, "bsp-variants": BSP_VARIANT_TIERS, "vis-variants": VIS_VARIANT_TIERS,
               "rad-variants": RAD_VARIANT_TIERS}


def tier_stages(tier: str) -> list[str]:
    return TIERS[tier]["stages"]


def expand_tiers(tier: str) -> list[str]:
    return TIER_GROUPS.get(tier, [tier])
STAGE_TIMEOUT = 4 * 3600
CACHE_ROOT = DATA / "cache"  # stagecache.py: tool runs and comparisons, content-addressed
DEFAULT_JOBS = max(1, os.cpu_count() or 1)  # tools run single-threaded; hlrad peaks at ~160 MB per map

# Maps left out of the compile corpus (fnmatch patterns on the map name). dm_* (dm_stalkfire_f1)
# cost about a third of the full tier's wall time without covering anything the other maps don't.
# The stock dm_stalkfire_f1.bsp is still used by probe and the unit tests: it is the only BSP with
# embedded lightmaps, and loading it takes well under a second.
EXCLUDED_MAPS = ("dm_*",)
# Maps kept in the corpus (with their oracle references) but left out of every routine run: they cost
# far more than they cover. They run only with --archived, or when named in --maps (e.g. for a final
# wrap-up). liminal_plaza (archived 2026-10-18): about 70% of all vis time and the slowest hlrad map.
# The one path only it reached: hlbsp's "Exceeded MAX_MAP_VERTS" in bsp-clamped/bsp-subdivide64.
ARCHIVED_MAPS = ("liminal_plaza",)
INCLUDE_ARCHIVED = False  # set by --archived
# The routine test set (default for check, sanitize, bench, coverage; --full selects the whole
# corpus). Chosen 2026-10-18 by per-map line coverage of hlcsg/hlbsp/hlvis over quick, every csg,
# bsp and vis tier and the full tier: a greedy cost-weighted cover, then the expensive tail replaced
# (fm_clipeconomy covers what only deathrun_arctic reached). It covers every line the whole corpus
# covers but three (a brink loop exit: as_forest/deathrun_arctic; the "world faces exceeded"
# warning: cnd_kiwi_bhop; a tjunc split branch: de_vertigo and others) at ~14% of the corpus' tool
# time. hlrad was not measured: revisit in its phase. `ref` always records the whole corpus.
CORE_MAPS = (
    "cs_assault", "cs_estate", "cs_militia", "de_dust2", "plants_soc_final", "unnamed-compile",
    "fm_bsp_leak", "fm_bsp_models", "fm_clipeconomy", "fm_compileparams", "fm_gametext", "fm_hullshape",
    "fm_lightstyles", "fm_specials", "fm_transform", "fm_usemodel", "fm_vis_overview",
    "fm_err_coplanar", "fm_err_detail_water", "fm_err_entity_contents", "fm_err_hull_overflow",
    "fm_err_mixed", "fm_err_noplane", "fm_err_origin_world", "fm_err_two_origins",
)
FULL_CORPUS = False  # set by --full (and always for ref)
# The rad micro-corpus: the default map set for hlrad runs (rad tiers, --stages rad, coverage --tool
# hlrad). Chosen in R0 by per-map hlrad line coverage over the quick, full and rad tiers: the
# lighting feature maps and unnamed reach 6429 of the 6541 lines the rad pool reaches, cs_estate
# (the cheapest real map with sky, smoothing and seam handling at scale) 87 more; the 25 left need
# the other heavy maps (README). ~10% of the pool's rad-tier time.
RAD_CORE_MAPS = (
    "cs_estate", "unnamed",
    "fm_rad_brushkeys", "fm_rad_compileparams", "fm_rad_err_ambient", "fm_rad_err_spread", "fm_rad_infoents",
    "fm_rad_opaque", "fm_rad_point", "fm_rad_sky", "fm_rad_spot", "fm_rad_styles", "fm_rad_texlights",
)
RAD_ROUTINE = False  # set when the run is about hlrad
# What the rad tiers ever run, also with --full and for their references: the feature maps and the
# real maps whose full-tier hlrad takes at most ~31 s. The quick and full tiers light every map at
# default options; option coverage does not need the heavy maps (as_forest alone is 172 s a run).
RAD_TIER_REAL_MAPS = ("unnamed", "unnamed-compile", "de_train", "cs_estate", "cs_747", "cs_militia", "cs_assault",
                      "de_vertigo", "cs_siege", "de_aztec", "de_dust2")
RAD_TIER = False  # set when the tier is a rad tier

MISSING_TEXTURE_RE = re.compile(r"::FindTexture\(\) texture (\S+) not found!")


# ---------------------------------------------------------------------------
# helpers


def log(msg: str) -> None:
    print(msg, flush=True)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def run(cmd: list, cwd: Path | None = None, **kw) -> subprocess.CompletedProcess:
    return subprocess.run([str(c) for c in cmd], cwd=cwd, **kw)


def write_json(path: Path, obj) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, indent=2, sort_keys=True) + "\n")


def read_json(path: Path):
    return json.loads(path.read_text())


def snapshot(src: Path, dst: Path) -> None:
    """Copy every regular file of src into a fresh dst."""
    if dst.exists():
        shutil.rmtree(dst)
    dst.mkdir(parents=True)
    for f in src.iterdir():
        if f.is_file():
            shutil.copy2(f, dst / f.name)


def is_excluded(name: str) -> bool:
    return any(fnmatch.fnmatchcase(name, pattern) for pattern in EXCLUDED_MAPS)


def corpus_maps(archived: bool | None = None) -> list[str]:
    """The compile corpus; with archived False (default: unless --archived) without ARCHIVED_MAPS."""
    manifest = DATA / "corpus" / "manifest.json"
    if not manifest.exists():
        sys.exit(f"no corpus at {manifest.parent}; run 'parity.py corpus' first")
    if archived is None:
        archived = INCLUDE_ARCHIVED
    return sorted(m for m in read_json(manifest)["maps"]
                  if not is_excluded(m) and (archived or m not in ARCHIVED_MAPS))


def routine_maps() -> list[str]:
    """What a check runs by default: CORE_MAPS, or the corpus with --full (plus --archived maps)."""
    maps = corpus_maps()
    if RAD_TIER:  # rad tiers never leave their pool
        pool = [m for m in maps if m.startswith("fm_") or m in RAD_TIER_REAL_MAPS]
        if FULL_CORPUS or not RAD_CORE_MAPS:
            return pool
        return sorted(m for m in pool if m in RAD_CORE_MAPS)
    if FULL_CORPUS:
        return maps
    core = RAD_CORE_MAPS if RAD_ROUTINE and RAD_CORE_MAPS else CORE_MAPS
    return sorted(m for m in maps if m in core or m in (ARCHIVED_MAPS if INCLUDE_ARCHIVED else ()))


def select_maps(requested: list[str] | None) -> list[str]:
    available = corpus_maps(archived=True)  # archived maps can always be named
    if not requested:
        return routine_maps()
    unknown = sorted(set(requested) - set(available))
    if unknown:
        sys.exit(f"unknown maps: {', '.join(unknown)}")
    return requested


def check_bin(bindir: Path) -> Path:
    bindir = bindir.resolve()
    for _, tool in STAGES:
        if not (bindir / tool).is_file():
            sys.exit(f"{bindir}: missing {tool}")
    # hlbsp falls back to <exe dir>/entities.void; a stray one would make runs incomparable.
    if (bindir / "entities.void").exists():
        sys.exit(f"{bindir}: contains entities.void, which changes hlbsp output; remove it")
    return bindir


# ---------------------------------------------------------------------------
# build


MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]


def oracle_build_date() -> str:
    """The oracle's __DATE__ ("Mmm dd yyyy", day space-padded), from its manifest."""
    manifest = read_json(DATA / "builds" / "oracle" / "manifest.json")
    if "build_date" in manifest:
        return manifest["build_date"]
    d = datetime.datetime.fromisoformat(manifest["built"])
    return f"{MONTHS[d.month - 1]} {d.day:2d} {d.year}"


def cmd_build(args) -> int:
    name = args.name or ("oracle" if not args.tree else "tree")
    # The build date is written into every .bsp (worldspawn "compiler" key) and into the binaries,
    # so candidates are built with the oracle's date. The oracle itself is never rebuilt.
    build_date = args.build_date
    if build_date is None and name != "oracle" and (DATA / "builds" / "oracle" / "manifest.json").exists():
        build_date = oracle_build_date()
    flags = PARITY_CXX_FLAGS if args.cxx_flags is None else args.cxx_flags
    if args.sanitize:
        flags = f"{flags} {SANITIZE_CXX_FLAGS}".strip()
    dest = DATA / "builds" / name
    if dest.exists():
        if not args.force:
            sys.exit(f"{dest} exists; pass --force to rebuild")
        shutil.rmtree(dest)
    dest.mkdir(parents=True)

    if args.tree:
        src = REPO
        commit = run(["git", "-C", REPO, "rev-parse", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()
        dirty = bool(run(["git", "-C", REPO, "status", "--porcelain", "--", "src", "CMakeLists.txt"],
                         capture_output=True, text=True, check=True).stdout.strip())
        source = f"working tree at {commit}" + (" (with uncommitted changes)" if dirty else "")
    else:
        ref = args.ref or ORACLE_REF
        commit = run(["git", "-C", REPO, "rev-parse", f"{ref}^{{commit}}"], capture_output=True, text=True, check=True).stdout.strip()
        src = dest / "src"
        src.mkdir()
        archive = subprocess.Popen(["git", "-C", str(REPO), "archive", commit], stdout=subprocess.PIPE)
        run(["tar", "-x", "-C", src], stdin=archive.stdout, check=True)
        if archive.wait() != 0:
            sys.exit("git archive failed")
        source = f"git archive of {ref} ({commit})"

    build = dest / "build"
    generator = ["-G", "Ninja"] if shutil.which("ninja") else []
    run(["cmake", "--no-warn-unused-cli", "-S", src, "-B", build, *generator,
         "-DCMAKE_BUILD_TYPE=Release",
         f"-DCMAKE_CXX_COMPILER={args.cxx}",
         f"-DCMAKE_CXX_FLAGS={flags}",
         "-DVHLT_BUILD_TESTS=OFF",
         *([f"-DVHLT_BUILD_DATE={build_date}"] if build_date else [])], check=True, stdout=subprocess.DEVNULL)
    proc = run(["cmake", "--build", build, "--parallel"], capture_output=True, text=True)
    (dest / "build.log").write_text(proc.stdout + proc.stderr)
    if proc.returncode != 0:
        sys.exit(f"build failed, see {dest / 'build.log'}")

    bindir = dest / "bin"
    bindir.mkdir()
    for _, tool in STAGES:
        shutil.copy2(build / "bin" / tool, bindir / tool)

    compiler = run([args.cxx, "--version"], capture_output=True, text=True).stdout.splitlines()[0]
    manifest = {
        "name": name,
        "source": source,
        "commit": commit,
        "compiler": compiler,
        "cmake_build_type": "Release",
        "cxx_flags": flags,
        "build_date": build_date,
        "built": datetime.datetime.now().isoformat(timespec="seconds"),
        "binaries": {tool: sha256(bindir / tool) for _, tool in STAGES},
    }
    write_json(dest / "manifest.json", manifest)
    if not args.tree:
        shutil.rmtree(build)  # the binaries and src are kept; the build tree is not needed
    log(f"built {name}: {bindir}")
    log(f"  {source}")
    log(f"  {compiler}; flags: {flags}")
    return 0


# ---------------------------------------------------------------------------
# corpus


def canonical_wads(wad_dir: Path) -> list[Path]:
    """Freeze zhlt.wad and the stock valve/cstrike WADs into the corpus, in a fixed order."""
    groups = [("zhlt", [REPO / "tools" / "zhlt.wad"]),
              ("valve", sorted((GAME / "valve").glob("*.wad"))),
              ("cstrike", sorted((GAME / "cstrike").glob("*.wad")))]
    out = []
    for group, files in groups:
        for f in files:
            dst = wad_dir / group / f.name
            dst.parent.mkdir(parents=True, exist_ok=True)
            if not dst.exists():
                shutil.copy2(f, dst)
            out.append(dst)
    return out


WORLDSPAWN_WAD_RE = re.compile(r'^(\s*)"wad"\s*"([^"]*)"\s*$', re.MULTILINE)


def worldspawn_span(text: str) -> tuple[int, int]:
    """Character range of the first entity's key/value block (up to its first brush or closing brace)."""
    start = text.index("{")
    m = re.compile(r"^\s*[{}]", re.MULTILINE).search(text, start + 1)
    return start, (m.start() if m else len(text))


def get_wad_key(text: str) -> list[str]:
    s, e = worldspawn_span(text)
    m = WORLDSPAWN_WAD_RE.search(text, s, e)
    return [w for w in m.group(2).split(";") if w] if m else []


def set_wad_key(text: str, wads: list[str]) -> str:
    s, e = worldspawn_span(text)
    value = ";".join(wads)
    m = WORLDSPAWN_WAD_RE.search(text, s, e)
    if m:
        return text[: m.start()] + f'{m.group(1)}"wad" "{value}"' + text[m.end():]
    nl = text.index("\n", s) + 1  # insert right after the opening brace line
    return text[:nl] + f'"wad" "{value}"\n' + text[nl:]


def probe_missing_textures(mapfile: Path, oracle_bin: Path) -> tuple[int, list[str]]:
    with tempfile.TemporaryDirectory(dir=DATA) as tmp:
        tmp = Path(tmp)
        shutil.copy2(mapfile, tmp / mapfile.name)
        proc = run([oracle_bin / "hlcsg", mapfile.stem], cwd=tmp, capture_output=True, text=True, errors="replace",
                   timeout=STAGE_TIMEOUT)
        return proc.returncode, sorted(set(MISSING_TEXTURE_RE.findall(proc.stdout)))


def cmd_corpus(args) -> int:
    oracle_bin = check_bin(DATA / "builds" / "oracle" / "bin")
    root = DATA / "corpus"
    previous = read_json(root / "manifest.json")["maps"] if (root / "manifest.json").exists() else {}
    if root.exists():
        if not args.force:
            sys.exit(f"{root} exists; pass --force to rebuild")
        shutil.rmtree(root)
    maps_dir, wad_dir, mkwad = root / "maps", root / "wads", Path(__file__).with_name("mkwad.py")
    maps_dir.mkdir(parents=True)

    canon = canonical_wads(wad_dir)
    stock = {w.resolve() for w in [REPO / "tools" / "zhlt.wad", *(GAME / "valve").glob("*.wad"), *(GAME / "cstrike").glob("*.wad"), *canon]}
    sources: dict[str, dict] = {}

    for rmf in sorted(RMF_ROOT.rglob("*.rmf")):
        name = rmf.stem
        if is_excluded(name):
            continue
        out = maps_dir / f"{name}.map"
        proc = run([MESS, "-convert", "-mapformat", "trenchbroomvalve220", "-tbgame", "Half-Life",
                    "-wad", ";".join(map(str, canon)), rmf, out], capture_output=True, text=True)
        if proc.returncode != 0 or not out.exists():
            log(f"  MESS failed on {rmf}:\n{proc.stdout[-2000:]}{proc.stderr[-2000:]}")
            return 1
        sources[name] = {"source": str(rmf), "source_sha256": sha256(rmf), "converted_by": "MESS 1.2.3 (trenchbroomvalve220)"}
        log(f"  converted {name}")

    for m in [*sorted(EXTRA_MAP_DIR.glob("*.map")), *sorted(FEATURE_MAP_DIR.glob("*.map"))]:
        if is_excluded(m.stem):
            continue
        if m.stem in sources:
            sys.exit(f"duplicate map name {m.stem}")
        shutil.copy2(m, maps_dir / m.name)
        sources[m.stem] = {"source": str(m), "source_sha256": sha256(m), "converted_by": None}
        log(f"  copied {m.stem}")

    manifest = {"created": datetime.datetime.now().isoformat(timespec="seconds"),
                "canonical_wads": [str(w.relative_to(root)) for w in canon], "maps": {}}
    for name in sorted(sources):
        mapfile = maps_dir / f"{name}.map"
        text = mapfile.read_text(encoding="latin-1")

        # Keep the map's own WADs that still exist (frozen), ahead of the stock ones.
        own = []
        for w in get_wad_key(text):
            p = Path(w.replace("\\", "/"))
            if p.is_file() and p.resolve() not in stock:
                dst = wad_dir / "extra" / name / p.name
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(p, dst)
                own.append(dst)
        wads = own + canon
        mapfile.write_text(set_wad_key(text, list(map(str, wads))), encoding="latin-1")

        status, missing = probe_missing_textures(mapfile, oracle_bin)
        if missing:
            stub = wad_dir / "stub" / f"{name}_stub.wad"
            stub.parent.mkdir(parents=True, exist_ok=True)
            run([sys.executable, mkwad, stub, *missing], check=True)
            wads.append(stub)
            mapfile.write_text(set_wad_key(text, list(map(str, wads))), encoding="latin-1")
            status, still = probe_missing_textures(mapfile, oracle_bin)
            if still:
                log(f"  {name}: still missing after stub WAD: {still}")
                return 1
        log(f"  {name}: {len(own)} own WADs, {len(missing)} placeholder textures, hlcsg exit {status}")
        manifest["maps"][name] = {**sources[name],
                                  "map_sha256": sha256(mapfile),
                                  "wads": [str(w.relative_to(root)) for w in wads],
                                  "placeholder_textures": missing,
                                  "oracle_hlcsg_exit": status}

    # Existing maps must come out byte-identical, or their oracle references would be stale.
    changed = [n for n, m in previous.items() if n in manifest["maps"] and m["map_sha256"] != manifest["maps"][n]["map_sha256"]]
    if changed:
        log(f"corpus maps changed content: {', '.join(changed)}; their references are stale (re-run ref for them)")
    write_json(root / "manifest.json", manifest)
    added = sorted(set(manifest["maps"]) - set(previous))
    log(f"corpus: {len(manifest['maps'])} maps in {maps_dir}" + (f"; new: {', '.join(added)}" if added else ""))
    return 1 if changed else 0
    return 0


# ---------------------------------------------------------------------------
# running the pipeline


def tool_args(tier: str, stage: str, threads: int) -> list[str]:
    return ["-threads", str(threads), *TIERS[tier][stage]]


def env_for(bindir: Path) -> dict:
    env = dict(os.environ)
    # Leaks are by design in these tools; heap errors and UB are what we want.
    env.setdefault("ASAN_OPTIONS", "detect_leaks=0:halt_on_error=0:abort_on_error=0")
    env.setdefault("UBSAN_OPTIONS", "print_stacktrace=1:halt_on_error=0")
    return env


def run_stage(bindir: Path, tool: str, args: list[str], name: str, cwd: Path, logdir: Path, stage: str) -> dict:
    start = time.perf_counter()
    try:
        proc = run([bindir / tool, *args, name], cwd=cwd, capture_output=True, env=env_for(bindir), timeout=STAGE_TIMEOUT)
        code, out, err = proc.returncode, proc.stdout, proc.stderr
    except subprocess.TimeoutExpired as e:
        code, out, err = "timeout", e.stdout or b"", e.stderr or b""
    seconds = time.perf_counter() - start
    logdir.mkdir(parents=True, exist_ok=True)
    (logdir / f"{stage}.stdout").write_bytes(out)
    (logdir / f"{stage}.stderr").write_bytes(err)
    return {"exit": code, "seconds": round(seconds, 3)}


@functools.lru_cache(maxsize=None)
def harness_digest() -> str:
    here = Path(__file__).resolve().parent
    return stagecache.digest([sha256(here / f) for f in ("parity.py", "bspdiff.py", "stagecache.py")])


@functools.lru_cache(maxsize=None)
def tool_identity(bindir: Path, tool: str) -> str:
    """The tool binary, plus every other file in its directory (hlbsp reads entities.void from there)."""
    others = {f.name: sha256(f) for f in sorted(bindir.iterdir()) if f.is_file() and f.name not in STAGE_TOOL.values()}
    return stagecache.digest({"tool": sha256(bindir / tool), "others": others})


@functools.lru_cache(maxsize=None)
def wads_digest() -> str:
    root = DATA / "corpus" / "wads"
    return stagecache.digest([(str(f.relative_to(root)), sha256(f)) for f in sorted(root.rglob("*")) if f.is_file()])


def run_key(bindir: Path, tool: str, args: list[str], name: str, inputs: dict) -> str:
    return stagecache.digest({
        "v": stagecache.VERSION, "tool": tool_identity(bindir, tool), "args": args, "map": name,
        "input": stagecache.manifest_digest(inputs),
        "wads": wads_digest() if tool == "hlcsg" else None, "WADROOT": os.environ.get("WADROOT")})


def open_cache(root: Path | None) -> "stagecache.StageCache | None":
    return stagecache.StageCache(root, normalize_log) if root else None


def record_run(cache, key: str, r: dict, work: Path, console: Path, stage: str, audit: bool) -> dict:
    """Store a fresh run under key; with audit, flag it if it differs from the entry already there."""
    outputs = cache.manifest(work, store=True)
    console_files = {n: cache.file_entry(console / n, store=True) for n in (f"{stage}.stdout", f"{stage}.stderr")}
    old = cache.get_run(key)
    if audit and old and (old["result"]["exit"] != r["exit"] or
                          stagecache.manifest_digest(old["outputs"]) != stagecache.manifest_digest(outputs)):
        r["audit"] = "differs from the cached run with the same key (nondeterminism, or an input the key misses)"
    if r["exit"] != "timeout":
        cache.put_run(key, {"result": {"exit": r["exit"], "seconds": r["seconds"]}, "outputs": outputs, "console": console_files})
    r["console"] = console_files
    return outputs


def cached_result(entry: dict) -> dict:
    return {**entry["result"], "cached": True, "console": entry["console"]}


def run_chain(bindir: Path, tier: str, name: str, root: Path, threads: int = 1, snapshots: bool = True,
              last_stage: str = "rad", cache=None, audit: bool = False) -> dict:
    """Run the stages up to last_stage in one directory; snapshot the directory after each into root/<stage>.

    With a cache, a stage whose key is known is not run; the directory is only rebuilt from the
    cache when a later stage has to run. Each result then carries "outputs", the manifest of the
    directory after the stage (its snapshot is rebuilt on demand, see ensure_snapshot)."""
    work = root / "work"
    if root.exists():
        shutil.rmtree(root)
    work.mkdir(parents=True)
    source = TIERS[tier].get("input")
    if source:  # a pre-baked input: the oracle's output of an earlier stage of another tier
        pre = DATA / "refs" / source[0] / name / source[1]
        if not pre.exists():  # the map stopped before that stage (an error map, a leak)
            shutil.rmtree(work)
            return {}
        for f in pre.iterdir():
            if f.is_file():
                shutil.copy2(f, work / f.name)
    else:
        shutil.copy2(DATA / "corpus" / "maps" / f"{name}.map", work / f"{name}.map")
    state = cache.manifest(work) if cache else None  # what the directory holds now
    on_disk = True
    results = {}
    for stage in tier_stages(tier):
        tool = STAGE_TOOL[stage]
        args = tool_args(tier, stage, threads)
        r = None
        locked = False
        if cache:
            key = run_key(bindir, tool, args, name, state)
            entry = None if audit else cache.run_or_wait(key)
            locked = not audit and entry is None
            if entry:
                r, state, on_disk = cached_result(entry), entry["outputs"], False
        if r is None:
            try:
                if not on_disk:
                    cache.materialize(state, work)
                    on_disk = True
                r = run_stage(bindir, tool, args, name, work, root / "console", stage)
                if snapshots:
                    snapshot(work, root / stage)
                if cache:
                    state = record_run(cache, key, r, work, root / "console", stage, audit)
            finally:
                if locked:
                    cache.unlock(key)
        if cache:
            r["outputs"] = state
        results[stage] = r
        if r["exit"] != 0 or stage == last_stage:
            break
    shutil.rmtree(work)
    return results


def run_isolated(bindir: Path, tier: str, name: str, ref_root: Path, root: Path, cache=None, audit: bool = False,
                 only: tuple | None = None) -> dict:
    """Run each stage after the first on the oracle's output of the previous stage (only: just these)."""
    results = {}
    stages = tier_stages(tier)
    for i, stage in enumerate(stages[1:], start=1):
        if stage not in ("bsp", "vis", "rad") or (only and stage not in only):
            continue  # isolated mode exercises the downstream tools only
        tool = STAGE_TOOL[stage]
        prev = ref_root / stages[i - 1]
        if not prev.exists() or not (ref_root / stage).exists():
            continue
        args = tool_args(tier, stage, 1)
        r = None
        locked = False
        if cache:
            key = run_key(bindir, tool, args, name, cache.manifest(prev))
            entry = None if audit else cache.run_or_wait(key)
            locked = not audit and entry is None
            if entry:
                r = cached_result(entry)
                r["outputs"] = entry["outputs"]
        if r is None:
            try:
                work = root / f"{stage}.work"
                if work.exists():
                    shutil.rmtree(work)
                shutil.copytree(prev, work)
                r = run_stage(bindir, tool, args, name, work, root / "console", stage)
                snapshot(work, root / stage)
                if cache:
                    r["outputs"] = record_run(cache, key, r, work, root / "console", stage, audit)
                shutil.rmtree(work)
            finally:
                if locked:
                    cache.unlock(key)
        results[stage] = r
    return results


# ---------------------------------------------------------------------------
# comparing


LOG_RULES = [
    # check --threads: the thread count shows up in the command line, the settings table and RunThreadsOn
    (re.compile(rb"^Running with \d+ threads", re.M), b"Running with <n> threads"),
    (re.compile(rb" -threads \d+ "), b" -threads <n> "),
    (re.compile(rb"^(threads +)\[ +\d+ \]", re.M), rb"\1[ <n> ]"),
    (re.compile(rb"\(\w{3} [ \d]\d \d{4}\)"), b"(<build date>)"),             # Banner: __DATE__
    (re.compile(rb"^(Command line: )\S*/(hl\w+)", re.M), rb"\1\2"),            # argv[0] path
    (re.compile(rb"\(\d+\.\d+ seconds\)"), b"(<t> seconds)"),                  # RunThreadsOn
    (re.compile(rb"\d+\.\d+ seconds elapsed( \[[\dhms ]+\])?"), b"<t> seconds elapsed"),
    (re.compile(rb"(\(\d+%: est\. time )[^)]*\)"), rb"\1<t>)"),         # -estimate progress (console only)
]


def normalize_log(data: bytes) -> bytes:
    for rx, sub in LOG_RULES:
        data = rx.sub(sub, data)
    return data


def compare_dirs(ref: Path, cand: Path, padding: str) -> list[str]:
    problems = []
    ref_files = {f.name for f in ref.iterdir()}
    cand_files = {f.name for f in cand.iterdir()}
    for f in sorted(ref_files - cand_files):
        problems.append(f"missing {f}")
    for f in sorted(cand_files - ref_files):
        problems.append(f"unexpected {f}")
    for f in sorted(ref_files & cand_files):
        a, b = ref / f, cand / f
        if f.endswith(".bsp"):
            same, report = bspdiff.compare(str(a), str(b), padding)
            if not same:
                problems.append(f"{f} differs:\n" + "\n".join(report))
        elif f.endswith((".log", ".err")):
            na, nb = normalize_log(a.read_bytes()), normalize_log(b.read_bytes())
            if na != nb:
                la, lb = na.split(b"\n"), nb.split(b"\n")
                i = next((i for i, (x, y) in enumerate(zip(la, lb)) if x != y), min(len(la), len(lb)))
                ra = la[i] if i < len(la) else b"<eof>"
                rb = lb[i] if i < len(lb) else b"<eof>"
                problems.append(f"{f} differs at line {i + 1}:\n    ref : {ra[:200]!r}\n    cand: {rb[:200]!r}")
        elif a.read_bytes() != b.read_bytes():
            problems.append(f"{f} differs ({a.stat().st_size} vs {b.stat().st_size} bytes)")
    return problems


def compare_stage(ref_root: Path, ref_res: dict, cand_root: Path, cand_res: dict, stage: str, padding: str) -> list[str]:
    problems = []
    re_, ce = ref_res.get(stage, {}).get("exit"), cand_res.get(stage, {}).get("exit")
    if re_ != ce:
        problems.append(f"exit code ref={re_} cand={ce}")
    if (ref_root / stage).exists() and (cand_root / stage).exists():
        problems += compare_dirs(ref_root / stage, cand_root / stage, padding)
    elif (ref_root / stage).exists() != (cand_root / stage).exists():
        problems.append("stage ran in only one of ref/cand")
    return problems


def ensure_snapshot(cache, root: Path, stage: str, result: dict | None) -> None:
    """Rebuild root/<stage> (and the stage's console output) from the cache if the run was reused."""
    if cache and result and result.get("outputs") is not None and not (root / stage).exists():
        cache.materialize(result["outputs"], root / stage)
    if cache and result and result.get("console"):
        (root / "console").mkdir(parents=True, exist_ok=True)
        for n, (_, blob) in result["console"].items():
            if not (root / "console" / n).exists():
                shutil.copyfile(cache.root / "blobs" / blob[:2] / blob, root / "console" / n)


def compare_stage_cached(cache, ref_root: Path, ref_res: dict, cand_root: Path, cand_res: dict, stage: str,
                         padding: str) -> list[str]:
    if cache is None:
        return compare_stage(ref_root, ref_res, cand_root, cand_res, stage, padding)
    cand = cand_res.get(stage)
    ref_dir = ref_root / stage
    key = stagecache.digest({
        "v": stagecache.VERSION, "harness": harness_digest(), "padding": padding, "stage": stage,
        "ref": stagecache.compare_digest(cache.manifest(ref_dir)) if ref_dir.exists() else None,
        "ref_exit": ref_res.get(stage, {}).get("exit"),
        "cand": stagecache.compare_digest(cand.get("outputs")) if cand else None,
        "cand_exit": cand.get("exit") if cand else None})
    hit = cache.get_compare(key)
    if hit is not None:
        return hit["problems"]
    ensure_snapshot(cache, cand_root, stage, cand)
    problems = compare_stage(ref_root, ref_res, cand_root, cand_res, stage, padding)
    cache.put_compare(key, {"problems": problems})
    return problems


# ---------------------------------------------------------------------------
# commands: ref, check


def _ref_job(bindir: Path, tier: str, name: str) -> tuple[str, dict]:
    root = DATA / "refs" / tier / name
    res = run_chain(bindir, tier, name, root)
    write_json(root / "result.json", res)
    return name, res


def cmd_ref(args) -> int:
    bindir = check_bin(Path(args.bin) if args.bin else DATA / "builds" / "oracle" / "bin")
    maps = select_maps(args.maps)
    write_json(DATA / "refs" / args.tier / "manifest.json",
               {"bin": str(bindir), "binaries": {t: sha256(bindir / t) for _, t in STAGES},
                "tier": args.tier, "args": {s: tool_args(args.tier, s, 1) for s in tier_stages(args.tier)},
                "recorded": datetime.datetime.now().isoformat(timespec="seconds")})
    with concurrent.futures.ProcessPoolExecutor(args.jobs) as pool:
        futures = [pool.submit(_ref_job, bindir, args.tier, n) for n in maps]
        for fut in concurrent.futures.as_completed(futures):
            name, res = fut.result()
            log(f"  {name:24} " + "  ".join(f"{s}={r['exit']}/{r['seconds']:.1f}s" for s, r in res.items()))
    return 0


DOWNSTREAM = ["hlbsp", "hlvis", "hlrad"]


def full_pass_record(tier: str) -> Path:
    return DATA / "builds" / f"last-full-pass-{tier}.json"


def downstream_guard(bindir: Path, tier: str, reference: str | None) -> tuple[bool, str]:
    """csg-only checks are sound only if hlbsp/hlvis/hlrad are byte-identical to binaries that passed
    a full-pipeline check of this tier. Returns (ok, explanation)."""
    if reference:
        path = DATA / "builds" / reference / "manifest.json"
        if not path.exists():
            return False, f"no build named {reference}"
        known = {"name": reference, "binaries": read_json(path)["binaries"]}
    else:
        path = full_pass_record(tier)
        if not path.exists():
            return False, f"no build has passed a full {tier} check yet"
        known = read_json(path)
    differing = [t for t in DOWNSTREAM if sha256(bindir / t) != known["binaries"][t]]
    if differing:
        return False, f"{', '.join(differing)} differ from {known['name']}"
    return True, f"hlbsp/hlvis/hlrad are identical to {known['name']}"


def _check_job(bindir: Path, tier: str, name: str, run_root: Path, mode: str, padding: str,
               threads: int = 1, last_stage: str = "rad", cache_root: Path | None = None, audit: bool = False,
               only: tuple | None = None) -> dict:
    """One map in one mode ("chain" or "isolated")."""
    cache = open_cache(cache_root)
    ref_root = DATA / "refs" / tier / name
    ref_res = read_json(ref_root / "result.json")
    out = {"map": name, "mode": mode, "stages": {}}
    root = run_root / mode / name
    if mode == "chain":
        stages = tier_stages(tier)
        run_stages = stages[: stages.index(last_stage) + 1] if last_stage in stages else stages
        res = run_chain(bindir, tier, name, root, threads=threads, last_stage=last_stage, cache=cache, audit=audit)
        compared = [s for s in run_stages if s in ref_res or s in res]
    else:
        res = run_isolated(bindir, tier, name, ref_root, root, cache=cache, audit=audit, only=only)
        compared = list(res)
    for stage in compared:
        problems = compare_stage_cached(cache, ref_root, ref_res, root, res, stage, padding)
        if res.get(stage, {}).get("audit"):
            problems = [f"cache audit: {res[stage]['audit']}", *problems]
        out["stages"][stage] = {"result": res.get(stage), "problems": problems}
    if any(s["problems"] for s in out["stages"].values()):
        for stage in res:  # keep everything for inspection, as an uncached run would have
            ensure_snapshot(cache, root, stage, res[stage])
    return out


def job_weight(tier: str, name: str, mode: str) -> float:
    """The oracle's time for the stages the job runs: longest jobs are started first."""
    try:
        ref = read_json(DATA / "refs" / tier / name / "result.json")
    except FileNotFoundError:
        return 0.0
    return sum(v.get("seconds", 0) for k, v in ref.items() if mode == "chain" or k in ("bsp", "vis", "rad"))


def for_each_tier(command):
    """Run command(args) once per tier of a tier group (e.g. csg-variants); worst exit status."""
    def run_all(args) -> int:
        status = 0
        for tier in expand_tiers(args.tier):
            if len(expand_tiers(args.tier)) > 1:
                log(f"== tier {tier}")
            status = max(status, command(argparse.Namespace(**{**vars(args), "tier": tier})))
        return status
    return run_all


def cmd_check(args) -> int:
    bindir = check_bin(Path(args.bin))
    maps = select_maps(args.maps)
    if not (DATA / "refs" / args.tier / "manifest.json").exists():
        sys.exit(f"no oracle reference for tier {args.tier}; run 'parity.py ref --tier {args.tier}'")
    last_stage = tier_stages(args.tier)[-1]
    if args.stages == "csg" and last_stage == "rad":
        ok, why = downstream_guard(bindir, args.tier, args.downstream_from)
        if ok:
            last_stage = "csg"
            log(f"csg only: {why}")
        else:
            log(f"running the full pipeline instead of csg only: {why}")
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    run_name = f"{stamp}-{args.label or bindir.parent.name}-{args.tier}"
    # Fully cached checks finish within a second, so back-to-back runs can share a stamp.
    for attempt in itertools.count():
        run_root = DATA / "runs" / (run_name if attempt == 0 else f"{run_name}-{attempt}")
        try:
            run_root.mkdir(parents=True)
            break
        except FileExistsError:
            pass

    # The cache relies on single-threaded determinism; multi-threaded runs are what checks it.
    cache_root = CACHE_ROOT if args.threads == 1 else None
    if args.threads != 1:
        log("cache not used: --threads > 1")
    modes = [m for m in ("chain", "isolated") if args.mode in (m, "both")]
    if last_stage == "csg" or TIERS[args.tier].get("input"):
        modes = [m for m in modes if m == "chain"]  # isolated runs only the downstream tools; one stage: chain
    only = None
    if args.stages == "rad":  # hlrad alone, on the oracle's VIS output of this tier (pre-baked input)
        modes, only = ["isolated"], ("rad",)
    jobs = sorted(((n, m) for n in maps for m in modes), key=lambda j: -job_weight(args.tier, *j))
    by_map = {n: {"map": n, "chain": {}, "isolated": {}} for n in maps}
    pending = {n: len(modes) for n in maps}
    with concurrent.futures.ProcessPoolExecutor(args.jobs) as pool:
        futures = [pool.submit(_check_job, bindir, args.tier, n, run_root, m, args.padding, args.threads, last_stage,
                               cache_root, args.no_cache, only) for n, m in jobs]
        for fut in concurrent.futures.as_completed(futures):
            r = fut.result()
            by_map[r["map"]][r["mode"]] = r["stages"]
            pending[r["map"]] -= 1
            if pending[r["map"]] == 0:
                cells = []
                for mode in ("chain", "isolated"):
                    for stage, s in by_map[r["map"]][mode].items():
                        mark = "*" if (s["result"] or {}).get("cached") else ""
                        cells.append(f"{mode[0]}:{stage}={'ok' if not s['problems'] else 'DIFF'}{mark}")
                log(f"  {r['map']:24} {' '.join(cells)}")

    results = [by_map[n] for n in sorted(maps)]
    lines = []
    failures = 0
    for r in results:
        for mode in ("chain", "isolated"):
            for stage, s in r[mode].items():
                if s["problems"]:
                    failures += 1
                    lines.append(f"[{r['map']}] {mode} {stage}:")
                    lines += [f"  {p}" for p in s["problems"]]
    scope = "csg only" if last_stage == "csg" else "all stages"
    runs = [s["result"] for r in results for mode in ("chain", "isolated") for s in r[mode].values() if s["result"]]
    reused = sum(1 for x in runs if x.get("cached"))
    cache_note = "cache off" if cache_root is None else ("cache audit" if args.no_cache else f"{reused}/{len(runs)} tool runs from cache")
    summary = (f"parity {'PASS' if not failures else 'FAIL'}: {len(results)} maps, tier {args.tier}, mode {args.mode}, "
               f"{scope}, threads {args.threads}, padding {args.padding}, {failures} differing stage(s), {cache_note}")
    report = "\n".join([summary, *lines]) + "\n"
    (run_root / "report.txt").write_text(report)
    write_json(run_root / "report.json", {"bin": str(bindir), "summary": summary, "results": results})
    log("")
    log(report if failures else summary)
    log(f"report: {run_root / 'report.txt'}")
    full_corpus = set(CORE_MAPS) & set(corpus_maps(archived=False)) <= set(maps)
    if not failures and last_stage == "rad" and tier_stages(args.tier)[-1] == "rad" and args.threads == 1 and args.mode in ("chain", "both") and full_corpus:
        # These downstream binaries are now known to match the oracle on this tier.
        write_json(full_pass_record(args.tier), {
            "name": bindir.parent.name, "bin": str(bindir), "run": str(run_root),
            "binaries": {t: sha256(bindir / t) for _, t in STAGES},
            "recorded": datetime.datetime.now().isoformat(timespec="seconds")})
        log(f"recorded {bindir.parent.name} as the last full {args.tier} pass")
    if not args.keep and not failures:
        for mode in ("chain", "isolated"):
            shutil.rmtree(run_root / mode, ignore_errors=True)
    return 1 if failures else 0


def cmd_cache(args) -> int:
    cache = open_cache(CACHE_ROOT)
    if args.clear:
        cache.clear()
        log(f"cleared {CACHE_ROOT}")
        return 0
    for kind, (n, size) in cache.stats().items():
        log(f"  {kind:9} {n:7} files {size / 1e6:9.1f} MB")
    return 0


# ---------------------------------------------------------------------------
# commands: bench, sanitize


def cmd_bench(args) -> int:
    bindir = check_bin(Path(args.bin))
    maps = select_maps(args.maps)
    label = args.label or bindir.parent.name
    out = {"bin": str(bindir), "tier": args.tier, "threads": args.threads, "repeat": args.repeat, "maps": {}}
    with tempfile.TemporaryDirectory(dir=DATA) as tmp:
        for name in maps:
            samples: dict[str, list[float]] = {s: [] for s in tier_stages(args.tier)}
            for _ in range(args.repeat):
                res = run_chain(bindir, args.tier, name, Path(tmp) / name, threads=args.threads, snapshots=False)
                for s, r in res.items():
                    samples[s].append(r["seconds"])
            med = {s: statistics.median(v) for s, v in samples.items() if v}
            out["maps"][name] = {"median_seconds": med, "samples": samples}
            log(f"  {name:24} " + "  ".join(f"{s}={t:.2f}s" for s, t in med.items()) + f"  total={sum(med.values()):.2f}s")
    path = DATA / "bench" / f"{label}-{args.tier}-t{args.threads}.json"
    write_json(path, out)
    log(f"saved {path}")

    if args.baseline:
        base = read_json(Path(args.baseline))
        log(f"\nvs {args.baseline} (ratio < 1.0 = faster):")
        for name, m in out["maps"].items():
            if name not in base["maps"]:
                continue
            b = base["maps"][name]["median_seconds"]
            ratios = "  ".join(f"{s}={t / b[s]:.3f}" for s, t in m["median_seconds"].items() if b.get(s))
            log(f"  {name:24} {ratios}")
    return 0


SAN_RE = re.compile(rb"(runtime error: .*|ERROR: AddressSanitizer: [\w-]+.*)")
SAN_LOC_RE = re.compile(rb"^(/\S+:\d+:\d+): runtime error: (.*)$", re.M)
HEX_RE = re.compile(r"0x[0-9a-f]+")
ASAN_FRAME_RE = re.compile(rb"#\d+ 0x[0-9a-f]+ in (\S+) (/\S+:\d+)")


LEAK_SUMMARY_RE = re.compile(rb"SUMMARY: AddressSanitizer: (\d+) byte\(s\) leaked")


def summarize_sanitizer(stderr: bytes) -> list[str]:
    found = []
    for m in SAN_LOC_RE.finditer(stderr):
        loc = m.group(1).decode(errors="replace").split("/src/", 1)[-1]
        what = HEX_RE.sub("0x?", m.group(2).decode(errors="replace"))  # heap addresses differ per run
        found.append(f"UBSan {loc}: {what}")
    for block in stderr.split(b"=================================================================")[1:]:
        head = re.search(rb"ERROR: AddressSanitizer: ([\w-]+)", block)
        frame = next((f for f in ASAN_FRAME_RE.finditer(block) if b"/src/" in f.group(2)), None)
        if head:
            where = frame.group(2).decode().split("/src/", 1)[-1] + f" ({frame.group(1).decode()})" if frame else "?"
            found.append(f"ASan {head.group(1).decode()} at {where}")
    return found


def cmd_sanitize(args) -> int:
    if args.bin:
        bindir = check_bin(Path(args.bin))
    else:
        build = argparse.Namespace(name="sanitize", tree=True, ref=None, cxx=args.cxx, cxx_flags=None, sanitize=True, force=True, build_date=None)
        cmd_build(build)
        bindir = DATA / "builds" / "sanitize" / "bin"
    maps = select_maps(args.maps)
    if args.leaks:
        # before the pool starts: workers take their environment from this process
        os.environ["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=0:abort_on_error=0"
    findings: dict[str, set[str]] = {}
    leaks: dict[str, dict[str, int]] = {}  # tool -> map -> bytes
    tiers = expand_tiers(args.tier)
    with tempfile.TemporaryDirectory(dir=DATA) as tmp:
        with concurrent.futures.ProcessPoolExecutor(args.jobs) as pool:
            # a tier group runs each of its tiers; maps are then reported as tier/map
            futs = {pool.submit(run_chain, bindir, t, n, Path(tmp) / t / n, 1, False): (t, n) for t in tiers for n in maps}
            for fut in concurrent.futures.as_completed(futs):
                tier, map_name = futs[fut]
                name = map_name if len(tiers) == 1 else f"{tier}/{map_name}"
                fut.result()
                for stage in tier_stages(tier):
                    tool = STAGE_TOOL[stage]
                    err = Path(tmp) / tier / map_name / "console" / f"{stage}.stderr"
                    if err.exists():
                        data = err.read_bytes()
                        for f in summarize_sanitizer(data):
                            findings.setdefault(f"{tool}: {f}", set()).add(name)
                        if args.leaks:
                            m = LEAK_SUMMARY_RE.search(data)
                            leaks.setdefault(tool, {})[name] = int(m.group(1)) if m else 0
                log(f"  {name}")
    lines = [f"{k}  [{len(v)} maps: {', '.join(sorted(v)[:4])}{', ...' if len(v) > 4 else ''}]" for k, v in sorted(findings.items())]
    report = f"sanitizer findings: {len(findings)} unique\n" + "\n".join(lines) + "\n"
    if args.leaks:
        # Measured, not compared with the baseline: leak reports are not findings.
        report += "\nleaks (LeakSanitizer, bytes still unreachable at exit):\n"
        for tool in sorted(leaks):
            leaking = {n: b for n, b in leaks[tool].items() if b}
            worst = ", ".join(f"{n} {b}" for n, b in sorted(leaking.items(), key=lambda x: -x[1])[:3])
            report += f"  {tool}: {sum(leaking.values())} bytes, {len(leaking)} of {len(leaks[tool])} maps" + (f" ({worst}, ...)" if worst else "") + "\n"
    path = DATA / "sanitize" / f"{datetime.datetime.now():%Y%m%d-%H%M%S}-{args.tier}.txt"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(report)
    log(report)
    log(f"saved {path}")
    if args.baseline:
        # Compare on file and message only: edits elsewhere in a file shift line/column numbers.
        def key(finding: str) -> str:
            return re.sub(r":\d+:\d+:", ":", finding.split("  [")[0])

        base = {key(l) for l in Path(args.baseline).read_text().splitlines()[1:] if l}
        new = sorted(f for f in findings if key(f) not in base)
        log(f"{len(new)} finding(s) not in baseline" + (":\n  " + "\n  ".join(new) if new else ""))
        return 1 if new else 0
    return 0


# ---------------------------------------------------------------------------
# command: probe (old-vs-new differential of common/ BSP I/O, and corruption smoke test)

PROBE_SRC = REPO / "tests" / "probe" / "bsp_probe.cpp"
PROBE_MODES = ["roundtrip", "embedded", "sizes", "textures"]


def build_probe(src_root: Path, out: Path, cxx: str, flags: list[str]) -> Path:
    """Compile bsp_probe against src_root/src/common (the tool configuration of hlrad)."""
    out.mkdir(parents=True, exist_ok=True)
    exe = out / "bsp_probe"
    sources = sorted((src_root / "src" / "common").glob("*.cpp"))
    cmd = [cxx, "-std=c++20", "-O2", *PARITY_CXX_FLAGS.split(), *flags, "-w", "-DHLRAD",
           f"-I{src_root / 'src'}", str(PROBE_SRC), *map(str, sources), "-o", str(exe)]
    proc = run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        sys.exit(f"probe build failed ({src_root}):\n{proc.stderr[-4000:]}")
    return exe


def probe_inputs() -> list[Path]:
    bsp_dir = Path(os.environ.get("VHLT_TEST_BSP_DIR", GAME / "cstrike" / "maps"))
    files = sorted(bsp_dir.glob("*.bsp"))
    files += sorted((DATA / "refs" / "quick").glob("*/rad/*.bsp"))
    return [f for f in files if f.read_bytes()[:4] == (30).to_bytes(4, "little")]


def _probe_case(old: Path, new: Path, mode: str, bsp: Path, tmp: Path) -> list[str]:
    problems = []
    outs = {}
    for label, exe in (("old", old), ("new", new)):
        out = tmp / f"{bsp.stem}.{mode}.{label}.bsp"
        args = [exe, mode, bsp] + ([out] if mode in ("roundtrip", "embedded") else [])
        proc = run(args, capture_output=True)
        outs[label] = (proc.returncode, proc.stdout, out)
    (oc, oout, obsp), (nc, nout, nbsp) = outs["old"], outs["new"]
    if oc != nc:
        problems.append(f"exit old={oc} new={nc}")
    if oout != nout:
        a, b = oout.split(b"\n"), nout.split(b"\n")
        i = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))
        problems.append(f"stdout differs at line {i + 1}: old={a[i][:160] if i < len(a) else b'<eof>'!r} "
                        f"new={b[i][:160] if i < len(b) else b'<eof>'!r}")
    if obsp.exists() or nbsp.exists():
        if not (obsp.exists() and nbsp.exists()):
            problems.append("output written by only one build")
        else:
            same, report = bspdiff.compare(str(obsp), str(nbsp), "zero-or-equal")
            if not same:
                problems.append("output .bsp differs:\n" + "\n".join(report))
            _, zreport = bspdiff.compare(str(nbsp), str(nbsp), "zero")
            if zreport:
                problems.append("new build wrote non-zero padding")
    for f in (obsp, nbsp):
        f.unlink(missing_ok=True)
    return problems


def mutations(data: bytes, rng) -> list[tuple[str, bytes]]:
    """Deterministic corruptions of a BSP image, aimed at the loader's checks."""
    import struct
    size = len(data)
    interesting = [0, 1, 3, -1, -4, size, size - 1, size + 1, 0x7FFFFFFF, -0x80000000, 0x40000000, 124, 125]
    out = []
    for _ in range(4):
        lump = rng.randrange(15)
        field = rng.randrange(2)
        value = rng.choice(interesting + [rng.randrange(-size, 2 * size)])
        b = bytearray(data)
        struct.pack_into("<i", b, 4 + 8 * lump + 4 * field, value)
        out.append((f"lump{lump}.{'ofs' if field == 0 else 'len'}={value}", bytes(b)))
    cut = rng.randrange(0, min(size, 200))
    out.append((f"truncated to {cut}", data[:cut]))
    # texture lump internals: nummiptex and one dataofs entry
    tofs, tlen = struct.unpack_from("<ii", data, 4 + 8 * 2)
    if tlen >= 8:
        b = bytearray(data)
        struct.pack_into("<i", b, tofs, rng.choice([-1, 0x7FFFFFFF, tlen, 1 << 20]))
        out.append(("nummiptex corrupted", bytes(b)))
        b = bytearray(data)
        struct.pack_into("<i", b, tofs + 4, rng.choice([-2, tlen, tlen - 1, 0x7FFFFFF0, 4]))
        out.append(("dataofs[0] corrupted", bytes(b)))
    # texinfo miptex values that index outside the texture table
    iofs, ilen = struct.unpack_from("<ii", data, 4 + 8 * 6)
    if ilen >= 40:
        b = bytearray(data)
        struct.pack_into("<i", b, iofs + 32, rng.choice([-2, -1000, 1 << 24, 0x7FFFFFFF]))
        out.append(("texinfo[0].miptex corrupted", bytes(b)))
    return out


def cmd_probe(args) -> int:
    import random
    root = DATA / "builds" / "probe"
    old = build_probe(DATA / "builds" / "oracle" / "src", root / "oracle", "g++", [])
    new = build_probe(REPO, root / "tree", "g++", [])
    inputs = probe_inputs()
    log(f"differential: {len(inputs)} BSPs x {len(PROBE_MODES)} modes, oracle common/ vs working tree")

    failures = []
    with tempfile.TemporaryDirectory(dir=DATA) as tmp:
        tmp = Path(tmp)
        with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            futs = {pool.submit(_probe_case, old, new, m, f, tmp): (f, m) for f in inputs for m in PROBE_MODES}
            for fut in concurrent.futures.as_completed(futs):
                f, m = futs[fut]
                for p in fut.result():
                    failures.append(f"[{f.name}] {m}: {p}")
        log(f"  {'PASS' if not failures else 'FAIL'}: {len(failures)} difference(s)")

        if args.mutations:
            if args.sanitize:
                san = build_probe(REPO, root / "sanitize", "clang++",
                                  ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-g"])
            exe = san if args.sanitize else new
            env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:exitcode=99", UBSAN_OPTIONS="halt_on_error=1:exitcode=98")
            rng = random.Random(args.seed)
            small = sorted((p for p in inputs if p.stat().st_size < 8 << 20), key=lambda p: p.stat().st_size)[:12]
            cases = [(p, desc, data) for p in small for _ in range(args.mutations // max(1, len(small)) // 8 + 1)
                     for desc, data in mutations(p.read_bytes(), rng)]
            cases = cases[: args.mutations]
            crashes, accepted, rejected = [], 0, 0
            for i, (p, desc, data) in enumerate(cases):
                f = tmp / f"mut{i}.bsp"
                f.write_bytes(data)
                for mode in ("load", "sizes", "textures"):
                    proc = run([exe, mode, f], capture_output=True, env=env, timeout=120)
                    if proc.returncode not in (0, 1):
                        tail = (proc.stderr or proc.stdout)[-600:].decode(errors="replace")
                        crashes.append(f"[{p.name}: {desc}] {mode}: exit {proc.returncode}\n{tail}")
                    elif mode == "load":
                        accepted += proc.returncode == 0
                        rejected += proc.returncode == 1
                f.unlink()
            log(f"mutations ({'ASan+UBSan' if args.sanitize else 'plain'} build, seed {args.seed}): {len(cases)} files, "
                f"{accepted} accepted, {rejected} rejected, {len(crashes)} crash(es)")
            failures += crashes

    if failures:
        log("\n".join(failures[:40]))
    return 1 if failures else 0


# ---------------------------------------------------------------------------
# command: regress (candidate-only scenarios where the oracle's behaviour is undefined)


def _hlcsg_bsp(bindir: Path, name: str, args: list[str], work: Path) -> tuple[int, bytes, bytes]:
    work.mkdir(parents=True)
    shutil.copy2(DATA / "corpus" / "maps" / f"{name}.map", work / f"{name}.map")
    proc = run([bindir / "hlcsg", "-threads", "1", *args, name], cwd=work, capture_output=True, env=env_for(bindir))
    bsp = work / f"{name}.bsp"
    return proc.returncode, bsp.read_bytes() if bsp.exists() else b"", proc.stdout + proc.stderr


def regress_texdata_growth(bindir: Path, tmp: Path) -> list[str]:
    """info_compile_parameters raises texdata above the buffer allocated for -texdata 16: hlcsg used to
    write past the 16 KB buffer (heap overflow). The output must equal a run with the default buffer."""
    small = _hlcsg_bsp(bindir, "fm_compileparams", ["-texdata", "16", "-nowadtextures"], tmp / "small")
    plain = _hlcsg_bsp(bindir, "fm_compileparams", ["-nowadtextures"], tmp / "plain")
    problems = []
    if small[0] != 0:
        problems.append(f"exit {small[0]} with -texdata 16")
    if b"ERROR: AddressSanitizer" in small[2] or b"runtime error:" in small[2]:
        problems.append("sanitizer error with -texdata 16")
    if small[1] != plain[1]:
        problems.append(".bsp differs from the run with the default texture buffer")
    return problems


def _leak_pipeline(bindir: Path, mapfile: Path, work: Path) -> tuple[int, bytes]:
    """hlcsg + hlbsp on mapfile in a fresh work; the hlbsp exit code and output."""
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    shutil.copy2(mapfile, work / mapfile.name)
    name = mapfile.stem
    run([bindir / "hlcsg", "-threads", "1", name], cwd=work, capture_output=True, env=env_for(bindir), check=True)
    proc = run([bindir / "hlbsp", "-threads", "1", name], cwd=work, capture_output=True, env=env_for(bindir))
    return proc.returncode, proc.stdout + proc.stderr


def regress_leak_all_hulls(bindir: Path, tmp: Path) -> list[str]:
    """A map that leaks in every hull: after the first leak the oracle wrote each later hull's trail to
    the point/line files it had already closed, so it crashes in some runs and not others. The
    candidate must exit 0 every time and write what the oracle writes in a run that survives."""
    sys.path.insert(0, str(FEATURE_MAP_DIR.parent))
    import make_feature_maps
    tmp.mkdir(parents=True)
    mapfile = tmp / "leak_all_hulls.map"
    make_feature_maps.write_map(mapfile, make_feature_maps.leak_all_hulls())
    canon = read_json(DATA / "corpus" / "manifest.json")["canonical_wads"]
    text = mapfile.read_text(encoding="latin-1")
    mapfile.write_text(set_wad_key(text, [str(DATA / "corpus" / w) for w in canon]), encoding="latin-1")

    problems = []
    oracle = DATA / "builds" / "oracle" / "bin"
    survived = None
    for attempt in range(40):
        code, _ = _leak_pipeline(oracle, mapfile, tmp / "oracle")
        if code == 0:
            survived = tmp / "oracle"
            break
    for i in range(3):
        work = tmp / f"cand{i}"
        code, out = _leak_pipeline(bindir, mapfile, work)
        if code != 0:
            problems.append(f"run {i}: hlbsp exit {code}")
        if b"ERROR: AddressSanitizer" in out or b"runtime error:" in out:
            problems.append(f"run {i}: sanitizer error")
        if b"LEAK in hull 1" not in out:
            problems.append(f"run {i}: no leak reported in hull 1 (the scenario no longer leaks there)")
        if survived is None:
            problems.append("the oracle crashed in all 40 runs: nothing to compare with")
        else:
            problems += [f"run {i}: {p}" for p in compare_dirs(survived, work, "zero")]
    return problems


def regress_hlbsp_threads(bindir: Path, tmp: Path) -> list[str]:
    """hlbsp parsed -threads into a local variable and dropped it (B4): the settings table showed the
    machine's thread count whatever was asked for. It must show the requested count now."""
    tmp.mkdir(parents=True)
    (tmp / "tiny.map").write_text(CLI_MAP)
    run([DATA / "builds" / "oracle" / "bin" / "hlcsg", "-threads", "1", "tiny"], cwd=tmp, capture_output=True, check=True)
    proc = run([bindir / "hlbsp", "-threads", "3", "tiny"], cwd=tmp, capture_output=True)
    if proc.returncode != 0:
        return [f"hlbsp exit {proc.returncode}"]
    if not re.search(rb"^threads +\[ +3 \]", proc.stdout, re.M):
        return ["settings table does not show -threads 3"]
    return []


def regress_hlvis_fastthreads(bindir: Path, tmp: Path) -> list[str]:
    """-fastthreads (V3, opt-in): with one thread it must give the default output; with several it
    must run (its output may differ from run to run, so it is not compared)."""
    oracle = DATA / "builds" / "oracle" / "bin"
    problems = []
    outputs = {}
    for label, args in (("default", []), ("fast1", ["-fastthreads", "-threads", "1"]), ("fast8", ["-fastthreads", "-threads", "8"])):
        work = tmp / label
        work.mkdir(parents=True)
        shutil.copy2(DATA / "corpus" / "maps" / "cs_assault.map", work / "cs_assault.map")
        for tool in ("hlcsg", "hlbsp"):
            run([oracle / tool, "-threads", "1", "cs_assault"], cwd=work, capture_output=True, check=True)
        proc = run([bindir / "hlvis", *(args or ["-threads", "1"]), "cs_assault"], cwd=work, capture_output=True)
        if proc.returncode != 0:
            problems.append(f"{label}: hlvis exit {proc.returncode}")
        outputs[label] = (work / "cs_assault.bsp").read_bytes(), proc.stdout
    if outputs["fast1"][0] != outputs["default"][0]:
        problems.append("-fastthreads -threads 1 differs from the default output")
    if b"fast threads        [      on ]" not in outputs["fast8"][1]:
        problems.append("settings table does not show fast threads")
    return problems


def regress_lightdata_growth(bindir: Path, tmp: Path) -> list[str]:
    """fm_rad_compileparams' info_compile_parameters raises lightdata above the buffer allocated for
    -lightdata 1: hlrad used to write the lightmaps past the 1 KB buffer (heap overflow). The .bsp must
    equal the oracle's default-buffer run (refs/full)."""
    name = "fm_rad_compileparams"
    tmp.mkdir(parents=True)
    for f in (DATA / "refs" / "full" / name / "vis").iterdir():
        if f.is_file():
            shutil.copy2(f, tmp / f.name)
    proc = run([bindir / "hlrad", "-threads", "1", "-lightdata", "1", name], cwd=tmp, capture_output=True,
               env=env_for(bindir))
    out = proc.stdout + proc.stderr
    problems = []
    if proc.returncode != 0:
        problems.append(f"exit {proc.returncode} with -lightdata 1")
    if b"ERROR: AddressSanitizer" in out or b"runtime error:" in out:
        problems.append("sanitizer error with -lightdata 1")
    expected = (DATA / "refs" / "full" / name / "rad" / f"{name}.bsp").read_bytes()
    bsp = tmp / f"{name}.bsp"
    if stagecache.bsp_canonical(bsp.read_bytes()) != stagecache.bsp_canonical(expected):
        problems.append(".bsp differs from the oracle's run with the default lighting buffer")
    return problems


def regress_waddir(bindir: Path, tmp: Path) -> list[str]:
    """Without the .wa_, hlrad finds the textures through -waddir (R4: the path was dir\\name on every
    system, so off Windows no wad was ever found and the run stopped). With the corpus wad
    directories, fm_rad_texlights must light exactly as the oracle's run with the .wa_ (refs/full)."""
    name = "fm_rad_texlights"
    tmp.mkdir(parents=True)
    for f in (DATA / "refs" / "full" / name / "vis").iterdir():
        if f.is_file() and f.suffix != ".wa_":
            shutil.copy2(f, tmp / f.name)
    dirs = [a for d in ("zhlt", "valve", "cstrike") for a in ("-waddir", str(DATA / "corpus" / "wads" / d))]
    proc = run([bindir / "hlrad", "-threads", "1", *dirs, name], cwd=tmp, capture_output=True, env=env_for(bindir))
    out = proc.stdout + proc.stderr
    problems = []
    if proc.returncode != 0:
        problems.append(f"exit {proc.returncode}")
    if f"Using Wadfile: {DATA / 'corpus' / 'wads' / 'valve' / 'halflife.wad'}".encode() not in out:
        problems.append("halflife.wad not opened from its -waddir")
    if b"texture is not found" in out:
        problems.append("textures not found")
    expected = (DATA / "refs" / "full" / name / "rad" / f"{name}.bsp").read_bytes()
    bsp = tmp / f"{name}.bsp"
    if not bsp.exists() or stagecache.bsp_canonical(bsp.read_bytes()) != stagecache.bsp_canonical(expected):
        problems.append(".bsp differs from the oracle's run with the .wa_")
    return problems


def regress_texdata_zero(bindir: Path, tmp: Path) -> list[str]:
    """-texdata with no usable number gives a 0-byte texture buffer: the oracle wrote the first texture
    into it (heap overflow). The candidate must stop with "Exceeded MAX_MAP_MIPTEX" and write no .bsp."""
    problems = []
    for value in ("x", "0"):
        code, bsp, out = _hlcsg_bsp(bindir, "cs_assault", ["-texdata", value], tmp / value)
        if code == 0:
            problems.append(f"-texdata {value}: exit 0")
        if b"Error: Exceeded MAX_MAP_MIPTEX" not in out:
            problems.append(f"-texdata {value}: no 'Exceeded MAX_MAP_MIPTEX' error")
        if b"ERROR: AddressSanitizer" in out or b"runtime error:" in out:
            problems.append(f"-texdata {value}: sanitizer error")
        if bsp:
            problems.append(f"-texdata {value}: a .bsp was written")
    return problems


def regress_rad_bad_texture(bindir: Path, tmp: Path) -> list[str]:
    """Every texinfo of fm_rad_texlights is pointed at texture 9999 (the map has 9): the oracle indexed
    its per-texture tables with it (read outside them). hlrad must stop with an error naming the face."""
    name = "fm_rad_texlights"
    tmp.mkdir(parents=True)
    for f in (DATA / "refs" / "full" / name / "vis").iterdir():
        if f.is_file():
            shutil.copy2(f, tmp / f.name)
    bsp = tmp / f"{name}.bsp"
    data = bytearray(bsp.read_bytes())
    LUMP_TEXINFO, TEXINFO_SIZE, MIPTEX_OFFSET = 6, 40, 32
    ofs, length = struct.unpack_from("<ii", data, 4 + LUMP_TEXINFO * 8)
    for i in range(length // TEXINFO_SIZE):
        struct.pack_into("<i", data, ofs + i * TEXINFO_SIZE + MIPTEX_OFFSET, 9999)
    bsp.write_bytes(data)
    proc = run([bindir / "hlrad", "-threads", "1", name], cwd=tmp, capture_output=True, env=env_for(bindir))
    log_text = (tmp / f"{name}.log").read_bytes()
    out = proc.stdout + proc.stderr + log_text
    problems = []
    if proc.returncode == 0:
        problems.append("exit 0")
    if b"Error: Face 0 refers to texture 9999, but there are only 9." not in log_text:
        problems.append("no 'Face 0 refers to texture 9999' error in the .log")
    if b"ERROR: AddressSanitizer" in out or b"runtime error:" in out:
        problems.append("sanitizer error")
    return problems


REGRESSIONS = {"texdata-growth": regress_texdata_growth, "leak-all-hulls": regress_leak_all_hulls,
               "hlbsp-threads": regress_hlbsp_threads, "hlvis-fastthreads": regress_hlvis_fastthreads,
               "lightdata-growth": regress_lightdata_growth, "waddir": regress_waddir,
               "texdata-zero": regress_texdata_zero, "rad-bad-texture": regress_rad_bad_texture}


def cmd_regress(args) -> int:
    bindir = check_bin(Path(args.bin))
    failures = 0
    with tempfile.TemporaryDirectory(dir=DATA) as tmp:
        for name, test in REGRESSIONS.items():
            problems = test(bindir, Path(tmp) / name)
            failures += bool(problems)
            log(f"  {name:20} {'ok' if not problems else 'FAIL: ' + '; '.join(problems)}")
    return 1 if failures else 0


# ---------------------------------------------------------------------------
# command: cli (oracle vs candidate on command lines the corpus tiers never use)

# "tiny" is a worldspawn-only map: hlcsg gets through its settings table (which shows what the
# command line set) and writes every output file in well under a second.
CLI_MAP = '{\n"classname" "worldspawn"\n}\n'

CLI_CASES = {
    "hlcsg": [
        [], ["tiny"], ["-threads"], ["-threads", "0", "tiny"], ["-threads", "abc", "tiny"],
        ["-threads", "3", "tiny"], ["tiny", "-threads"], ["-estimate", "-chart", "tiny"],
        ["-ESTIMATE", "tiny"], ["-noskyclip", "-onlyents", "-clipeconomy", "tiny"],
        ["-cliptype"], ["-cliptype", "bogus", "tiny"], ["-CLIPTYPE", "PRECISE", "tiny"],
        *[["-cliptype", t, "tiny"] for t in ("smallest", "normalized", "simple", "precise", "legacy")],
        ["-cliptype", "simple", "-cliptype", "bogus", "tiny"],
        ["-wadautodetect", "-nowadtextures", "-nolightopt", "-notextconvert", "tiny"],
        ["-wadinclude"], ["-wadinclude", "a.wad", "-wadinclude", "b", "tiny"],
        ["-texdata"], ["-texdata", "8192", "tiny"],
        # not "-texdata x": a 0-byte texture buffer, which the oracle overflowed (fixed in H2: an error)
        ["-lightdata"], ["-lightdata", "1024", "tiny"],
        ["-unknown", "tiny"], ["tiny", "extra"], ["-low", "tiny"], ["-", "tiny"],
        # S6a: how atoi reads option values (blanks, '+', a value past INT_MAX that glibc's atoi wraps)
        ["-texdata", "+8192", "tiny"], ["-texdata", " 8192", "tiny"], ["-threads", "4294967297", "tiny"],
    ],
    "hlbsp": [
        [], ["tiny"], ["-threads"], ["-threads", "0", "tiny"], ["-threads", "abc", "tiny"], ["-threads", "3", "tiny"],
        ["tiny", "-threads"], ["-estimate", "-chart", "tiny"], ["-CHART", "tiny"], ["-nohull2", "-viewportal", "tiny"],
        ["-subdivide"], ["-subdivide", "16", "tiny"], ["-subdivide", "100", "tiny"], ["-subdivide", "1000", "tiny"],
        ["-subdivide", "x", "tiny"], ["-subdivide", "-chart", "tiny"],
        ["-maxnodesize"], ["-maxnodesize", "32", "tiny"], ["-maxnodesize", "4096", "tiny"], ["-maxnodesize", "100000", "tiny"],
        ["-texdata"], ["-texdata", "65536", "tiny"], ["-lightdata"], ["-lightdata", "65536", "tiny"],
        ["-unknown", "tiny"], ["tiny", "extra"], ["-low", "tiny"], ["-high", "tiny"], ["-", "tiny"],
        # S6a
        ["-subdivide", "+64", "tiny"], ["-subdivide", "0x40", "tiny"], ["-maxnodesize", "4294968320", "tiny"],
    ],
    "hlvis": [
        [], ["tiny"], ["-threads"], ["-threads", "0", "tiny"], ["-threads", "abc", "tiny"], ["-threads", "3", "tiny"],
        ["tiny", "-threads"], ["-estimate", "-chart", "tiny"], ["-FAST", "tiny"], ["-full", "tiny"], ["-fast", "-full", "tiny"],
        ["-maxdistance"], ["-maxdistance", "512", "tiny"], ["-maxdistance", "-512", "tiny"], ["-maxdistance", "x", "tiny"],
        ["-texdata"], ["-texdata", "65536", "tiny"], ["-lightdata"], ["-lightdata", "65536", "tiny"],
        ["-unknown", "tiny"], ["tiny", "extra"], ["-low", "tiny"], ["-", "tiny"],
        # S6a: 4294967808 wraps to 512 in glibc's atoi
        ["-maxdistance", "4294967808", "tiny"], ["-maxdistance", " +512", "tiny"],
    ],
    "hlvis-empty": [["tiny"], ["-full", "tiny"]],
    "hlrad": [
        [], ["tiny"], ["-threads"], ["-threads", "0", "tiny"], ["-threads", "3", "tiny"], ["tiny", "-threads"],
        ["-bounce"], ["-bounce", "abc", "tiny"], ["-bounce", "2000", "tiny"], ["-bounce", "2", "tiny"],
        ["-chop", "0", "tiny"], ["-chop", "16", "tiny"], ["-texchop", "0", "tiny"], ["-texchop", "20", "tiny"],
        ["-fade", "-1", "tiny"], ["-vismatrix", "bogus", "tiny"], ["-VISMATRIX", "off", "tiny"],
        ["-compress", "5", "tiny"], ["-rgbcompress", "9", "tiny"], ["-compress"],
        ["-ambient", "0.1", "tiny"], ["-ambient", "0.1", "0.2", "0.3", "tiny"], ["-colourgamma", "1", "tiny"],
        ["-minlight", "300", "tiny"], ["-minlight", "-5", "tiny"], ["-blur", "0.5", "tiny"], ["-smooth", "30", "tiny"],
        ["-dscale", "2", "tiny"], ["-scale", "3", "tiny"], ["-nosubdivide", "tiny"], ["-texlightgap", "3", "tiny"],
        ["-limiter", "-1", "tiny"], ["-gamma", "1", "tiny"], ["-softsky", "0", "tiny"], ["-extra", "-fast", "tiny"],
        ["-waddir", "/nonexistent", "tiny"], ["-unknown", "tiny"], ["tiny", "extra"], ["-low", "tiny"], ["-", "tiny"],
        # R4 (table-driven command line): the forms not covered above
        ["-colourscale", "1", "2", "3", "tiny"], ["tiny", "-colourscale", "1", "2"], ["tiny", "-ambient"],
        ["-circus", "-noskyfix", "-nospread", "-customshadowwithbounce", "-rgbtransfers", "-notexscale", "-chart",
         "-estimate", "-notextures", "tiny"],
        ["-dlight", "5", "-sky", "0.5", "-smooth2", "20", "-coring", "0.1", "-depth", "4", "-blockopaque", "0",
         "-texreflectgamma", "1.5", "-texreflectscale", "0.9", "-rgbcompress", "1", "-texdata", "8192", "tiny"],
        ["tiny", "-scale"], ["tiny", "-vismatrix"], ["-FAST", "-Extra", "tiny"],
        # S6a: atof reads blanks, '+', exponents and hex floats; atoi wraps past INT_MAX (4294967298 -> 2)
        ["-bounce", "4294967298", "tiny"], ["-bounce", " +2", "tiny"], ["-chop", "0x40", "tiny"],
        ["-texchop", " 3.2e1", "tiny"], ["-ambient", "0x1p-3", "+0.1", " 1e-1", "tiny"], ["-fade", "1e400", "tiny"],
        ["-minlight", "4294967306", "tiny"], ["-dscale", "2abc", "tiny"],
    ],
}
# hlvis needs a map with portals: its cases run on this corpus map (copied as tiny.map).
# "hlvis-empty" runs hlvis on the worldspawn-only map, whose .bsp has a dummy edge 0 referring to
# vertex 0 of 0: the bounds-checked loader rejected it from Phase 2 to V1, where the oracle got as
# far as the missing .prt. A case group "tool-suffix" runs that tool.
CLI_MAP_FROM_CORPUS = {"hlvis": "fm_vis_overview", "hlrad": "fm_rad_point"}
# Usage lines the candidate adds on purpose (new options): removed from its output before comparing.
CLI_ADDED_LINES = {
    "hlvis": [b"    -fastthreads    : Faster with many threads, but the result can differ from run to run\n"],  # V3
}
# Tools whose input comes from an earlier stage: the oracle's tools prepare it (same for both sides).
CLI_PREPARE = {"hlbsp": ["hlcsg"], "hlvis": ["hlcsg", "hlbsp"], "hlrad": ["hlcsg", "hlbsp", "hlvis"]}


def _drop_lines(data: bytes, lines: list[bytes]) -> bytes:
    for line in lines:
        data = data.replace(line, b"")
    return data


def _cli_run(exe: Path, argv: list[str], cwd: Path, group: str, added: list[bytes] = ()) -> tuple:
    cwd.mkdir(parents=True)
    if group in CLI_MAP_FROM_CORPUS:
        shutil.copy2(DATA / "corpus" / "maps" / f"{CLI_MAP_FROM_CORPUS[group]}.map", cwd / "tiny.map")
    else:
        (cwd / "tiny.map").write_text(CLI_MAP)
    for tool in CLI_PREPARE.get(exe.name, []):
        run([DATA / "builds" / "oracle" / "bin" / tool, "-threads", "1", "tiny"], cwd=cwd, capture_output=True, check=True)
    proc = run([exe, *argv], cwd=cwd, capture_output=True, timeout=60)
    # .bsp files without lump padding: the oracle leaves stale bytes there (check verifies the padding)
    files = {f.name: stagecache.bsp_canonical(f.read_bytes()) if f.suffix == ".bsp"
             else _drop_lines(normalize_log(f.read_bytes()), added) for f in sorted(cwd.iterdir())}
    return (proc.returncode, _drop_lines(normalize_log(proc.stdout), added),
            _drop_lines(normalize_log(proc.stderr), added), files)


# Path handling (S4a): hlcsg on GoldSrc-style paths the corpus never has (the MESS conversion writes
# the "wad" key with forward slashes): backslashes, drive letters, a leading backslash, WADROOT, empty
# and over-long "wad" entries, -wadinclude patterns, and map arguments with backslashes, dots and
# absolute paths. Files whose names contain a backslash exist so that the lookups that keep the
# backslash on POSIX succeed too. Each case runs the oracle and then the candidate in the same
# directory (absolute paths appear in the logs) and compares exit code, output and every file.
PATH_MAP = """{
"classname" "worldspawn"
"mapversion" "220"
"wad" "%s"
{
( 64 0 0 ) ( 64 0 64 ) ( 64 64 0 ) PATHTEX [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1
( 0 0 0 ) ( 0 64 0 ) ( 0 0 64 ) PATHTEX [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1
( 0 64 64 ) ( 0 64 0 ) ( 64 64 64 ) PATHTEX [ 1 0 0 0 ] [ 0 0 -1 0 ] 0 1 1
( 64 0 64 ) ( 64 0 0 ) ( 0 0 64 ) PATHTEX [ 1 0 0 0 ] [ 0 0 -1 0 ] 0 1 1
( 0 0 64 ) ( 0 64 64 ) ( 64 0 64 ) PATHTEX [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1
( 0 64 0 ) ( 0 0 0 ) ( 64 64 0 ) PATHTEX [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1
}
}
"""
WAD_OK = "wads/sub/pathtex.wad"
# name: wad key, wad files to create (other.wad holds OTHERTEX, every other file PATHTEX), and optionally
# hlcsg options, the map argument, where the map is written, and environment ({cwd}: the case directory).
PATH_CASES = {
    "wad forward slashes": {"wad": WAD_OK, "files": [WAD_OK]},
    "wad backslashes, file named with backslashes": {"wad": "wads\\sub\\pathtex.wad", "files": ["wads\\sub\\pathtex.wad"]},
    "wad backslashes, only the forward-slash file": {"wad": "wads\\sub\\pathtex.wad", "files": [WAD_OK]},
    "wad absolute": {"wad": "{cwd}/" + WAD_OK, "files": [WAD_OK]},
    "wad drive letter, forward slashes": {"wad": "C:" + WAD_OK, "files": [WAD_OK]},
    "wad drive letter, backslashes": {"wad": "C:\\wads\\pathtex.wad", "files": ["\\wads\\pathtex.wad"]},
    "wad drive-relative": {"wad": "C:pathtex.wad", "files": ["pathtex.wad"]},
    "wad leading backslash, found with a drive": {"wad": "\\wads\\pathtex.wad", "files": ["C:\\wads\\pathtex.wad"]},
    "wad leading backslash, not found": {"wad": "\\wads\\pathtex.wad", "files": [WAD_OK]},
    "WADROOT, forward slashes": {"wad": "/nonexistent/sub/pathtex.wad", "files": ["wadroot/sub/pathtex.wad"],
                                 "env": {"WADROOT": "wadroot"}},
    "WADROOT, backslash key": {"wad": "C:\\sierra\\half-life\\sub\\pathtex.wad", "files": ["wadroot/sub\\pathtex.wad"],
                               "env": {"WADROOT": "wadroot"}},
    # the "subdirectory" of D:\pathtex.wad is "\" (a path ending in a slash names a directory)
    "WADROOT absolute, root-level wad": {"wad": "D:\\pathtex.wad", "files": ["wadroot/\\pathtex.wad"],
                                          "env": {"WADROOT": "{cwd}/wadroot"}},
    "wad list with empty entries": {"wad": ";other.wad;;" + WAD_OK + ";", "files": ["other.wad", WAD_OK]},
    "wad list, -wadautodetect": {"wad": "other.wad;" + WAD_OK, "files": ["other.wad", WAD_OK], "argv": ["-wadautodetect"]},
    # longer than Windows' _MAX_PATH (260), so not cut here. The compat cut at _MAX_PATH - 1
    # (wadpath.cpp) can't be reached off Windows: _MAX_PATH is PATH_MAX (4096) and the tokenizer stops a
    # "wad" value of 4095 characters first.
    "wad entry of 300 characters": {"wad": "wads/sub/" + "x" * 300 + ".wad;" + WAD_OK, "files": [WAD_OK]},
    "wad entry without extension": {"wad": "wads/sub/pathtex", "files": ["wads/sub/pathtex"]},
    "wad directory as entry": {"wad": "wads/sub/;" + WAD_OK, "files": [WAD_OK]},
    "-wadinclude backslash pattern": {"wad": "wads\\sub\\pathtex.wad;other.wad", "files": ["wads\\sub\\pathtex.wad", "other.wad"],
                                      "argv": ["-wadinclude", "sub\\pathtex"]},
    "-wadinclude forward pattern, backslash path": {"wad": "wads\\sub\\pathtex.wad;other.wad",
                                                    "files": ["wads\\sub\\pathtex.wad", "other.wad"],
                                                    "argv": ["-wadinclude", "sub/pathtex"]},
    "map argument with backslashes": {"wad": WAD_OK, "files": [WAD_OK], "map": "maps\\pathmap", "map_at": "maps/pathmap.map"},
    "map argument with extension": {"wad": WAD_OK, "files": [WAD_OK], "map": "maps/pathmap.map", "map_at": "maps/pathmap.map"},
    "map argument absolute": {"wad": WAD_OK, "files": [WAD_OK], "map": "{cwd}/maps/pathmap", "map_at": "maps/pathmap.map"},
    "map argument with a dot in a directory": {"wad": WAD_OK, "files": [WAD_OK], "map": "maps.v2\\sub.d/pathmap",
                                               "map_at": "maps.v2/sub.d/pathmap.map"},
    "map argument .map": {"wad": WAD_OK, "files": [WAD_OK], "map": ".map", "map_at": ".map"},
    "map argument is a directory": {"wad": WAD_OK, "files": [WAD_OK], "map": "maps/", "map_at": "maps/pathmap.map"},
    # S5: the same texture name more than once (dup.wad: PATHTEX twice, red then blue; red.wad: one red
    # PATHTEX). Embedded with -wadinclude, the copy hlcsg picks ends up in the BSP's texture lump.
    "duplicate texture in one wad": {"wad": "dup.wad", "files": ["dup.wad"], "argv": ["-wadinclude", "dup"]},
    "duplicate texture across wads": {"wad": WAD_OK + ";red.wad", "files": [WAD_OK, "red.wad"],
                                      "argv": ["-wadinclude", "pathtex", "-wadinclude", "red"]},
    "duplicate texture, later wad included": {"wad": WAD_OK + ";red.wad;dup.wad", "files": [WAD_OK, "red.wad", "dup.wad"],
                                              "argv": ["-wadinclude", "dup"]},
}
# colours of the PATHTEX copies in the S5 wads above (every other file holds one grey PATHTEX)
DUP_WADS = {"dup.wad": [(255, 0, 0), (0, 0, 255)], "red.wad": [(255, 0, 0)]}


def _path_setup(case: dict, cwd: Path) -> tuple[list[str], dict]:
    if cwd.exists():
        shutil.rmtree(cwd)
    cwd.mkdir(parents=True)
    for rel in case["files"]:
        f = cwd / rel
        f.parent.mkdir(parents=True, exist_ok=True)
        if f.name in DUP_WADS:
            mkwad.write_wad_lumps(str(f), [("PATHTEX", mkwad.miptex("PATHTEX", 16, rgb)) for rgb in DUP_WADS[f.name]])
        else:
            mkwad.write_wad(str(f), ["OTHERTEX" if f.name == "other.wad" else "PATHTEX"], 16, (200, 200, 200))
    map_at = cwd / case.get("map_at", "pathmap.map")
    map_at.parent.mkdir(parents=True, exist_ok=True)
    map_at.write_text(PATH_MAP % case["wad"].replace("{cwd}", str(cwd)))
    env = {k: v for k, v in os.environ.items() if k != "WADROOT"}
    env.update({k: v.replace("{cwd}", str(cwd)) for k, v in case.get("env", {}).items()})
    argv = ["-threads", "1", *case.get("argv", []), case.get("map", "pathmap").replace("{cwd}", str(cwd))]
    return argv, env


def _path_run(exe: Path, case: dict, cwd: Path) -> tuple:
    argv, env = _path_setup(case, cwd)
    proc = run([exe, *argv], cwd=cwd, capture_output=True, timeout=60, env=env)
    files = {str(f.relative_to(cwd)): stagecache.bsp_canonical(f.read_bytes()) if f.suffix == ".bsp"
             else normalize_log(f.read_bytes()) for f in sorted(cwd.rglob("*")) if f.is_file()}
    return proc.returncode, normalize_log(proc.stdout), normalize_log(proc.stderr), files


# hlrad's own "wad" key loop (no .wa_, no -waddir: it names every wad it can't locate). hlcsg only
# ever writes bare file names there, so the key is put into the entity lump directly. An entry can't
# reach hlrad's MAX_VAL cut: the entity tokenizer stops at MAXTOKEN (same size) first.
PATH_RAD_CASES = {
    "hlrad wad key": "C:pathtex.wad;wads\\sub\\noext;" + "y" * 300 + ".wad;;/abs/dir/;C:\\hl\\valve\\halflife.wad",
}


def bsp_with_wad_key(data: bytes, key: str) -> bytes:
    """data with worldspawn's "wad" value replaced; the new entity lump is appended at the end."""
    ofs, length = struct.unpack_from("<ii", data, 4)  # lump 0: entities
    ents = data[ofs:ofs + length].rstrip(b"\0").decode("latin-1")
    ents = re.sub(r'"wad" "[^"]*"', lambda m: f'"wad" "{key}"', ents, count=1)
    lump = ents.encode("latin-1") + b"\0"
    out = bytearray(data) + lump
    struct.pack_into("<ii", out, 4, len(data), len(lump))
    return bytes(out)


def _path_rad_run(exe: Path, template: Path, key: str, cwd: Path) -> tuple:
    if cwd.exists():
        shutil.rmtree(cwd)
    shutil.copytree(template, cwd)
    bsp = cwd / "tiny.bsp"
    bsp.write_bytes(bsp_with_wad_key(bsp.read_bytes(), key))
    proc = run([exe, "-threads", "1", "tiny"], cwd=cwd, capture_output=True, timeout=60)
    files = {f.name: stagecache.bsp_canonical(f.read_bytes()) if f.suffix == ".bsp" else normalize_log(f.read_bytes())
             for f in sorted(cwd.iterdir())}
    return proc.returncode, normalize_log(proc.stdout), normalize_log(proc.stderr), files


def cli_paths(cand: Path, oracle: Path, tmp: Path) -> list[str]:
    failures = []

    def compare(name, ref, new):
        what = [n for n, a, b in zip(("exit code", "stdout", "stderr", "files"), ref, new) if a != b]
        if what:
            failures.append(f"paths: {name}: {', '.join(what)} differ")

    for i, (name, case) in enumerate(PATH_CASES.items()):
        cwd = tmp / f"paths-{i}"
        compare(name, _path_run(oracle / "hlcsg", case, cwd), _path_run(cand / "hlcsg", case, cwd))

    template = tmp / "paths-rad-template"
    template.mkdir()
    shutil.copy2(DATA / "corpus" / "maps" / f"{CLI_MAP_FROM_CORPUS['hlrad']}.map", template / "tiny.map")
    for tool in CLI_PREPARE["hlrad"]:
        run([oracle / tool, "-threads", "1", "tiny"], cwd=template, capture_output=True, check=True)
    (template / "tiny.wa_").unlink()
    for i, (name, key) in enumerate(PATH_RAD_CASES.items()):
        cwd = tmp / f"paths-rad-{i}"
        compare(name, _path_rad_run(oracle / "hlrad", template, key, cwd), _path_rad_run(cand / "hlrad", template, key, cwd))
    return failures


# Number parsing (S6a): the tools read numbers with atoi/atof/sscanf, which skip leading blanks, take a
# '+', read hex floats (atof/%f) and ignore trailing junk. Each mutation below respells the numbers of a
# feature map, and the whole pipeline runs, oracle vs candidate, so a parser that treats a spelling
# differently changes the result. plus/space/exp/hex keep the value for every parser the tools use.
# junk and edangle (the last number of each numeric value gets "x" or "e") don't for sscanf: glibc's %lf
# fails on "0x" and on "3e" and leaves the variable as it was, where atof/strtod/from_chars read 0 and 3.
# That is on purpose: those two are what tells sscanf apart from its replacements.
NUMBER_MAPS = ("fm_specials", "fm_transform", "fm_hullshape", "fm_lightstyles", "fm_compileparams", "fm_usemodel",
               "fm_rad_point", "fm_rad_spot", "fm_rad_sky", "fm_rad_texlights", "fm_rad_styles", "fm_rad_infoents",
               "fm_rad_brushkeys")
NUMBER_RE = re.compile(r"-?\d+(\.\d+)?")
KEYVALUE_RE = re.compile(r'^"([^"]*)" "([^"]*)"$')


def _respell(tok: str, how: str, brush: bool) -> str:
    if not NUMBER_RE.fullmatch(tok):
        return tok
    if how == "plus":
        return tok if tok.startswith("-") else "+" + tok
    if how == "exp" and (brush or "." in tok):  # atoi would stop at the 'e': only where atof/%f reads it
        sign, digits = ("-", tok[1:]) if tok.startswith("-") else ("", tok)
        whole, _, frac = digits.partition(".")
        return f"{sign}{whole}{frac}e-{len(frac)}" if frac else f"{sign}{whole}e0"
    if how == "hex" and brush and "." not in tok:  # brush numbers go through atof, which reads hex
        return f"-0x{-int(tok):x}" if tok.startswith("-") else f"0x{int(tok):x}"
    return tok


def respell_numbers(text: str, how: str) -> str:
    out = []
    for line in text.split("\n"):
        m = KEYVALUE_RE.match(line)
        if m:
            key, value = m.groups()
            toks = value.split(" ")
            if value and all(NUMBER_RE.fullmatch(t) for t in toks):
                if how == "space":
                    value = " " + value
                elif how == "junk":
                    value = value + "x"
                elif how == "edangle":
                    value = value + "e"
                else:
                    value = " ".join(_respell(t, how, False) for t in toks)
            line = f'"{key}" "{value}"'
        elif line.startswith("("):
            toks = line.split(" ")
            if how in ("junk", "edangle"):
                toks = [t + ("x" if how == "junk" else "e") if NUMBER_RE.fullmatch(t) else t for t in toks]
            else:
                toks = [_respell(t, how, True) for t in toks]
            line = " ".join(toks)
        out.append(line)
    return "\n".join(out)


NUMBER_MUTATIONS = ("plus", "space", "exp", "hex", "junk", "edangle")


def _number_run(bindir: Path, name: str, text: str, cwd: Path) -> tuple:
    if cwd.exists():
        shutil.rmtree(cwd)
    cwd.mkdir(parents=True)
    (cwd / f"{name}.map").write_text(text, encoding="latin-1")
    codes, out = [], b""
    for tool in ("hlcsg", "hlbsp", "hlvis", "hlrad"):
        proc = run([bindir / tool, "-threads", "1", name], cwd=cwd, capture_output=True, timeout=600)
        codes.append(proc.returncode)
        out += normalize_log(proc.stdout + proc.stderr)
        if proc.returncode:
            break
    files = {f.name: stagecache.bsp_canonical(f.read_bytes()) if f.suffix == ".bsp" else normalize_log(f.read_bytes())
             for f in sorted(cwd.iterdir())}
    return codes, out, files


def _number_case(cand: Path, oracle: Path, name: str, how: str, root: Path) -> str | None:
    text = respell_numbers((DATA / "corpus" / "maps" / f"{name}.map").read_text(encoding="latin-1"), how)
    cwd = root / f"numbers-{name}-{how}"
    ref = _number_run(oracle, name, text, cwd)
    new = _number_run(cand, name, text, cwd)
    what = [n for n, a, b in zip(("exit codes", "output", "files"), ref, new) if a != b]
    return f"numbers: {name} ({how}): {', '.join(what)} differ" if what else None


def cli_numbers(cand: Path, oracle: Path, tmp: Path) -> list[str]:
    with concurrent.futures.ThreadPoolExecutor(DEFAULT_JOBS) as pool:
        jobs = [pool.submit(_number_case, cand, oracle, m, how, tmp) for m in NUMBER_MAPS for how in NUMBER_MUTATIONS]
        return [r for r in (j.result() for j in jobs) if r]


def cmd_cli(args) -> int:
    # Lets `cli --bin <sanitize build>` run: findings show up as stderr differences, and leaks (by design) must not
    # change the exit code. Plain builds ignore both variables.
    os.environ.setdefault("ASAN_OPTIONS", "detect_leaks=0:halt_on_error=0:abort_on_error=0")
    os.environ.setdefault("UBSAN_OPTIONS", "print_stacktrace=1:halt_on_error=0")
    cand = check_bin(Path(args.bin))
    oracle = DATA / "builds" / "oracle" / "bin"
    failures = []
    with tempfile.TemporaryDirectory(dir=DATA) as tmp:
        for tool in args.tools:
            for i, argv in enumerate(CLI_CASES[tool]):
                exe = tool.split("-")[0]
                ref = _cli_run(oracle / exe, argv, Path(tmp) / f"{tool}-{i}-ref", tool)
                new = _cli_run(cand / exe, argv, Path(tmp) / f"{tool}-{i}-new", tool, CLI_ADDED_LINES.get(exe, []))
                what = [n for n, a, b in zip(("exit code", "stdout", "stderr", "files"), ref, new) if a != b]
                if what:
                    failures.append(f"{tool} {' '.join(argv) or '(no arguments)'}: {', '.join(what)} differ")
        if args.paths:
            failures += cli_paths(cand, oracle, Path(tmp))
        if args.numbers:
            failures += cli_numbers(cand, oracle, Path(tmp))
        n = (sum(len(CLI_CASES[t]) for t in args.tools) + (len(PATH_CASES) + len(PATH_RAD_CASES) if args.paths else 0)
             + (len(NUMBER_MAPS) * len(NUMBER_MUTATIONS) if args.numbers else 0))
        log(f"  {'PASS' if not failures else 'FAIL'}: {n} command lines, {len(failures)} difference(s)")
        for f in failures:
            log(f"    {f}")
    return 1 if failures else 0


# ---------------------------------------------------------------------------
# command: coverage (gcov line coverage of hlcsg over the csg tiers)


# Per tool: the tiers its coverage runs over, and the pipeline stage it stops after.
COVERAGE_TOOLS = {
    "hlcsg": {"tiers": lambda: ["quick", *VARIANT_TIERS], "last": "csg"},
    "hlbsp": {"tiers": lambda: ["quick", *BSP_VARIANT_TIERS], "last": "bsp"},
    "hlvis": {"tiers": lambda: ["quick", "full", *VIS_VARIANT_TIERS], "last": "vis"},  # quick: -fast
    "hlrad": {"tiers": lambda: ["quick", "full", *RAD_VARIANT_TIERS], "last": "rad"},
}


def _coverage_job(bindir: Path, tier: str, name: str, root: Path, last_stage: str) -> None:
    stages = tier_stages(tier)
    last = last_stage if stages[-1] == "rad" else stages[-1]
    run_chain(bindir, tier, name, root / tier / name, snapshots=False, last_stage=last)


def cmd_coverage(args) -> int:
    dest = DATA / "builds" / "coverage"
    build = dest / "build"
    if args.rebuild and dest.exists():
        shutil.rmtree(dest)
    generator = ["-G", "Ninja"] if shutil.which("ninja") else []
    run(["cmake", "--no-warn-unused-cli", "-S", REPO, "-B", build, *generator, "-DCMAKE_BUILD_TYPE=Debug",
         f"-DCMAKE_CXX_FLAGS=--coverage -O0 {PARITY_CXX_FLAGS}", "-DVHLT_BUILD_TESTS=OFF",
         f"-DVHLT_BUILD_DATE={oracle_build_date()}"], check=True, stdout=subprocess.DEVNULL)
    tool = args.tool
    tools = [t for _, t in STAGES]
    targets = tools[: tools.index(tool) + 1]  # a tool runs on the earlier tools' output
    proc = run(["cmake", "--build", build, "--target", *targets, "--parallel"], capture_output=True, text=True)
    if proc.returncode != 0:
        sys.exit(f"coverage build failed:\n{proc.stdout[-3000:]}{proc.stderr[-3000:]}")
    for gcda in build.rglob("*.gcda"):
        gcda.unlink()
    bindir = dest / "bin"
    if bindir.exists():
        shutil.rmtree(bindir)
    bindir.mkdir()
    for t in ("hlcsg", "hlbsp", "hlvis", "hlrad"):
        if t in targets:
            shutil.copy2(build / "bin" / t, bindir / t)
        else:
            (bindir / t).touch()  # never run, but check_bin-style layout keeps run_chain happy

    tiers = args.tiers or COVERAGE_TOOLS[tool]["tiers"]()
    maps = select_maps(args.maps)
    log(f"running {tool} (coverage build) on {len(maps)} maps x {len(tiers)} tiers")
    with tempfile.TemporaryDirectory(dir=DATA) as tmp:
        with concurrent.futures.ProcessPoolExecutor(args.jobs) as pool:  # libgcov merges counters under a file lock
            futs = [pool.submit(_coverage_job, bindir, t, n, Path(tmp), COVERAGE_TOOLS[tool]["last"])
                    for t in tiers for n in maps]
            for fut in concurrent.futures.as_completed(futs):
                fut.result()

    common_lib = "common_f64.dir" if tool in ("hlcsg", "hlbsp") else "common_f32.dir"  # vec_t precision
    # common/ counters are shared by every tool in the build: only meaningful for hlcsg's run,
    # which runs no other instrumented tool. For hlbsp they include hlcsg's use of common/.
    rows, funcs = [], []
    for objdir, sources in ((build / "CMakeFiles" / f"{tool}.dir" / "src" / tool, REPO / "src" / tool),
                            (build / "CMakeFiles" / common_lib / "src" / "common", REPO / "src" / "common")):
        gcdas = sorted(objdir.glob("*.gcda"))
        if not gcdas:
            continue
        out = run(["gcov", "-n", "-f", "-m", *map(str, gcdas)], cwd=objdir, capture_output=True, text=True).stdout
        for f, pct, n in re.findall(r"File '([^']+)'\nLines executed:([\d.]+)% of (\d+)", out):
            if Path(f).parent == sources:
                rows.append((f"{sources.name}/{Path(f).name}", float(pct), int(n)))
        if sources.name == tool:
            for fn, pct, n in re.findall(r"Function '([^']+)'\nLines executed:([\d.]+)% of (\d+)", out):
                if "::" not in fn.split("(")[0] or fn.startswith(("Winding", "BoundingBox")):
                    funcs.append((fn.split("(")[0], float(pct), int(n)))
    lines = [f"{'file':28} {'lines':>7}  covered"]
    for f, pct, n in sorted(rows):
        lines.append(f"{f:28} {n:7}  {pct:6.2f}%")
    hl = [r for r in rows if r[0].startswith(f"{tool}/")]
    total = sum(n for _, _, n in hl)
    covered = sum(n * pct / 100 for _, pct, n in hl)
    lines.append(f"{tool + '/ total':28} {total:7}  {100 * covered / max(total, 1):6.2f}%")
    lines.append(f"\n{tool} functions under 50% (by size):")
    for fn, (pct, n) in sorted({f: (p, n) for f, p, n in funcs}.items(), key=lambda x: -x[1][1]):
        if pct < 50 and n > 3:
            lines.append(f"  {pct:5.1f}% of {n:4}  {fn}")
    report = "\n".join(lines) + "\n"
    path = DATA / "coverage" / f"{datetime.datetime.now():%Y%m%d-%H%M%S}-{tool}.txt"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(report)
    log(report)
    log(f"saved {path}")
    return 0


# ---------------------------------------------------------------------------


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("build", help="build tools from a git ref (default: oracle-base) or the working tree")
    p.add_argument("--name")
    g = p.add_mutually_exclusive_group()
    g.add_argument("--ref", help=f"git ref to archive and build (default {ORACLE_REF})")
    g.add_argument("--tree", action="store_true", help="build the current working tree")
    p.add_argument("--cxx", default="g++")
    p.add_argument("--cxx-flags", help=f"override CMAKE_CXX_FLAGS (default '{PARITY_CXX_FLAGS}')")
    p.add_argument("--sanitize", action="store_true", help="add ASan/UBSan instrumentation")
    p.add_argument("--build-date", help="VHLT_BUILD_DATE, \"Mmm dd yyyy\" (default: the oracle's date)")
    p.add_argument("--force", action="store_true")
    p.set_defaults(func=cmd_build)

    p = sub.add_parser("corpus", help="convert RMFs and copy maps into the frozen corpus")
    p.add_argument("--force", action="store_true")
    p.set_defaults(func=cmd_corpus)

    def common(p, bin_required=True):
        p.add_argument("--bin", required=bin_required, help="directory with hlcsg/hlbsp/hlvis/hlrad")
        p.add_argument("--tier", choices=[*TIERS, *TIER_GROUPS], default="quick",
                       help="csg-variants: every csg-only option tier")
        p.add_argument("--maps", nargs="+")
        p.add_argument("--jobs", type=int, default=DEFAULT_JOBS)
        p.add_argument("--archived", action="store_true", help=f"also the archived maps ({', '.join(ARCHIVED_MAPS)})")
        p.add_argument("--full", action="store_true", help="the whole corpus instead of the core set (CORE_MAPS)")

    p = sub.add_parser("ref", help="record oracle outputs for a tier")
    common(p, bin_required=False)
    p.set_defaults(func=for_each_tier(cmd_ref))

    p = sub.add_parser("check", help="run a candidate and compare every stage against the oracle")
    common(p)
    p.add_argument("--mode", choices=["chain", "isolated", "both"], default="both")
    # Since Phase 2 the writer zero-fills lump padding; the oracle leaks stale bytes there.
    p.add_argument("--padding", choices=["zero-or-equal", "zero", "strict", "ignore"], default="zero")
    p.add_argument("--label")
    p.add_argument("--keep", action="store_true", help="keep outputs even when everything matches")
    p.add_argument("--stages", choices=["all", "csg", "rad"], default="all",
                   help="csg: stop after hlcsg, but only if hlbsp/hlvis/hlrad are byte-identical to the last "
                        "full pass of this tier (otherwise the full pipeline runs); rad: only hlrad, on the "
                        "oracle's VIS output of this tier (pre-baked input)")
    p.add_argument("--downstream-from", metavar="BUILD",
                   help="for --stages csg: compare downstream binaries with this build instead of the last full pass")
    p.add_argument("--threads", type=int, default=1,
                   help="run every tool with -threads N; compared with the single-threaded oracle references")
    p.add_argument("--no-cache", action="store_true",
                   help="run every tool (no reuse), refresh the cache, and fail where a run differs from its cache entry")
    p.set_defaults(func=for_each_tier(cmd_check))

    p = sub.add_parser("cache", help="size of the run/comparison cache used by check; --clear empties it")
    p.add_argument("--clear", action="store_true")
    p.set_defaults(func=cmd_cache)

    p = sub.add_parser("bench", help="time each stage (sequential, median of --repeat runs)")
    common(p)
    p.add_argument("--repeat", type=int, default=3)
    p.add_argument("--threads", type=int, default=1)
    p.add_argument("--label")
    p.add_argument("--baseline", help="bench JSON to compare against")
    p.set_defaults(func=cmd_bench)

    p = sub.add_parser("sanitize", help="ASan/UBSan run over the corpus; reports unique findings")
    common(p, bin_required=False)
    # clang++: its sanitizer runtime ships with the compiler (GCC needs the separate libasan/libubsan packages).
    p.add_argument("--cxx", default="clang++")
    p.add_argument("--baseline", help="previous findings file; fail only on new findings")
    p.add_argument("--leaks", action="store_true", help="also run LeakSanitizer and report leaked bytes per tool")
    p.set_defaults(func=cmd_sanitize)

    p = sub.add_parser("probe", help="old-vs-new differential of common/ BSP I/O, plus corruption smoke test")
    p.add_argument("--jobs", type=int, default=DEFAULT_JOBS)
    p.add_argument("--mutations", type=int, default=0, help="number of corrupted files to try (0: skip)")
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--sanitize", action="store_true", help="run the mutations against an ASan/UBSan build")
    p.set_defaults(func=cmd_probe)

    p = sub.add_parser("regress", help="candidate-only regression scenarios (oracle behaviour undefined there)")
    p.add_argument("--bin", required=True)
    p.set_defaults(func=cmd_regress)

    p = sub.add_parser("cli", help="oracle vs candidate on option-parsing command lines (output, files, exit code)")
    p.add_argument("--bin", required=True)
    p.add_argument("--tools", nargs="+", default=list(CLI_CASES), choices=list(CLI_CASES))
    p.add_argument("--no-paths", dest="paths", action="store_false", help="skip the path-handling cases (PATH_CASES, PATH_RAD_CASES)")
    p.add_argument("--no-numbers", dest="numbers", action="store_false", help="skip the number-spelling pipelines (NUMBER_MAPS)")
    p.set_defaults(func=cmd_cli)

    p = sub.add_parser("coverage", help="gcov line coverage of a tool over the quick tier (up to that tool) and its variant tiers")
    p.add_argument("--tool", choices=list(COVERAGE_TOOLS), default="hlcsg")
    p.add_argument("--tiers", nargs="+", choices=list(TIERS), help="instead of the tool's usual tiers")
    p.add_argument("--maps", nargs="+")
    p.add_argument("--jobs", type=int, default=DEFAULT_JOBS)
    p.add_argument("--rebuild", action="store_true")
    p.add_argument("--archived", action="store_true", help=f"also the archived maps ({', '.join(ARCHIVED_MAPS)})")
    p.add_argument("--full", action="store_true", help="the whole corpus instead of the core set (CORE_MAPS)")
    p.set_defaults(func=cmd_coverage)

    args = ap.parse_args()
    global INCLUDE_ARCHIVED, FULL_CORPUS, RAD_ROUTINE, RAD_TIER
    INCLUDE_ARCHIVED = getattr(args, "archived", False)
    RAD_TIER = getattr(args, "tier", "") in (*RAD_VARIANT_TIERS, "rad-variants")
    RAD_ROUTINE = (getattr(args, "stages", None) == "rad" or getattr(args, "tool", None) == "hlrad"
                   or getattr(args, "tier", "") in (*RAD_VARIANT_TIERS, "rad-variants"))
    FULL_CORPUS = getattr(args, "full", False) or args.cmd == "ref"  # references cover the whole corpus
    DATA.mkdir(parents=True, exist_ok=True)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
