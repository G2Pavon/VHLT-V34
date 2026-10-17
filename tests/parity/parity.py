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
import hashlib
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import bspdiff

REPO = Path(__file__).resolve().parents[2]
DATA = Path(os.environ.get("VHLT_PARITY_DATA", REPO.parent / "vhlt-parity"))
GAME = Path(os.environ.get("VHLT_GAME_DIR", "~/.steam/steam/steamapps/common/Half-Life")).expanduser()
MESS = Path(os.environ.get("VHLT_MESS", REPO.parent / "mess_1.2.3_linux64" / "MESS"))
RMF_ROOT = Path(os.environ.get("VHLT_RMF_ROOT", REPO.parent / "GoldSrc-map-sources-restored"))
EXTRA_MAP_DIR = GAME / "cstrike" / "maps"

ORACLE_REF = "oracle-base"
# Pinned so that no compiler default (e.g. Clang's -ffp-contract=on) can change float results.
PARITY_CXX_FLAGS = "-ffp-contract=off -fno-fast-math"
SANITIZE_CXX_FLAGS = "-fsanitize=address,undefined -fsanitize-recover=address,undefined -fno-omit-frame-pointer -g"

STAGES = [("csg", "hlcsg"), ("bsp", "hlbsp"), ("vis", "hlvis"), ("rad", "hlrad")]
TIERS = {
    "quick": {"csg": [], "bsp": [], "vis": ["-fast"], "rad": ["-fast"]},
    "full": {"csg": [], "bsp": [], "vis": [], "rad": []},
}
STAGE_TIMEOUT = 4 * 3600
DEFAULT_JOBS = max(1, min(os.cpu_count() or 1, 8))

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


def corpus_maps() -> list[str]:
    manifest = DATA / "corpus" / "manifest.json"
    if not manifest.exists():
        sys.exit(f"no corpus at {manifest.parent}; run 'parity.py corpus' first")
    return sorted(read_json(manifest)["maps"])


def select_maps(requested: list[str] | None) -> list[str]:
    available = corpus_maps()
    if not requested:
        return available
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


def cmd_build(args) -> int:
    name = args.name or ("oracle" if not args.tree else "tree")
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
         "-DVHLT_BUILD_TESTS=OFF"], check=True, stdout=subprocess.DEVNULL)
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
        out = maps_dir / f"{name}.map"
        proc = run([MESS, "-convert", "-mapformat", "trenchbroomvalve220", "-tbgame", "Half-Life",
                    "-wad", ";".join(map(str, canon)), rmf, out], capture_output=True, text=True)
        if proc.returncode != 0 or not out.exists():
            log(f"  MESS failed on {rmf}:\n{proc.stdout[-2000:]}{proc.stderr[-2000:]}")
            return 1
        sources[name] = {"source": str(rmf), "source_sha256": sha256(rmf), "converted_by": "MESS 1.2.3 (trenchbroomvalve220)"}
        log(f"  converted {name}")

    for m in sorted(EXTRA_MAP_DIR.glob("*.map")):
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

    write_json(root / "manifest.json", manifest)
    log(f"corpus: {len(manifest['maps'])} maps in {maps_dir}")
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


def run_chain(bindir: Path, tier: str, name: str, root: Path, threads: int = 1, snapshots: bool = True) -> dict:
    """Run all stages in one directory; snapshot the directory after each stage into root/<stage>."""
    work = root / "work"
    if root.exists():
        shutil.rmtree(root)
    work.mkdir(parents=True)
    shutil.copy2(DATA / "corpus" / "maps" / f"{name}.map", work / f"{name}.map")
    results = {}
    for stage, tool in STAGES:
        r = run_stage(bindir, tool, tool_args(tier, stage, threads), name, work, root / "console", stage)
        results[stage] = r
        if snapshots:
            snapshot(work, root / stage)
        if r["exit"] != 0:
            break
    shutil.rmtree(work)
    return results


def run_isolated(bindir: Path, tier: str, name: str, ref_root: Path, root: Path) -> dict:
    """Run each stage after the first on the oracle's output of the previous stage."""
    results = {}
    for i, (stage, tool) in enumerate(STAGES[1:], start=1):
        prev = ref_root / STAGES[i - 1][0]
        if not prev.exists() or not (ref_root / stage).exists():
            continue
        work = root / f"{stage}.work"
        if work.exists():
            shutil.rmtree(work)
        shutil.copytree(prev, work)
        results[stage] = run_stage(bindir, tool, tool_args(tier, stage, 1), name, work, root / "console", stage)
        snapshot(work, root / stage)
        shutil.rmtree(work)
    return results


# ---------------------------------------------------------------------------
# comparing


LOG_RULES = [
    (re.compile(rb"\(\w{3} [ \d]\d \d{4}\)"), b"(<build date>)"),             # Banner: __DATE__
    (re.compile(rb"^(Command line: )\S*/(hl\w+)", re.M), rb"\1\2"),            # argv[0] path
    (re.compile(rb"\(\d+\.\d+ seconds\)"), b"(<t> seconds)"),                  # RunThreadsOn
    (re.compile(rb"\d+\.\d+ seconds elapsed( \[[\dhms ]+\])?"), b"<t> seconds elapsed"),
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
                "tier": args.tier, "args": {s: tool_args(args.tier, s, 1) for s, _ in STAGES},
                "recorded": datetime.datetime.now().isoformat(timespec="seconds")})
    with concurrent.futures.ProcessPoolExecutor(args.jobs) as pool:
        futures = [pool.submit(_ref_job, bindir, args.tier, n) for n in maps]
        for fut in concurrent.futures.as_completed(futures):
            name, res = fut.result()
            log(f"  {name:24} " + "  ".join(f"{s}={r['exit']}/{r['seconds']:.1f}s" for s, r in res.items()))
    return 0


def _check_job(bindir: Path, tier: str, name: str, run_root: Path, mode: str, padding: str) -> dict:
    ref_root = DATA / "refs" / tier / name
    ref_res = read_json(ref_root / "result.json")
    out = {"map": name, "chain": {}, "isolated": {}}
    if mode in ("chain", "both"):
        root = run_root / "chain" / name
        res = run_chain(bindir, tier, name, root)
        for stage, _ in STAGES:
            if stage in ref_res or stage in res:
                out["chain"][stage] = {"result": res.get(stage), "problems": compare_stage(ref_root, ref_res, root, res, stage, padding)}
    if mode in ("isolated", "both"):
        root = run_root / "isolated" / name
        res = run_isolated(bindir, tier, name, ref_root, root)
        for stage in res:
            out["isolated"][stage] = {"result": res[stage], "problems": compare_stage(ref_root, ref_res, root, res, stage, padding)}
    return out


def cmd_check(args) -> int:
    bindir = check_bin(Path(args.bin))
    maps = select_maps(args.maps)
    if not (DATA / "refs" / args.tier / "manifest.json").exists():
        sys.exit(f"no oracle reference for tier {args.tier}; run 'parity.py ref --tier {args.tier}'")
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    run_root = DATA / "runs" / f"{stamp}-{args.label or bindir.parent.name}-{args.tier}"
    run_root.mkdir(parents=True)

    results = []
    with concurrent.futures.ProcessPoolExecutor(args.jobs) as pool:
        futures = [pool.submit(_check_job, bindir, args.tier, n, run_root, args.mode, args.padding) for n in maps]
        for fut in concurrent.futures.as_completed(futures):
            r = fut.result()
            results.append(r)
            cells = []
            for mode in ("chain", "isolated"):
                for stage, s in r[mode].items():
                    cells.append(f"{mode[0]}:{stage}={'ok' if not s['problems'] else 'DIFF'}")
            log(f"  {r['map']:24} {' '.join(cells)}")

    results.sort(key=lambda r: r["map"])
    lines = []
    failures = 0
    for r in results:
        for mode in ("chain", "isolated"):
            for stage, s in r[mode].items():
                if s["problems"]:
                    failures += 1
                    lines.append(f"[{r['map']}] {mode} {stage}:")
                    lines += [f"  {p}" for p in s["problems"]]
    summary = (f"parity {'PASS' if not failures else 'FAIL'}: {len(results)} maps, tier {args.tier}, mode {args.mode}, "
               f"padding {args.padding}, {failures} differing stage(s)")
    report = "\n".join([summary, *lines]) + "\n"
    (run_root / "report.txt").write_text(report)
    write_json(run_root / "report.json", {"bin": str(bindir), "summary": summary, "results": results})
    log("")
    log(report if failures else summary)
    log(f"report: {run_root / 'report.txt'}")
    if not args.keep and not failures:
        for mode in ("chain", "isolated"):
            shutil.rmtree(run_root / mode, ignore_errors=True)
    return 1 if failures else 0


# ---------------------------------------------------------------------------
# commands: bench, sanitize


def cmd_bench(args) -> int:
    bindir = check_bin(Path(args.bin))
    maps = select_maps(args.maps)
    label = args.label or bindir.parent.name
    out = {"bin": str(bindir), "tier": args.tier, "threads": args.threads, "repeat": args.repeat, "maps": {}}
    with tempfile.TemporaryDirectory(dir=DATA) as tmp:
        for name in maps:
            samples: dict[str, list[float]] = {s: [] for s, _ in STAGES}
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
        build = argparse.Namespace(name="sanitize", tree=True, ref=None, cxx=args.cxx, cxx_flags=None, sanitize=True, force=True)
        cmd_build(build)
        bindir = DATA / "builds" / "sanitize" / "bin"
    maps = select_maps(args.maps)
    findings: dict[str, set[str]] = {}
    with tempfile.TemporaryDirectory(dir=DATA) as tmp:
        with concurrent.futures.ProcessPoolExecutor(args.jobs) as pool:
            futs = {pool.submit(run_chain, bindir, args.tier, n, Path(tmp) / n, 1, False): n for n in maps}
            for fut in concurrent.futures.as_completed(futs):
                name = futs[fut]
                fut.result()
                for stage, tool in STAGES:
                    err = Path(tmp) / name / "console" / f"{stage}.stderr"
                    if err.exists():
                        for f in summarize_sanitizer(err.read_bytes()):
                            findings.setdefault(f"{tool}: {f}", set()).add(name)
                log(f"  {name}")
    lines = [f"{k}  [{len(v)} maps: {', '.join(sorted(v)[:4])}{', ...' if len(v) > 4 else ''}]" for k, v in sorted(findings.items())]
    report = f"sanitizer findings: {len(findings)} unique\n" + "\n".join(lines) + "\n"
    path = DATA / "sanitize" / f"{datetime.datetime.now():%Y%m%d-%H%M%S}-{args.tier}.txt"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(report)
    log(report)
    log(f"saved {path}")
    if args.baseline:
        base = {l.split("  [")[0] for l in Path(args.baseline).read_text().splitlines()[1:] if l}
        new = sorted(set(findings) - base)
        log(f"{len(new)} finding(s) not in baseline" + (":\n  " + "\n  ".join(new) if new else ""))
        return 1 if new else 0
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
    p.add_argument("--force", action="store_true")
    p.set_defaults(func=cmd_build)

    p = sub.add_parser("corpus", help="convert RMFs and copy maps into the frozen corpus")
    p.add_argument("--force", action="store_true")
    p.set_defaults(func=cmd_corpus)

    def common(p, bin_required=True):
        p.add_argument("--bin", required=bin_required, help="directory with hlcsg/hlbsp/hlvis/hlrad")
        p.add_argument("--tier", choices=TIERS, default="quick")
        p.add_argument("--maps", nargs="+")
        p.add_argument("--jobs", type=int, default=DEFAULT_JOBS)

    p = sub.add_parser("ref", help="record oracle outputs for a tier")
    common(p, bin_required=False)
    p.set_defaults(func=cmd_ref)

    p = sub.add_parser("check", help="run a candidate and compare every stage against the oracle")
    common(p)
    p.add_argument("--mode", choices=["chain", "isolated", "both"], default="both")
    p.add_argument("--padding", choices=["zero-or-equal", "zero", "strict", "ignore"], default="zero-or-equal")
    p.add_argument("--label")
    p.add_argument("--keep", action="store_true", help="keep outputs even when everything matches")
    p.set_defaults(func=cmd_check)

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
    p.set_defaults(func=cmd_sanitize)

    args = ap.parse_args()
    DATA.mkdir(parents=True, exist_ok=True)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
