# Tests

Two layers guard the modernization of `src/`:

| Layer | What it proves | Where |
|---|---|---|
| Unit tests (doctest) | `src/common` behaves exactly as at tag `oracle-base`, including output-affecting quirks | `tests/unit/` |
| Parity harness | hlcsg/hlbsp/hlvis/hlrad produce byte-identical files to the oracle build on a fixed corpus | `tests/parity/` |

Every change under `src/` must keep both green. A unit test may change only together with a
deliberate, documented behaviour change; quirks the tests pin are marked `compat:`.

## Unit tests

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

`common/` is built as two object libraries, `common_f64` (`DOUBLEVEC_T`, used by hlcsg and
hlbsp) and `common_f32` (used by hlvis and hlrad), and the tests are built against each:
`common_tests_f64` and `common_tests_f32`.
`test_cmdline.cpp` covers `ParseCommandLine` (common/cmdline.h), the table-driven option parser
the tools move to as they are modernized (hlcsg since H6; `parity.py cli` checks its command
lines against the oracle). `common_tests_f64` also holds the hlcsg pieces that build on their own (`test_hlcsg.cpp`:
`brushhull_t` copy/move ownership, `ANSItoUTF8`; `test_planeindex.cpp`).
The BSP round-trip test reads every `.bsp` in `VHLT_TEST_BSP_DIR` (CMake cache variable,
default `~/.steam/steam/steamapps/common/Half-Life/cstrike/maps`) and is skipped if there are none.

## Parity harness

Data lives outside the repo in `$VHLT_PARITY_DATA` (default `../vhlt-parity`):

```
vhlt-parity/
  builds/oracle/     src (git archive of oracle-base), bin, manifest.json (commit, compiler, flags, sha256)
  corpus/            maps/*.map, wads/ (frozen copies + generated placeholders), manifest.json
  refs/<tier>/<map>/ oracle snapshot after each stage: csg/ bsp/ vis/ rad/, result.json
  runs/              one folder per check: report.txt, report.json (outputs kept only on failure or --keep)
  bench/             timing JSON
  sanitize/          ASan/UBSan findings
```

### One-time setup

```sh
P=tests/parity/parity.py
python3 $P build                 # oracle from tag oracle-base, flags -ffp-contract=off -fno-fast-math
python3 $P corpus                # 18 RMFs via MESS + cstrike/maps/*.map minus EXCLUDED_MAPS (23 maps), canonical WAD lists, placeholders
python3 $P ref --tier quick      # oracle outputs (vis/rad -fast), ~1 min
python3 $P ref --tier full       # oracle outputs (default vis/rad), slow
```

`EXCLUDED_MAPS` in `parity.py` (currently `dm_*`) keeps maps out of the compile corpus:
dm_stalkfire_f1 took about a third of the full tier's time without adding coverage. The stock
`dm_stalkfire_f1.bsp` is still loaded by `probe` and the unit tests, as the only BSP with
embedded lightmaps.

### Checking a change

```sh
python3 $P build --tree --name cand --force
python3 $P check --bin ../vhlt-parity/builds/cand/bin              # quick tier, chain + isolated
python3 $P check --bin ../vhlt-parity/builds/cand/bin --tier full
```

- **chain**: the candidate runs all four tools; every stage's directory is compared with the oracle's.
- **isolated**: each tool after hlcsg runs on the *oracle's* output of the previous stage, so a
  difference is attributed to the tool that caused it.
- Files are compared byte for byte, `.bsp` structurally with `bspdiff.py` (record-level report),
  `.log`/`.err` after normalizing build date, argv[0] path and elapsed times.
- Exit codes must match too. `plants_soc_final` is a Quake-3-format map that hlcsg rejects; it
  checks that the error path is unchanged.
- `--padding`: the 0–3 bytes after each lump. The oracle leaks stale buffer bytes there; since
  Phase 2 the writer zero-fills, and the default `zero` requires that. `zero-or-equal` accepts either
  (for checking builds older than Phase 2).

All tools run with `-threads 1`. Multi-threaded hlcsg is not deterministic even between two runs
of the same binary (plane numbering depends on scheduling), so it cannot be compared byte for byte.

### Run cache

`check` reuses tool runs it has already done (`tests/parity/stagecache.py`, data in
`$DATA/cache`). A run's key holds everything that decides what the tool writes: the tool binary
and the other files in its directory, the arguments, the map name, and the content of every
file the tool starts with (plus the corpus WADs for hlcsg). `.log`/`.err` count by their
normalized content: the tools only append to the log and delete the .err, never read them.
Comparisons with the oracle are cached too, keyed by both sides' contents, the exit codes, the
padding mode and the harness source.

So only tools whose binary or input changed run. While working on hlbsp, for example, hlcsg's
runs are reused, and if hlbsp's output is unchanged, hlvis and hlrad are reused as well. Identical
runs inside one check are also shared between modes; the oracle's hlcsg output equals the
candidate's, so isolated and chain hlbsp often have the same key.

- Soundness rests on the tools being deterministic at `-threads 1` (checked when the oracle was
  set up). `check --no-cache` runs every tool, refreshes the cache and **fails** wherever a fresh
  run differs from its cache entry: run it at phase gates to confirm the reuse was valid.
- `--threads N` (N > 1) never uses the cache: those runs exist to catch nondeterminism.
- `parity.py cache` shows its size, `cache --clear` empties it. Outputs are stored by content,
  so repeated files cost nothing.
- Markers: `ok*` in the per-map lines means the run came from the cache; the summary line ends
  with `N/M tool runs from cache` (or `cache audit` / `cache off`).

Each map's chain and isolated runs are separate jobs, started longest first (by the oracle's
times), so one slow map no longer serializes its two halves at the end of a run.

### Feature maps and csg option tiers

The Valve maps in the corpus exercise under half of hlcsg's brush code. Synthetic maps generated by
`tests/parity/featuremaps/make_feature_maps.py` (committed under `featuremaps/maps/`, copied into the
corpus by `corpus`) cover the rest:

| Map | Exercises |
|---|---|
| `fm_specials` | CLIP/CLIPHULLn/CLIPBEVEL*/BEVEL*/NOCLIP/HINT/SKIP/SOLIDHINT/SPLITFACE/CONTENT* textures, sky clip, water, animated and random textures, detail levels, func_group, ORIGIN/BOUNDINGBOX brushes, clip-economy entities, non-axial and sloped brushes |
| `fm_hullshape` | info_hullshape: custom, default-replacing (twice), empty, disabled, duplicate names |
| `fm_compileparams` | info_compile_parameters (including a larger texdata) |
| `fm_lightstyles` | texlight styles -1/-2/-3 before named lights, zhlt_usestyle, duplicate light names |
| `fm_usemodel`, `fm_transform`, `fm_gametext` | zhlt_usemodel, zhlt_transform, game_text Windows-1252 conversion |
| `fm_err_*` | hlcsg's error paths (exit 1, log compared); `fm_err_hull_overflow` reaches the CreateHullBrush stack overflow |

csg-only tiers run hlcsg alone: `csg-default` (default options), then one option each: `csg-smallest`, `csg-normalized`, `csg-precise`,
`csg-legacy`, `csg-clipeconomy`, `csg-noskyclip`, `csg-nolightopt`, `csg-nowadtextures`,
`csg-wadinclude`, `csg-misc`, and `csg-onlyents` (a second hlcsg `-onlyents` run on the first
run's `.bsp`). `--tier csg-variants` means all of them (about 15 s for the whole corpus).

```sh
python3 $P ref --tier csg-variants               # oracle references (once)
python3 $P check --bin DIR --tier csg-variants
python3 $P coverage                              # gcov of hlcsg over quick csg + every variant tier
```

Coverage of `src/hlcsg` at H1: 88.0% of lines (brush.cpp 88.7%, hlcsg.cpp 89.1%, map.cpp 87.8%,
textures.cpp 85.8%), up from 46-67% with the Valve maps alone. `corpus` refuses to change the
content of an existing map silently: it reports maps whose references are now stale.

### csg-only checks and multi-threaded checks

```sh
python3 $P check --bin DIR --stages csg          # stop after hlcsg (about 2 s for the corpus instead of about 45 s)
python3 $P check --bin DIR --threads 16          # run every tool multi-threaded, compare with the -threads 1 refs
```

- `--stages csg` compares everything hlcsg writes (`.p*`, `.b*`, `.pln`, `.hsz`, `.wa_`, `.bsp`, log,
  exit code). The downstream tools only see hlcsg through those files, so this is sufficient **as
  long as hlbsp/hlvis/hlrad are unchanged**. The harness enforces that: csg-only runs only if the
  candidate's `hlbsp`, `hlvis` and `hlrad` are byte-identical to the binaries of the last build that
  passed a full single-threaded check of the whole corpus on the same tier
  (`$DATA/builds/last-full-pass-<tier>.json`, written automatically). Otherwise it says why and runs
  the full pipeline. `--downstream-from BUILD` compares with a named build instead.
- Builds are reproducible: `build` passes the oracle's date as `VHLT_BUILD_DATE` (the date is in the
  banner and in every `.bsp`'s worldspawn `compiler` key), so unchanged sources give identical
  binaries. `--build-date` overrides it.
- `--threads N` normalizes only the thread count in logs (command line, settings table, "Running
  with N threads"). Baseline at H0: 22 of 22 compiling maps differ at the csg stage with 16 threads.
  Since H4, hlcsg with any thread count writes exactly what the oracle's `-threads 1` writes:
  `check --tier csg-variants --threads 16` passes (`csg-default` is plain hlcsg). The downstream
  tools are still nondeterministic with threads, so full-pipeline `--threads` checks fail there.
- ThreadSanitizer (hlcsg, 16 threads; 0 warnings since H4, thousands at oracle-base):

  ```sh
  python3 $P build --tree --name tsan --cxx clang++ --cxx-flags "-ffp-contract=off -fno-fast-math -fsanitize=thread -g" --force
  # then run ../vhlt-parity/builds/tsan/bin/hlcsg -threads 16 on corpus maps and grep for "WARNING: ThreadSanitizer"
  ```

### Index verification build

hlcsg's plane, texinfo and miptex lookups use exact-result indexes (H3). A build with
`-DVHLT_VERIFY_INDEXES` also runs the old linear scans on every lookup and stops with an error on
any disagreement:

```sh
python3 $P build --tree --name verify --cxx-flags "-ffp-contract=off -fno-fast-math -DVHLT_VERIFY_INDEXES" --force
python3 $P check --bin ../vhlt-parity/builds/verify/bin --stages csg && python3 $P check --bin ../vhlt-parity/builds/verify/bin --tier csg-variants
```

### Other commands

```sh
python3 $P bench --bin DIR --repeat 3 [--maps cs_assault ...] [--threads N] [--baseline ../vhlt-parity/bench/oracle-quick-t1.json]
python3 $P sanitize --baseline ../vhlt-parity/sanitize/baseline-h2-quick.txt   # ASan+UBSan (clang++) build of the tree; fails on new findings
python3 $P sanitize --leaks [--bin DIR]          # also LeakSanitizer: leaked bytes per tool (reported, not compared)
python3 $P regress --bin DIR                     # candidate-only scenarios where the oracle's behaviour is undefined (e.g. texdata-growth)
python3 $P cli --bin DIR [--tools hlcsg]         # oracle vs candidate on option-parsing command lines: output, files, exit code
python3 $P probe [--mutations 400 --sanitize]   # common/ BSP I/O, oracle vs tree, on stock + corpus BSPs; corruption smoke test
python3 tests/parity/bspdiff.py a.bsp b.bsp [--padding strict]
python3 tests/parity/mkwad.py out.wad NAME... [--size 64] [--rgb 255,255,255]
```

### Harness self-tests (done when the oracle was set up)

| Build | Expected | Result |
|---|---|---|
| oracle vs its own refs | PASS (determinism, log normalization) | PASS |
| working tree, same flags | PASS | PASS |
| oracle source without the pinned flags | PASS (flags are neutral for GCC) | PASS |
| `-march=native -ffp-contract=fast` (FMA) | FAIL | FAIL, 122 stages |

### Sanitizer baseline (oracle-base, quick tier)

No ASan findings. UBSan reported four issues at oracle-base:

| Finding | Tools | Planned fix |
|---|---|---|
| `common/bspfile.cpp:204` `vecs[0][j]` with `j < 8` in the no-op `SwapBSPFile` | all | fixed in Phase 1 (`SwapBSPFile` deleted) |
| `hlcsg/hlcsg.cpp:298` `g_texinfo[-1]` | hlcsg | fixed in H2 |
| `hlbsp/brink.cpp:1131` pointer arithmetic overflow | hlbsp | hlbsp phase |
| `hlvis/hlvis.cpp:123` member access through a null `winding_t*` | hlvis | hlvis phase |

Since H2 the baseline is `../vhlt-parity/sanitize/baseline-h2-quick.txt` (39-map corpus): hlcsg has
no findings left (H2 fixed the `g_texinfo[-1]` pointer, the CreateHullBrush stack overflow, the
`g_csg_mapplanes[-1]` reads after "plane with no normal", and the texture-buffer heap overflow);
only the hlbsp and hlvis findings above remain.

Leaks (`sanitize --leaks`, quick tier): hlcsg leaked 2.5 MB on 34 of 39 maps before H5 (hull
faces and their windings dropped by MakeHullFaces and by clearing the hull-0 faces of
ORIGIN/BOUNDINGBOX/cliphull brushes, plus the `.wa_` lump table); since H5 it leaks nothing.
Still leaking: hlbsp about 29 MB over 29 maps, hlrad about 0.5 MB over 23 maps (their phases).

### Environment

| Variable | Default |
|---|---|
| `VHLT_PARITY_DATA` | `<repo>/../vhlt-parity` |
| `VHLT_GAME_DIR` | `~/.steam/steam/steamapps/common/Half-Life` |
| `VHLT_MESS` | `<repo>/../mess_1.2.3_linux64/MESS` |
| `VHLT_RMF_ROOT` | `<repo>/../GoldSrc-map-sources-restored` |
