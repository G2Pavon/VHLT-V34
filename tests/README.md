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
the tools move to as they are modernized (hlcsg since H6, hlbsp since B5, hlvis since V4, hlrad since R4, whose three-value options use
`numvalues`/`applymany`;
`parity.py cli` checks their command lines against the oracle). `common_tests_f64` also holds the hlcsg pieces that build on their own (`test_hlcsg.cpp`:
`brushhull_t` copy/move ownership, `ANSItoUTF8`; `test_planeindex.cpp`). `common_tests_f32` also holds
hlrad's transfer compression (`test_compress.cpp`): every float/vector type, packed and unpacked at
each alignment, against a table produced by the oracle's compress.h (the corpus never reaches some
of its paths, e.g. a zero component of an RGB transfer).
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
python3 $P smoke                 # while editing: seconds
python3 $P gate                  # before committing a step
python3 $P check                 # quick tier, chain + isolated (one command of the gate)
python3 $P check --tier full
```

`check`, `cli`, `regress`, `bench`, `smoke` and `gate` run the working tree's `build/` unless given
`--bin DIR`. `parity.py` builds it first (`cmake --build build`), stops on a compiler warning, and
configures it as the oracle was built (Release, the oracle's build date) when it isn't: the build date
is written into every `.bsp`, and a Debug build would be 20 times slower. The binaries are the same
as `build --tree --name cand` makes (same compiler and flags), so both share the cache.

- **`smoke`**: the build, then at the same time the unit tests, `check --smoke --fail-fast` (the
  `SMOKE_MAPS`: cs_assault, a feature map for each tool, an error map and the two lighting maps),
  `cli --smoke` (every 6th option case, 3 number maps) and all of `regress`. A tripwire, not a
  gate: it fails on a planted bug in any tool, but a bug in an option or a map it skips needs `gate`.
- **`gate`**: all of the following, with the jobs of every tier in one queue (longest first) and
  the steps that aren't `check` running at the same time: `check` on quick, quick again with
  `--no-cache` (the audit), full, `--stages rad`, and every group of variant tiers; unit tests; `regress`;
  `cli`; `sanitize --baseline`. Options: `--tiers`, `--skip unit regress cli sanitize probe`,
  `--fail-fast`, `--milestone` (whole corpus with the archived maps, `sanitize --full --archived
  --leaks`, `probe --mutations 400 --sanitize`), `--release` (every check is an audit: nothing from
  the cache; the cache is refreshed and must agree).

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

### Map sets

Routine runs (`check`, `sanitize`, `bench`, `coverage`) use the core set, `CORE_MAPS` in
`parity.py`: 25 maps (the feature maps, plants_soc_final, unnamed-compile, cs_assault, cs_estate,
cs_militia, de_dust2). It was chosen from per-map line coverage of hlcsg, hlbsp and hlvis over the
quick tier, every csg/bsp/vis tier and the full tier (greedy, cost-weighted), with the expensive
tail replaced by `fm_clipeconomy`. It covers every line the whole corpus covers except three (a
brink loop exit, the "world faces exceeded" warning, a tjunc split branch) at about 14% of the
corpus' tool time. hlrad was not part of that measurement.

- `--full` uses the whole corpus; `--archived` adds the archived maps (`ARCHIVED_MAPS`:
  liminal_plaza, out of every routine run since 2026-10-18); `--maps` names maps explicitly.
- `ref` always records the whole corpus (and the archived maps), so `--full` works at any time.
- hlrad runs (rad tiers, `--stages rad`, `coverage --tool hlrad`) use the rad micro-corpus instead,
  `RAD_CORE_MAPS` (see hlrad tiers below).
- Cold gate on the core set (every tool changed, empty cache): quick 6 s, full 23 s, all csg tiers
  8 s, bsp tiers 9 s, vis tiers 6 s; `--no-cache` quick 9 s; sanitize 60 s.

### Run cache

`check` reuses tool runs it has already done (`tests/parity/stagecache.py`, data in
`$DATA/cache`). A run's key holds everything that decides what the tool writes: the tool binary
and the other files in its directory, the arguments, the map name, and the content of every
file the tool starts with (plus the corpus WADs for hlcsg). `.log`/`.err` count by their
normalized content: the tools only append to the log and delete the .err, never read them.
Comparisons with the oracle are cached too, keyed by both sides' contents, the exit codes, the
padding mode and the harness source.

Besides `check`, the cache answers `cli` (an option case is keyed like a stage: tool, arguments,
and the prepared directory; the number-spelling pipelines run through the same stage chain, for the oracle
as well) and `sanitize` (the sanitizer's stderr is stored with the run). Keys include `ASAN_OPTIONS` /
`UBSAN_OPTIONS` when they are set, because they change what a sanitizer build prints. A tool binary is identified
by its own bytes, plus `entities.void` if one sits next to it (the oracle and older builds read it); the other files of a directory are not part of a
key, so `build/bin` can hold the unit tests.

So only tools whose binary or input changed run. `.bsp` inputs are keyed without their lump
padding (the oracle leaves stale bytes there, the tools zero them; LoadBSPFile never reads them), and
a run that another job is already doing is waited for (lock files in `$DATA/cache/locks`), so a
map's chain and isolated runs never do the same work twice. While working on hlbsp, for example, hlcsg's
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
times), so one slow map no longer serializes its two halves at the end of a run. `gate` does the
same across tiers: with one queue for all of them, four uncached rad tiers took 45 s instead of 78 s.
Every run of a tool has a time limit of 20 times the oracle's time for that stage (200 times for a
sanitizer build), at least 5 minutes: longer counts as a hang (`exit: timeout`) instead of stopping the run
after 4 hours.

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
| `fm_bsp_leak` | a 16-unit hole only hull 0 leaks through: hlbsp's leak trail (`.pts`/`.lin`, no `.prt`; hlvis then stops) |
| `fm_bsp_models` | brush models without visible faces (head node split on an arbitrary plane), without clip hulls, several brushes, origin brush |
| `fm_bsp_geometry` | stairs, slopes, a 40-sided column (faces above MAXPOINTS), boxes on large faces (T-junctions), rotated and off-grid brushes (brinks) |
| `fm_vis_overview` | zones that cannot all see each other, an `info_overview_point` and a reversed one (skybox point) |
| `fm_rad_point`, `fm_rad_spot` | point lights (1-, 3-, 4-value `_light`, `_fade`, a styled light with `zhlt_stylecoring`); `light_spot` aimed by target, angle -1/-2, angles + pitch, a missing target |
| `fm_rad_sky` | a sky opening, `light_environment` (sun, `_diffuse_light`, `_spread`), a second sun with `_fake`, a `_sky` spot |
| `fm_rad_texlights` | texture lights from `info_texlights` and from a light with `_tex` (light_surface filters, `_scale`, `_chop`, `_texcolor`, `_texlightgap`), animated and toggled light textures |
| `fm_rad_styles` | more than four styles on one face (discard path), a styled func_wall, `light_shadow`, `light_bounce` |
| `fm_rad_opaque`, `fm_rad_infoents` | `zhlt_lightflags` opaque models, `zhlt_customshadow` (1 and 3 values), a translucent texture; `info_smoothvalue`, `info_chopscale`, `info_angularfade`, `info_translucent` |
| `fm_rad_brushkeys` | `_minlight`, `zhlt_striprad`, `zhlt_embedlightmap`, `light_origin` + `model_center`, `zhlt_copylight` |
| `fm_rad_compileparams` | `info_compile_parameters` as hlrad reads it (vismatrix, bounce, chop, ambient, dscale, lightdata, rgb transfers) |
| `fm_rad_err_spread`, `fm_rad_err_ambient` | hlrad's error stops (`_spread` outside 0..180, ambient outside 0..1) |

The `fm_rad_*` maps (except the error maps) are scaled to half size by the generator: a full-tier
hlrad run takes 1-2 s each instead of 8-15 s.

A leak in more than one hull is undefined behaviour in the oracle: after hull 0, hlbsp writes the
next hull's leak trail to the point file it has already closed (it crashes or not, from run to
run). `fm_bsp_leak` therefore leaks in hull 0 only; the multi-hull case is a candidate-only
`regress` scenario once hlbsp is fixed.

csg-only tiers run hlcsg alone: `csg-default` (default options), then one option each: `csg-smallest`, `csg-normalized`, `csg-precise`,
`csg-legacy`, `csg-clipeconomy`, `csg-noskyclip`, `csg-nolightopt`, `csg-nowadtextures`,
`csg-wadinclude`, `csg-misc`, and `csg-onlyents` (a second hlcsg `-onlyents` run on the first
run's `.bsp`). `--tier csg-variants` means all of them (about 15 s for the whole corpus).

bsp tiers run hlcsg (default options) and hlbsp with one option set: `bsp-nohull2`,
`bsp-viewportal`, `bsp-subdivide64`, `bsp-subdivide512`, `bsp-maxnodesize64`,
`bsp-maxnodesize65536`, `bsp-clamped` (out-of-range values: warning, then clamped; liminal_plaza
then exceeds MAX_MAP_VERTS, an error path) and `bsp-misc` (`-chart -estimate -texdata -lightdata`).
`--tier bsp-variants` means all of them.

vis tiers run hlcsg and hlbsp (default options) and hlvis with one option set: `vis-full`,
`vis-maxdistance512`, `vis-maxdistance2048` and `vis-misc` (`-chart -estimate -texdata
-lightdata`); `--tier vis-variants` means all of them. The quick tier covers `-fast`, the full tier
default vis. Recording `vis-full` takes long (liminal_plaza); the run cache keeps it to one run per
hlvis change.

```sh
python3 $P ref --tier csg-variants               # oracle references (once; also bsp-variants)
python3 $P check --bin DIR --tier csg-variants
python3 $P coverage [--tool hlbsp]               # gcov of a tool over the quick tier (up to it) + its variant tiers
```

Coverage of `src/hlcsg` at H1: 88.0% of lines (brush.cpp 88.7%, hlcsg.cpp 89.1%, map.cpp 87.8%,
textures.cpp 85.8%), up from 46-67% with the Valve maps alone. `corpus` refuses to change the
content of an existing map silently: it reports maps whose references are now stale.

Coverage of `src/hlbsp` before any hlbsp change: 86.9% with the Valve and hlcsg feature maps on the
quick tier, 89.9% with the `fm_bsp_*` maps and bsp tiers (MarkLeakTrail 0% -> covered, outside.cpp
75.0 -> 87.5%, portals.cpp 82.4 -> 93.3%). The rest is mostly error-message helpers, `Usage()`
(covered by `cli`). (The `.void` loader was in this list until it was removed.)

Coverage of `src/hlvis` before any hlvis change: 61.0% on the quick and full tiers (MaxDistVis,
WindingDist, BestNormalFromWinding, SaveVisData, VisLeafnumForPoint never ran), 90.9% with the vis
tiers and `fm_vis_overview` (flow.cpp 53.0 -> 95.9%). Measured without the three slowest vis maps
(`coverage --maps`), which the -O0 coverage build would take very long on; `--tiers` picks tiers.

### hlrad tiers, pre-baked inputs and the rad micro-corpus

hlrad iteration does not rerun hlcsg, hlbsp and hlvis. A rad tier starts from a **pre-baked
input**: the oracle's VIS-complete directory of the full tier, `refs/full/<map>/vis` (`.bsp`,
`.wa_`, `.ext`, ...), copied into the work directory, and runs hlrad alone with one option set
(`"input": RAD_INPUT` in `TIERS`). A map that stopped before vis (an error map, `fm_bsp_leak`) has
no input and is skipped. Rad tiers run in chain mode only (with the oracle's input, chain and
isolated are the same run).

20 rad tiers, `--tier rad-variants`: `rad-extra`, `rad-bounce0/1/16`, `rad-chop`, `rad-smooth`,
`rad-vismatrix-normal`, `rad-vismatrix-off` (default: sparse), `rad-compress0/2` (FLOAT32/FLOAT8
transfers), `rad-rgb`, `rad-rgb0/1/3` (`-rgbtransfers` with VECTOR32/96/48/24), `rad-customshadow`,
`rad-sky`, `rad-colour`, `rad-colour2` (gamma/scale/limiter/minlight/coring/ambient/... options),
`rad-notextures` and `rad-misc` (`-chart -estimate -texdata -lightdata -notexscale -nosubdivide
-texlightgap`). The quick tier covers `-fast`, the full tier default hlrad.

```sh
python3 $P check --bin DIR --tier rad-variants   # hlrad x 20 option sets on the micro-corpus
python3 $P check --bin DIR --stages rad          # quick tier, hlrad alone on the oracle's quick vis output
python3 $P check --bin DIR --stages rad --tier full
python3 $P coverage --tool hlrad                 # quick + full + rad tiers
```

`--stages rad` is the isolated hlrad stage of the quick or full tier (input `refs/<tier>/<map>/vis`).
The rad tiers ever run on the **rad pool**: the feature maps and the real maps whose full-tier
hlrad takes at most ~31 s (`RAD_TIER_REAL_MAPS`); `ref` records the pool, `--full` checks it.

Routine hlrad runs use the **rad micro-corpus**, `RAD_CORE_MAPS`: the eleven `fm_rad_*` maps,
`unnamed` and `cs_estate`. It was chosen from per-map hlrad line coverage of the pool (43 maps)
over the quick, full and all rad tiers (gcov, greedy, cost-weighted): of the 6541 hlrad lines the
pool reaches, the feature maps and `unnamed` reach 6429 and `cs_estate` 87 more (sky visibility,
edge smoothing and seam handling that need real geometry). The 25 left need the heavy maps
(`cs_militia`, `cs_747`, `cs_siege`, `fm_compileparams`, `fm_specials`, `fm_lightstyles`: discarded
direct lights, a degenerate light map, `info_compile_parameters` turning `-extra` off, negative
texlight styles); `--full` covers them. The micro-corpus costs about 10% of the pool's rad-tier time.
Cold (new hlrad binary, empty cache for it): all 20 rad tiers on the micro-corpus 4 min; the pool
(`--full`) 13 min more.

Coverage of `src/hlrad` on the micro-corpus (before any hlrad change): 84.0% of 7287 lines on the
quick and full tiers, 89.7% with the rad tiers (compress.h 53.1 -> 91.0%, nomatrix.cpp 33.3 ->
91.7%, transparency.cpp 19.3 -> 64.8%, loadtextures.cpp 72.9 -> 86.6%, hlrad.cpp 77.3 -> 85.3%).
6.5 minutes with the -O0 coverage build. Largest uncovered functions: `Usage()` (covered by `cli`),
`CalcSightArea_SpotLight`, the opaque-style arrays in transparency.cpp (`AddStyleToStyleArray`,
`CreateFinalStyleArrays`, `SortStyleList`) and `AddWadFolder` (`-waddir`, broken on Linux until R4).

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
  `check --tier csg-variants --threads 16` passes (`csg-default` is plain hlcsg). Measured after H6
  on the whole pipeline at 16 threads: the quick tier matches the `-threads 1` references entirely;
  in the full tier hlvis differed on 15 of 29 maps (hlrad only because of its input). Since V3 hlvis
  writes the `-threads 1` result with any thread count (portals are processed in the single-thread
  order, and a flow waits for the earlier portals it uses): `check --tier full --threads 16` and
  `--tier vis-variants --threads 16` pass, also on the 14 maps that used to differ. `-fastthreads`
  keeps the old faster behaviour (output can vary); `regress hlvis-fastthreads` covers it, and
  `cli` ignores its usage line (`CLI_ADDED_LINES`). ThreadSanitizer, hlvis at 16 threads: 0 warnings
  in either mode (oracle: 186 on cs_assault, 398 on de_dust2). hlbsp is
  single-threaded, so it always matches (until B4 `-threads` was parsed into a local and discarded;
  now it sets the thread count shown in the settings table, checked by `regress hlbsp-threads`).
- hlrad's BSP output never depended on the thread count; since R3 its log does not either. With
  `ThreadSetOrderedLog(true)` (common/threads.h) every work item's output is captured from
  `GetThreadWork` handing it out until the thread asks for the next, and written in work order; a
  captured `Error()` ends the run after the earlier items' output (the thread's `ThreadLock`s are
  released first). Output that depends on state shared by items runs at replay (`LogInOrder`): the
  "too many light styles" counter and `PrintOnce`. No corpus map logs from an hlrad worker, so
  `test_threads.cpp` checks the order (scrambled finish times, both runner styles) and the error
  path (a forked run: exit 1, exactly the earlier items' output, no deadlock). Also: the unread
  `g_maxdiscardedlight` race (now `NoteDiscardedLight`), and the transparency/style searches start
  each work item at `lower_bound(p1)` instead of relying on increasing items per thread.
  ThreadSanitizer, hlrad at 16 threads on 8 option/map runs: 0 warnings (before R3: the discarded
  light race on every map). Time at 16 threads unchanged.
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
python3 $P sanitize --baseline ../vhlt-parity/sanitize/baseline-v1-quick.txt   # ASan+UBSan (clang++) build of the tree; fails on new findings
python3 $P sanitize --leaks [--bin DIR]          # also LeakSanitizer: leaked bytes per tool (reported, not compared)
python3 $P regress --bin DIR                     # candidate-only scenarios (oracle undefined or deliberately changed): texdata-growth, leak-all-hulls, hlbsp-threads, hlvis-fastthreads, lightdata-growth, waddir, texdata-zero, rad-bad-texture, bsp-texvec-hang, void-ignored
python3 $P smoke                                 # build, unit tests, check --smoke, cli --smoke, regress: seconds
python3 $P gate [--tiers T...] [--skip S...] [--milestone | --release] [--fail-fast]   # every tier in one queue + unit tests, regress, cli, sanitize
python3 $P check [--smoke] [--fail-fast]         # --bin defaults to build/ (as do cli, regress, bench)
python3 $P cli --bin DIR [--tools hlcsg hlbsp hlvis hlrad] [--smoke] [--no-paths] [--no-numbers]  # oracle vs candidate on option-parsing command lines, path handling and number spellings: output, files, exit code. With the sanitize build (`build --tree --name sanitize --sanitize --cxx clang++`) it doubles as a sanitizer run over these inputs: findings appear as stderr differences
python3 $P probe [--mutations 400 --sanitize]   # common/ BSP I/O, oracle vs tree, on stock + corpus BSPs; corruption smoke test
python3 $P fuzz --bin SANITIZE_DIR --plain DIR [--seed 1 --count 600 --start 0 --maps fm_specials ...]   # corrupted .map files through all four tools, no oracle; findings = sanitizer reports, crashes, hangs (reproducers in $VHLT_PARITY_DATA/fuzz)
python3 tests/parity/bspdiff.py a.bsp b.bsp [--padding strict]
python3 tests/parity/mkwad.py out.wad NAME... [--size 64] [--rgb 255,255,255]
```

`cli` runs hlrad's 40 command lines on `fm_rad_point` (prepared by the oracle's hlcsg, hlbsp and
hlvis). It compares `.bsp` files without their lump padding (`stagecache.bsp_canonical`): the
oracle leaves stale bytes there, which `check --padding zero` already covers.

`cli` also runs the path-handling cases (S4a, `PATH_CASES`), because the corpus never gives hlcsg a
backslash path (MESS writes the `"wad"` key with forward slashes). There are 25 hlcsg setups:
`"wad"` entries with backslashes, drive letters (`C:\...`, drive-relative `C:x.wad`), a leading
backslash, `WADROOT` (relative, absolute, a backslash key), empty entries, a 300-character entry,
no extension, a directory, `-wadinclude` patterns with either slash, and map arguments with
backslashes, an extension, an absolute path, dotted directories, `.map` and a directory. Files whose
names contain a backslash are created so that the lookups that keep the backslash on POSIX also
succeed. One hlrad case (`PATH_RAD_CASES`) puts such a key straight into the entity lump. Each case
runs the oracle and then the candidate in the same directory. Mutation check (2026-10-19): splitting
only at `/` (what `std::filesystem::path` does on POSIX) fails 7 cases, normalizing `\` to `/` in the
wad list without `-wadinclude` fails 9, and dropping hlrad's default `.wad` fails 1. The
`wadpath.cpp` cut at `_MAX_PATH - 1` can't be reached off Windows (see the comment in `PATH_CASES`).

`cli` also runs the number-spelling pipelines (S6a, `NUMBER_MAPS` × `NUMBER_MUTATIONS`, 78
pipelines, about 70 s of the gate). 13 feature maps go through the whole pipeline, oracle vs
candidate, with every number respelled: `+N`, a leading blank on numeric values, exponent form, hex
in brush coordinates (atof reads it), and trailing `x` or `e` on the last number. The first four keep
the value for every parser the tools use: 52 of 52 compile to the same lumps apart from the entity
text. `junk` and `edangle` don't for `sscanf`, on purpose: glibc's `%lf` fails on `0x` and on `3e`
(where atof/strtod/from_chars read 0 and 3), so `"zhlt_transform" "0 32 0x"` takes a different
branch. Option values get explicit cases: blanks, `+`, hex floats, `1e400`, and values past `INT_MAX`
that glibc's `atoi` wraps (`-bounce 4294967298` is 2 bounces). Mutation check (2026-10-25):
`from_chars` in `IntForKey` fails 12 pipelines, in the brush plane numbers 26, and three `strtod`
calls instead of `sscanf` in `GetVectorForKey` fail 12 (`edangle`). The `-estimate` progress
estimates are timing noise and are normalized (`LOG_RULES`).

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
| `hlbsp/brink.cpp:1131` pointer arithmetic overflow | hlbsp | fixed in B1 |
| `hlvis/hlvis.cpp:123` member access through a null `winding_t*` | hlvis | fixed in V1 |

Since H2 the baseline is `../vhlt-parity/sanitize/baseline-h2-quick.txt` (39-map corpus): hlcsg has
no findings left (H2 fixed the `g_texinfo[-1]` pointer, the CreateHullBrush stack overflow, the
`g_csg_mapplanes[-1]` reads after "plane with no normal", and the texture-buffer heap overflow);
only the hlbsp and hlvis findings above remain.

Since B1 the baseline is `../vhlt-parity/sanitize/baseline-b1-quick.txt` (42 maps): hlbsp has no
findings left on the quick tier or any bsp tier; only hlvis's remains. B1 also fixed undefined
behaviour no sanitizer reports: the leak trail of a second leaking hull written to closed FILEs
(`regress leak-all-hulls`), and std::maps keyed by heap addresses in brink.cpp (now creation order;
output unchanged on every tier).

Since V1 the baseline is `../vhlt-parity/sanitize/baseline-v1-quick.txt`: no findings in any tool
(hlvis computed a size through a null `winding_t*`). V1 also fixed what no sanitizer reported on the
corpus: portal windings allocated shorter or longer than `winding_t` and indexed past its array,
ChopWinding's wrap-around entry one past its arrays, an off-by-one leaf check, an unchecked 64 KB
portal buffer, a one-byte-short leaf bit string, bit strings read through `long*`, and
`-maxdistance` with INT_MIN. The bounds-checked BSP loader accepts again the dummy edge 0 of a map
without geometry (rejected since Phase 2; `cli` group `hlvis-empty`).

Leaks (`sanitize --leaks`, quick tier): hlcsg leaked 2.5 MB on 34 of 39 maps before H5 (hull
faces and their windings dropped by MakeHullFaces and by clearing the hull-0 faces of
ORIGIN/BOUNDINGBOX/cliphull brushes, plus the `.wa_` lump table); since H5 it leaks nothing.
hlbsp leaked 32 MB over all 32 compiling maps before B2 (every BSP tree, pruned subtrees, clip-hull
faces, split fragments, portals on the split plane); since B2 it leaks nothing on the quick tier
or any bsp tier that completes (an Error exit leaves the tree being written behind, by design).
hlrad leaked about 0.5 MB over 23 maps before R1 (the winding of every face without a patch, e.g.
sky, and each sky light's sun normals); since R1 it leaks nothing on the full tier or any rad tier.

R1 (hlrad): the full tier showed 5 UBSan findings at oracle-base and up to V4, all in
`hlrad/compress.h` (transfers packed at 1-6 byte strides, read and written as misaligned 32-bit
words; float bits through `unsigned*`). Since R1: no findings and no leaks on the full tier or the
rad tiers (`sanitize --tier rad-variants`, 260 hlrad runs). compress.h now goes through memcpy and
`std::bit_cast` with the same bytes, keeping the oracle's arithmetic for two shifts that were
undefined: `bitget(x, s, 32)` (GCC folded it to `x >> s`) and the VECTOR32/24 mantissa shift by
`1 + max - exponent`, which can reach 32 or more and which the oracle's x86 `shr` takes mod 32
(`test_compress.cpp`). R1 also fixed, without sanitizer reports on the corpus: the lighting buffer
overflow when `info_compile_parameters` raises `lightdata` (`regress lightdata-growth`; v4 dies
with SIGBUS), `pvs2` read uninitialized for a translucent face's first patch, the luxel flags read
past `w*h`, an overlapping `strcpy` and unbounded texture-name copies, texture lump reads
(`dataofs`, miptex records) without bounds or alignment, an unchecked face miptex index (now an
error), the wad `wad`-key copy, signed overflows (`* 1024`, wad offsets), enum casts before
their range check, and the final light's float-to-int conversion (NaN or out of range: INT_MIN,
as the oracle's x86 conversion gives, then clamped to 0).

R2 (hlrad ownership): every allocation is owned (vectors, `unique_ptr`, stores). Three constraints
keep the output: `patch_t` stays trivially copyable (`static_assert`), because SortPatches orders
patches with qsort (glibc's stable merge sort: the oracle's order) and subdivision copies them with
memcpy, so patch windings live in a store and transfers in `g_patch_transfers`; the lists that used
to be prepended (edge vertex faces, face light dependencies) are read newest first, the order that
sets the summation order; and logged sizes keep their old arithmetic (the transparency/style
arrays' 1.41x capacity, `sizeof(sparse_column_t)`, AllocBlock's zero-size warning). hlrad time
unchanged (full tier, one job: cs_estate 11.25 s, cs_assault 14.69 s, de_dust2 15.75 s; R1 11.25,
14.96, 15.87 s).

R4 (hlrad command line and cleanup): the options are a `ParseCommandLine` table (`cli`: 48 hlrad
command lines, 8 added for forms the old loop handled separately); `-waddir` joins the directory
and the wad name with the system's separator (it used a backslash everywhere, so off Windows no
wad was ever found through it: `regress waddir` lights fm_rad_texlights without its `.wa_` and
must equal the oracle's run with it; the oracle exits 1). hlrad builds without warnings; the
`#pragma warning` lines and the last win32fix shims (`CDECL`, `FORCEINLINE`, `sprintf_s`) are gone.

Phase C (ownership in common/, hlcsg, hlbsp): blockmem (unused since the tools stopped calling
it) is gone; `Winding::Copy()` returns a `unique_ptr`; winding temporaries are values or
`unique_ptr`; `bface_t` nodes are made and freed only in hlcsg/faces.cpp (`NewFace`/`FreeFace`;
`brushhull_t` owns its list); hlbsp's `outputedges` is a vector and the brink analysis a typed
owning handle (`Brinkinfo`). hlbsp's graph nodes (tree, portals that two nodes share, surfaces,
brushes) and the brink tree stay behind their single Alloc/Free functions by design (LeakSanitizer
0 bytes). `MAX_SUPERFACEEDGES` (tjunc.cpp) is pinned at 680, the value the oracle's 1208-byte
`face_t` gave, now that `face_t` is larger. The MSVC `#pragma warning` lines of the common headers
are `/wd` options in CMakeLists.txt. All four tools and common/ build without warnings (GCC).

### Environment

| Variable | Default |
|---|---|
| `VHLT_PARITY_DATA` | `<repo>/../vhlt-parity` |
| `VHLT_GAME_DIR` | `~/.steam/steam/steamapps/common/Half-Life` |
| `VHLT_MESS` | `<repo>/../mess_1.2.3_linux64/MESS` |
| `VHLT_RMF_ROOT` | `<repo>/../GoldSrc-map-sources-restored` |
