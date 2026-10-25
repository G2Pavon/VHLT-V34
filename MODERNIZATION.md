# VHLT-V34 modernization: project context

This file is the starting point for anyone working on this code, people or AI agents. It explains
what the project is, what has changed, how the tests keep the code under control, what has been
audited, and what is still open. `readme.md` is the user-facing summary, and `tests/README.md` is
the full manual for the test harness.

State as of 2026-10-25: branch `modernize` at `10804d9` (the `.map` fuzzer), plus the test-harness speedups (`smoke`, `gate`, section 4).

---

## 1. What this project is

VHLT v34 is a set of map compile tools for GoldSrc (Half-Life 1) BSP v30 maps:
`hlcsg` → `hlbsp` → `hlvis` → `hlrad`. This branch rewrites the original C-style code as C++20
under one hard rule:

> **The modernized tools must write the same files as the original tools, byte for byte.**

The original tools are called **the oracle**. They are built from tag `oracle-base` (`07575e9`:
the original v34 code plus `CMAKE_CXX_STANDARD 20`), with the same compiler and flags as the
candidate. Every change is checked against the oracle on a fixed corpus of maps.

Ground rules in force:

| Rule | Meaning |
|---|---|
| Byte-identical output | `.bsp`, `.log`, `.err` and intermediate files match the oracle. The one deliberate exception is the padding between BSP lumps, which the oracle filled with stale memory. The candidate writes zeros. |
| `// compat:` comments | Where the original behaviour is odd but reaches the output (truncation, a quirky parse, a wrap-around), it is kept and marked `// compat:` with the reason. There are about 45 of them in `src/`. Don't "fix" one without making the change on purpose and testing it. |
| Single-threaded reference | Parity runs use `-threads 1`. Multi-threaded output is required to equal `-threads 1` (section 4). |
| Reproducible build date | The build date is written into every `.bsp` (worldspawn `"compiler"` key). Builds compared against the oracle must set `-DVHLT_BUILD_DATE="Oct 17 2026"`. `parity.py build` does it automatically, and the dev tree `build/` is configured with it. |
| Platform | Developed and verified on Linux (Fedora, GCC 16, glibc 2.43). Little-endian only. The Windows/MSVC build has **not** been tested since the modernization started. |
| Commits | One commit per step, each building and passing the unit tests on its own. Commit messages say what was verified. |

---

## 2. Workspace layout

```
CHLT/
  VHLT-V34/                       this repository
    src/common/                   shared code (BSP I/O, math, windings, tokenizer, logging, threads, cmdline)
    src/hlcsg src/hlbsp src/hlvis src/hlrad
    tests/unit/                   doctest unit tests (common_tests_f64 / common_tests_f32)
    tests/parity/                 parity.py harness, bspdiff.py, stagecache.py, mkwad.py
    tests/parity/featuremaps/     make_feature_maps.py + 40 synthetic fm_* maps
    tests/probe/                  bsp_probe.cpp (BSP loader comparison and corruption smoke test)
    tests/README.md               harness manual
    readme.md                     user-facing summary
    MODERNIZATION.md              this file
  vhlt-parity/                    harness data, not in git ($VHLT_PARITY_DATA)
    builds/oracle/ builds/cand/   oracle and candidate binaries
    corpus/maps, corpus/wads      63 maps and frozen WAD copies
    refs/<tier>/<map>/            oracle outputs per stage
    cache/                        content-addressed cache of tool runs
    runs/ sanitize/ bench/ coverage/
  GoldSrc-map-sources-restored/   18 .rmf sources for the corpus (converted with MESS)
  mess_1.2.3_linux64/             MESS, used to convert .rmf to .map
```

The tag `oracle-base` (`07575e9`, the first commit of `modernize`) is on origin since 2026-10-25;
`git fetch --tags` gets it.

---

## 3. Quick start

```sh
# parity (one-time setup: tests/README.md, "One-time setup")
P=tests/parity/parity.py
python3 $P smoke        # while editing: build, unit tests, a few maps, a cli subset, regress. 4 s warm, ~20 s after a change
python3 $P gate         # before committing a step: every tier, unit tests, regress, cli, sanitize. 11 s warm
```

The candidate is the working tree's `build/`. The harness configures it as the oracle was built
(Release, the oracle's build date; `CMakeLists.txt` pins the float flags), rebuilds it, and fails
on a compiler warning, so there is no separate `cand` build to make. A `build/` configured as
Debug, or with `__DATE__`, is reconfigured: it would differ in the `.bsp` "compiler" key, and run
the unit tests and the tools about 20 times slower. `--bin DIR` runs another build instead.
Section 4.3 has the gate.

---

## 4. How the tests keep the code under control

The code is about 34,500 lines of C++ whose output has to stay byte-identical while almost every
line is rewritten. That's only possible because every kind of input that can change the output
is covered by a test, and every test compares against the oracle rather than against a stored
expectation that could drift.

### 4.1 Layers

| Layer | Command | What it catches |
|---|---|---|
| Smoke | `parity.py smoke` | The edit-run loop, not a gate: builds `build/`, then at the same time the unit tests, `check --smoke` (8 maps, quick tier, `--fail-fast`), `cli --smoke` (every 6th option case, 3 number maps) and every `regress` scenario. 4 s with everything cached, about 20 s after a change to hlrad. Planted bugs in each tool and in `common/` all fail it. |
| Gate | `parity.py gate [--milestone] [--release]` | Section 4.3. |
| Unit tests (doctest, 127 cases) | `ctest --test-dir build` | `common/` behaviour pinned to the oracle (path helpers, tokenizer, BSP round-trip, compression tables generated from the oracle, cmdline parsing, `ParseInt`, `ProbeSearch` against glibc's `bsearch`, ...). Built twice, as `f64` (hlcsg/hlbsp) and `f32` (hlvis/hlrad). |
| Parity `check` | `parity.py check --bin $B [--tier T] [--stages rad] [--full]` | Every stage's files against the oracle's reference outputs: 46 tiers (`quick`, `full`, 12 csg, 8 bsp, 4 vis and 20 rad option tiers) on the corpus. |
| Corpus | 63 maps | 18 converted Valve/community maps, stock `cstrike` maps, and 40 synthetic feature maps (`fm_*`) generated so that hlcsg/hlbsp/hlvis/hlrad features the real maps never use get exercised, including error paths (`fm_err_*`). |
| `cli` | `parity.py cli --bin $B` | 256 command lines, oracle vs candidate, live (exit code, stdout, stderr, every file): option parsing; **path handling** (S4a: backslashes, drive letters, `WADROOT`, `-wadinclude`, duplicate texture names); **number spellings** (S6a: 78 full pipelines with `+N`, blanks, exponents, hex, trailing junk; overflow cases). |
| `regress` | `parity.py regress --bin $B` | 11 candidate-only scenarios where the oracle crashed or behaved differently on purpose (heap overflows, bad texture index, endless face subdivision, multi-hull leak trail, `-waddir`, honest `-threads`, `-fastthreads`). |
| `sanitize` | `parity.py sanitize --baseline ../vhlt-parity/sanitize/baseline-v1-quick.txt [--leaks]` | ASan + UBSan (clang build) over the tiers; fails on any finding not in the baseline (the baseline is empty). `--leaks` adds LeakSanitizer. The `cli` cases run under the sanitizers too, with `cli --bin` pointing at the sanitize build (about 6 min): findings show up as stderr differences. First run (2026-10-25): one finding, a `memcpy` of a null source with size 0 in hlbsp `writebsp.cpp` (maps with no textures), fixed with a guard. |
| `probe` | `parity.py probe [--mutations 400 --sanitize]` | `common/` BSP loader/writer against the oracle's on 58 BSPs, plus a seeded corruption smoke test. |
| `fuzz` | `parity.py fuzz --bin ../vhlt-parity/builds/sanitize/bin --plain $B [--seed N --count 600]` | Seeded corruptions of the `fm_*` maps (syntax damage, entity key/value edits, number respellings, plane nudges) through hlcsg → hlbsp → hlvis → hlrad on the sanitize build. No oracle: a finding is a sanitizer report, a crash by signal, or a tool that does not stop (rechecked with the plain build, since the sanitizers slow the tools 10 to 20 times). Reproducers are kept in `fuzz/`; `--start N --count 1` repeats a case. About 15 min per 600 cases, roughly 45% of them get through all four tools. |
| `coverage` | `parity.py coverage --tool hlcsg` | gcov line coverage per tool over its tiers; used to choose feature maps and the core map set. |
| `bench` | `parity.py bench --bin $B` | Timing against `bench/oracle-quick-t1.json`. |
| ThreadSanitizer | manual | 0 data races in hlcsg/hlvis/hlrad at 16 threads (the oracle had hundreds). |

Speed features that keep the gate short: a content-addressed **run cache** (`stagecache.py`) only
reruns tools whose binary, arguments or inputs changed. If every run in a gate is a cache hit, the
binaries produce identical output, which is itself a proof. The cache serves `check`, `cli` (the
option cases, the prepared inputs and the number-spelling pipelines, for the oracle too) and
`sanitize` (the sanitizer output of a run is stored with it). `gate` puts the jobs of all tiers in
**one queue, longest first**, so the pool does not drain at the end of each tier: four uncached
rad tiers took 78 s one after the other and 45 s together. A tool that runs longer than 20 times
the oracle's time (200 times for a sanitizer build; at least 5 minutes) counts as hung, instead
of waiting 4 hours. **`CORE_MAPS`** (33 maps: 25 chosen by coverage, 8 added for the code those
missed, see 5.4c) is the routine set; `--full` runs the whole corpus. **`RAD_CORE_MAPS`** (13 maps) is
the routine set for hlrad. `liminal_plaza` is archived (very slow) and only runs with
`--archived` at the end of a milestone.

### 4.2 Testing workflow

1. **Before changing code that has no tests, write the tests first.** Each phase started with a
   test step (`H1`, `B0`, `V0`, `R0`, `S4a`, `S6a`) that added feature maps, tiers or cases and
   recorded oracle results. The corpus only ever spelled `"wad"` keys with forward slashes and
   numbers in one way, so before S4a/S6a a broken path or number parser could have passed
   everything.
2. **Prove the tests can fail.** New tests are mutation-checked: plant the bug the step could
   introduce, build it, and confirm the tests catch it. Examples from this work:
   - splitting paths only at `/` (what `std::filesystem::path` does on Linux): 7 path cases fail;
   - `std::sort` instead of `std::stable_sort` for hlrad patches: 2 lighting bytes differ in
     `fm_rad_texlights`;
   - `from_chars` in `IntForKey`: 12 number pipelines fail;
   - `strtod` instead of `sscanf`: 12 fail (dangling-exponent spelling).
3. **Iterate fast:** `parity.py smoke` after each edit. For one map, `check --maps cs_assault`
   (plus `--stages rad` for hlrad). No sanitizers mid-step.
4. **Gate before committing** (section 4.3): `parity.py gate`.
5. **At a milestone:** `gate --milestone` (every tier on the whole corpus with the archived maps,
   `sanitize --full --leaks`, `probe --sanitize`). **Before a release:** `gate --release` (every tool
   run again, nothing taken from the cache, the cache refreshed and required to agree).

Never revert a planted bug with `git checkout -- src` while `src/` has uncommitted work: it once
wiped a whole step's edits. Restore the exact bytes instead, and `touch` the file: a copy that keeps
the old modification time (`cp -p`) makes ninja think the objects built from the planted version are
still current.

### 4.3 The gate for a step

```sh
python3 $P gate                       # builds build/ (fails on a warning), then:
                                      #   check: quick (core set), an audit of quick (no cache), full, hlrad's
                                      #          micro-corpus (--stages rad), every csg/bsp/vis/rad variant tier
                                      #   at the same time: unit tests, regress, cli, sanitize (empty baseline)
python3 $P gate --tiers quick full    # a subset;  --skip cli sanitize ...  leaves steps out;  --fail-fast
python3 $P gate --milestone           # whole corpus + archived maps, sanitize --full --leaks, probe --sanitize
python3 $P gate --release             # every tool run again; the cache must agree
```

With the cache warm the gate takes about 11 s; a change to hlrad makes its 20 option tiers and the
lighting runs of the other tiers run again, about 3 minutes. The old sequence of commands still
works and does the same: `check` (`--no-cache` once per step, `--tier full`, `--stages rad`, tier
groups), `cli`, `regress`, `sanitize --baseline ../vhlt-parity/sanitize/baseline-v1-quick.txt`.

The warning set is `-Wall -Wextra -Wpedantic`, plus `-Wconditionally-supported` on GCC. That last
flag catches a `std::string` passed to a printf-style function such as `Log`/`Error`, which GCC
otherwise compiles silently. Clang treats that case as an error by default.

---

## 5. What has changed

`git log oracle-base..modernize` holds 62 commits. Each phase was planned, approved, and gated
before it was committed.

### 5.1 Infrastructure and `common/` (Phases 0–7)

| Commit | Step |
|---|---|
| `16867d6` Phase 0 | Parity harness, oracle build, corpus, unit test framework (doctest) |
| `2ba2e09` Phase 1 | Build hygiene in common/ |
| `47b4539` Phase 2 | BSP disk format types, bounds-checked loader, zero-padding writer |
| `22eebd6` Phase 3 | mathlib macros → constexpr templates |
| `e051709` Phase 4 | Strings, files, paths, logging |
| `2aea603` Phase 5 | Entities, tokenizer, tool parameter injection |
| `ec8a399` Phase 6 | Winding ownership, bug fixes, plane templates |
| `d423935` Phase 7 | Recursive ThreadLock, no tool `#ifdef`s in common/, common built as two object libraries |

### 5.2 Per tool

| Tool | Steps | Highlights |
|---|---|---|
| hlcsg | `H0`–`H6` | Reproducible build date; feature maps + 11 csg tiers; UB/overflow fixes (texdata buffer growth); exact hash indexes for planes/texinfo/miptex (dm_stalkfire_f1 7.4 s → 1.9 s single-threaded, same output); **deterministic multi-threading** (16 threads = `-threads 1`; 0.59 s vs oracle 6.9 s); RAII ownership (leaks 2.5 MB → 0); `Tokenizer` and table-driven command line. |
| hlbsp | `B0`–`B5` | Feature maps + 8 bsp tiers; fixed a use-after-free that made multi-hull leaks crash at random; UB fixes; tree freeing and RAII (LSan 32 MB → 0, peak RSS 72 → 36 MB); honest single-threaded `-threads`; table-driven CLI. |
| hlvis | `V0`–`V4` | Feature map + 4 vis tiers; UB fixes; RAII; **deterministic threading by default** (ordered portal scheduler: any thread count = `-threads 1`, cs_italy 20.8 s → 7.9 s at 16 threads), `-fastthreads` restores the old nondeterministic scheduling. |
| hlrad | `R0`–`R4` | Pre-baked VIS inputs + 11 fm_rad maps + 20 rad tiers; `compress.h` misaligned/aliasing UB fixed bit-exactly (shift quirks emulated, tested against an oracle-generated table); lightdata buffer growth; leaks fixed; zero raw allocations; ordered worker logging (`ThreadSetOrderedLog`, `LogInOrder`); `-waddir` path separator fixed. |
| all | `C1`–`C3` | Ownership cleanup in common/hlcsg/hlbsp, MSVC pragmas → CMake, zero warnings. |

### 5.3 Static analysis and language-level cleanup (2026-10-18)

- clang-tidy `bugprone-*` / `clang-analyzer-*` report triaged by hand (591 locations). It found
  two real defects: a signed-overflow path in hlrad `LoadTexture` (`6c75c49`) and a division by
  zero in hlvis with a 0-leaf `.prt` file (`e95c222`). `hlassume`/`Usage` were made `[[noreturn]]`,
  removing a class of false positives (`9f90416`).
- clang-tidy `modernize-*`, a curated subset, one commit each: `use-nullptr`, `use-bool-literals`,
  `raw-string-literal`, `redundant-void-arg`, `use-emplace`, `return-braced-init-list`,
  `use-using`, `use-auto`, `loop-convert`.
- Texture lump struct-hack (`dmiptexlump_t::dataofs[4]`) replaced by `MiptexLumpView`
  (`6b3fc34`, `640e54b`).
- Tier 1 (2026-10-19): the last `goto`s and leftover C macros removed; the `.void` list is a
  `vector<string>`.

### 5.4 Tier 2: strings, sorting, parsing (2026-10-19 to 2026-10-25)

| Commit | Step | Content |
|---|---|---|
| `123581d` | S1 | Every `char[_MAX_PATH]` path buffer → `std::string` (`SetMapname`, `PathWithSystemSlashes`). |
| `027b624` | flag | `-Wconditionally-supported` on GCC. |
| `55499ad` | S2 | `StringPrintf`/`StringVPrintf`; log messages no longer cut at 2047 bytes; simple text buffers → `std::string`; latent format-string bug in `Fatal`/`HlassumeFailed` fixed; Win32 `ExecutablePath` grows its buffer (the function went with `.void`, see 5.5). |
| `e4bd67f` | S3 | Char-buffer path wrappers deleted; `RemoveFile()` via `std::filesystem::remove` (never removes directories); string path helpers documented and pinned by a 21-row doctest table. |
| `ac8ee64` | S4a | 26 path-handling `cli` cases (section 4.1). |
| `e251912` | S5 | `qsort` → `std::stable_sort` (glibc's `qsort` is a stable merge sort; MSVC's isn't), hlcsg `FindTexture` → `std::equal_range`, hlrad texture lookup → `ProbeSearch` (probes exactly like glibc's `bsearch`). Output no longer depends on the C library. 3 duplicate-texture cases. |
| `a559a9d` | S6a | Number-spelling pipelines and overflow cases in `cli`. |
| `eac7eea` | S6b | All `atoi` → `ParseInt` (`strtoll` + C++20 int conversion): glibc's result, with no undefined behaviour, the same on Windows. |

### 5.4b Dead-code cleanup of hlcsg and hlvis (2026-10-25)

Found with four kinds of evidence: the linker's own list of unreferenced symbols (`--gc-sections
--print-gc-sections` on a build with function and data sections), identifiers that occur once or
are only written, gcov line coverage over every tier, and a scratch build with the candidate code
deleted. Removed (no output change): hlcsg's `Brush_LoadEntity` declaration (defined nowhere),
`WadInclude_i`, the write-only `g_nMapFileVersion` and its `mapversion` branch, the unused
`s_baseaxis` table and a stale comment; hlvis's `CalcAmbientSounds` declaration (defined nowhere),
`PORTALFILE`, the write-only `portal_t::numcansee` and its increment, and the commented-out
`visblock`/`postcompile` code. The `.vdt` dump (5.5) went with it. 43 lines in 6 files, plus the dump.

What looked dead and wasn't: `LumpLessByName`/`LumpLessByWadAndName` (the linker reports an
out-of-line copy of a function the compiler inlined), `brushhull_t`'s copy operations (`= delete`
doesn't compile: `CopyCurrentBrush` copies brushes; the corpus never does), and the code the tiers
never reach (wad autodetect, noclip entities, `gscale`, contents from texture names, the non-axial
hull expansions, hlvis's `info_compile_parameters` settings and its fast-threads branch), which is
untested rather than dead. **That list was wrong, and is superseded by 5.4c:** it came from gcov counters read
after a different tool's coverage run had deleted them (each `coverage` run starts by deleting every counter;
read them before running the next tool). Left for the owner: the `VHLT_VERIFY_INDEXES` scaffolding in hlcsg
(3 `#ifdef` blocks and `ScanMapPlanes`, the optional index verification build).

`common/` (same day), no output change. Removed: `Winding::Check` and `Winding::CopyPoints`,
`BoundingBox::testUnion` and `BASE_WINDING_DISTANCE` (nothing called them, tests included), and
`safe_strncat`, `SafeOpenRead`, `str_casecmp` and `str_ncasecmp` (only their own unit tests did;
`stringlib.cpp`/`.h` held nothing else, so they went too), with those tests. The three
commented-out `MODIFICATIONS_STRING` lines, old maintainers' bug-report contacts, moved to
`CREDITS.md`. About 200 lines in 12 files, two of them deleted. Kept on purpose: the `Winding` members that only the unit tests
use (the points constructor, `initFromPoints`, `addPoint`, `insertPoint`, `Valid`): the winding tests
build their polygons with them, and rewriting those tests is not worth the 40 lines.

hlrad (same day), no output change. About 115 lines in 5 files, in three groups.
(1) `NoteDiscardedLight`: a diagnostic that tracked the largest brightness lost to "too many light
styles on a face" (10 call sites, two statics, and a global lock taken on every new maximum). Nothing
read the result; in the oracle the report was already only a comment. (2) Computations nothing read:
`MakeParents` with the `nodeparents`/`leafparents` arrays (the loader already rejects out-of-range
node children, so it could only have failed on a cyclic node graph, by recursing forever),
`g_face_lightmode` (256 KB) with the `zhlt_lightflags` parse that filled it (the two reads of that key that
matter are separate), and `patch_t::scale` with its assignment (`getScale` stays: `getChop` uses it).
(3) Declarations defined nowhere (`MakeShadowSplits`, `intersect_line_plane`), the value-returning
overloads of `MultiplyMatrix` and `MatrixForScale` nothing called, the constants `MLH_LEFT`/`MLH_RIGHT`, the
enumerators `LightPulledInside`/`LightSimpleNudge`, and six commented-out statements. Kept: `g_lightscale`
(a constant that only feeds the "global light scale" line of the settings table, which has to stay),
`MakeScalesStub` (it does real work), `stylewarningcount`/`stylewarningnext` (a live warning throttle),
and the `//LRC` attribution markers.

How to audit for dead code in a shared library: **read the final binaries' symbol tables**, not the
linker's `--print-gc-sections` log (it left out functions that were gone), and check every
candidate's callers: the compiler inlines small functions into their callers, so the linker
reports them as unused (`LogEnd`, `EntityKeys::Set`/`Find`, `Tokenizer::EndOfStream`,
`Winding::Reset`/`resize`/`allocate` and the `Winding` move assignment were all live). Header-only
code never appears in the binaries: scan the headers for identifiers with no use.

### 5.4c Tests for the code the corpus did not reach (2026-10-25)

Coverage over every csg/bsp/vis tier, with each tool's counters read straight after its own run: hlcsg 87.1%,
hlbsp 90.4%, hlvis 90.7%. Most of the rest was error handling and diagnostics; the reachable feature code was
small, and 8 feature maps plus 1 `cli` case reach it (hlcsg 88.7%, hlbsp 91.6%, hlvis 91.5% after):

| New test | Reaches |
|---|---|
| `fm_contents` | `!lava`, `!slime` and the six `!cur_*` textures: hlcsg's contents from texture names and hlbsp's contents-to-rank mapping |
| `fm_cp_clip0`, `1`, `2`, `9` | `info_compile_parameters` `cliptype` 0, 1, 2 and an unknown value (legacy); hlvis modes 2 (normal) and 3 (full) |
| `fm_hullshape_edges` | edge-edge planes of the hull-brush expansion: a sphere and a triangular prism hull on slanted and rounded brushes |
| `fm_gametext_utf8` | `game_text` with valid 3- and 4-byte UTF-8 and with bad continuation bytes (`IsValidUTF8`) |
| `fm_small_models` | a tiny custom hull shape on a tiny brush (hlbsp collapses the hull bounds), and solid entities with nothing to draw (CLIP, NULL, SKIP, HINT: "Empty solid entity") |
| `cli hlbsp-nopln` | hlbsp without `<map>.pln`: the planes come from the `.bsp` |

Each new block was mutation-checked: a planted bug in it fails the matching map (cliptype 0 and legacy, the
edge-plane normal, current contents in hlcsg and in hlbsp, the 3-byte UTF-8 check, the hull collapse, the empty
model bounds, vis modes 2 and 3, and the `.pln` fallback). The new maps are in `CORE_MAPS` and have references for
every tier except the 20 rad tiers (`RAD_TIER_EXCLUDED_MAPS`: they test nothing about lighting, and a full-size
room costs 7-8 s of hlrad per tier). Dead code found on the way, and removed: hlcsg's `ent_gscale_b`
(initialized `false`, never assigned: five blocks in `map.cpp` that could not run), and the 4-byte branch of
`push_utf8` (its input never exceeds `0x20AC`). Not reached, left alone: error handling for malformed data (bad
texture lump, limit overflows), the "ambiguous leaf contents" warning, hull-0 bounds collapse (zero hull size:
it cannot invert), and the deep copy in `CopyFaceList` (`CopyCurrentBrush` runs, but never on a brush with faces).
`particles`, the owner's work-in-progress map in `cstrike/maps`, is excluded from the corpus (`EXCLUDED_MAPS`).

### 5.5 Intended behaviour differences from the oracle

Everything else is byte-identical. These are deliberate; the right column says how each is
covered:

| Change | Covered by |
|---|---|
| Zero padding between BSP lumps (oracle: stale memory) | `check --padding zero` |
| hlrad `-waddir` joins with the system separator (oracle used `\` everywhere, so on Linux no wad was ever found) | `regress waddir` |
| hlcsg `texdata` / hlrad `lightdata` from `info_compile_parameters` grow the buffer (oracle: heap overflow) | `regress texdata-growth`, `lightdata-growth` |
| A leak in several hulls writes every hull's trail (oracle: random crash, use-after-free) | `regress leak-all-hulls` |
| hlbsp `-threads` sets the count shown in the settings table (oracle ignored it) | `regress hlbsp-threads` |
| hlvis deterministic by default; `-fastthreads` added | `regress hlvis-fastthreads`, `cli` (usage line) |
| An out-of-range face texture index in hlrad is an error (oracle read outside the tables) | `regress rad-bad-texture` (R1; the corpus never has one, so the scenario patches every texinfo of fm_rad_texlights to texture 9999) |
| `-texdata x` (0-byte buffer) is an error (oracle overflowed it) | `regress texdata-zero` (candidate-only: the oracle's result is undefined, so it is not in `cli`) |
| hlbsp no longer reads `<map>.void` or `entities.void` (classnames allowed to lie outside the world): an entity in the void leaks as it does without the file. The corpus never had one; hlbsp's output without a .void file is unchanged. `ExecutablePath()` went with it | `regress void-ignored` (the oracle with the file does not leak; the candidate leaks with or without it) |
| hlvis `-maxdistance` no longer writes `<map>.vdt` (a dump of the visdata that no tool read; its own comment said "BUG BUG BUG") nor logs `saving visdata to ...`. The `.bsp` is unchanged | `regress vis-no-vdt` (the oracle writes the file, the candidate doesn't); the harness allows the oracle's `.vdt` (`INTENDED_MISSING_SUFFIXES`) and drops the log line (`LOG_RULES`) in every comparison |
| A worldspawn-only `.bsp` loads again (Phase 2 regression, fixed in V1) | `cli hlvis-empty` |
| hlbsp stops with an error where the oracle never finished: a texture vector of `-1e400` (error "not finite") or `-99999999999` ("Exceeded MAX_MAP_FACES" once a face needs that many splits) made `SubdivideFace` split forever | `regress bsp-texvec-hang` (the oracle times out on both) |
| Log/console messages longer than 2047 bytes are no longer cut | unit test |
| `RemoveFile` doesn't delete an empty directory named like a temp file (old `std::remove` did on POSIX) | unit test |
| Windows: `ParseInt` wraps like glibc (32-bit `long` clamped), `std::stable_sort` matches glibc order | tested on Linux only; never built or run on Windows |

---

## 6. What has been audited (and what was found)

Each tool was analysed before any change to it. Detailed notes on each audit are kept with the
corresponding commit messages and `tests/README.md`. In summary:

- **hlcsg:** the build date in every BSP; three sources of multi-thread nondeterminism (first-come
  plane/texinfo numbering, face write order); `g_texinfo[-1]`; uninitialized reads; a texdata
  overflow; unvalidated WAD headers; linear lookups at 62% of runtime.
- **hlbsp:** really single-threaded; 29 MB of leaks; a use-after-free on multi-hull leaks; UB list
  (uninitialized brink children, off-by-one checks, misaligned casts, address-keyed maps).
- **hlvis:** nondeterminism caused by scheduling-dependent pruning (and a data race), with an exact
  fix prototyped before it was implemented; UB (null-based `offsetof`, overflowing arrays,
  strict-aliasing reads).
- **hlrad:** misaligned and aliasing accesses in `compress.h`; shifts by ≥ 32 whose results had to
  be emulated as x86 computes them; a lightdata overflow; leaks; log order depending on
  scheduling; `qsort` with equal keys.
- **clang-tidy report** (section 5.3).
- **Legacy-C census** (2026-10-19, updated 2026-10-25), what remains:

  | Item | Count |
  |---|---|
  | `vec3_t` C arrays | 620 uses (`Vector*` helpers are already constexpr templates) |
  | Mutable globals | about 215 |
  | printf-style log calls | about 760 |
  | `atof` / `sscanf` | 22 / 30 |
  | `safe_snprintf` / `safe_strncpy` | 16 |
  | Fixed `char` arrays | 30, mostly on-disk formats: 16-byte texture names, WAD headers |
  | `calloc` | 1, on purpose: lazily zeroed BSP buffers |

- **`std::filesystem::path` for path parsing (2026-10-19/25):** rejected. On Linux it doesn't
  split at `\`. GoldSrc paths in `.map` files and `"wad"` keys use Windows rules on every
  platform, and the file names derived from them are written into the BSP. A compiled
  side-by-side table and the S4a cases show the differences, including `C:x.wad`: the BSP keeps
  it as written, while `fs::path` on Windows would split off the drive.
- **Number parsing (2026-10-25):** no `setlocale` anywhere, so `atof` is always in the C locale.
  `from_chars` differs from `atoi`/`atof` on blanks, `+`, hex floats, overflow and invalid input.
  glibc's `sscanf("%lf")` fails on `0x` and `3e`, where `atof` reads 0 and 3.
- **Unreachable compat cuts:** the `wadpath.cpp` cut at `_MAX_PATH - 1` and hlrad's `MAX_VAL - 1`
  cut can't trigger on Linux (`_MAX_PATH` = 4096, and the tokenizer rejects a 4095-character
  value first). They only matter on Windows.

---

## 7. Decided against, with the reasons

Don't reopen these without new information:

| Idea | Why not |
|---|---|
| Path helpers on `std::filesystem::path`, plus normalizing `\` → `/` where paths come in (S4b–S4d) | Needs about 6 compat special cases (`.map`, trailing slash, trailing separator, drive-relative names on Windows), an untestable Windows branch that decides BSP content, extra state for log spelling, and normalizing `-wadinclude` and the drive-letter fallbacks too. No behaviour gain on Linux. `std::filesystem` is used for disk operations (`RemoveFile`). |
| `std::from_chars` for `atof`/`sscanf` (full S6b) | Needs reimplementing `strtod`/`strtol`/`scanf` quirks to stay identical; locale independence is moot. Only `atoi`'s undefined behaviour was worth fixing (`ParseInt`). |
| `std::sort` instead of `std::stable_sort` | Changes output (proved by mutation). |
| hlbsp graph nodes (tree, portals, surfaces, brushes, brink tree) as individually owned objects | Shared membership (a portal belongs to two nodes) and arena-like peak memory; they stay behind one allocate and one free function per type. |
| clang-tidy `use-default-member-init` | Would move log.cpp's `thread_local` capture into log.h. |
| `using x_t = struct x_s {...}` for the 49 `typedef struct` definitions | Worse to read; kept as typedefs. |
| clang-tidy `trailing-return-type`, `avoid-c-arrays`, `nodiscard`, `integer-sign-comparison`, `use-ranges` | Churn without value; `integer-sign-comparison` would undo compat casts. |
| `use-std-numbers` | 8 sites, all bit-identical, no value; the trap is `log2`/`pi_v<float>`. |

---

## 8. Not implemented yet (planned or proposed)

| Item | Status |
|---|---|
| `std::format` for the ~760 log calls | Proposed, **not decided**. I recommended skipping it: a lot of churn, and `std::format` doesn't format floats exactly like `printf` (`%g` especially); type safety is already covered by `FORMAT_PRINTF` plus `-Wconditionally-supported`. |
| Tier 3: a `Vec3` value type replacing `vec3_t` C arrays | Proposed as the only Tier 3 item worth considering; **not designed**. It's the biggest remaining job (620 uses), with float-evaluation risks: evaluation order and precision must stay identical. Would need a design and a test step first. |
| Verify the Windows/MSVC build | **Not done** since the modernization started. Several fixes target Windows behaviour (`ParseInt`, `stable_sort`, the `_MAX_PATH` cut) but were only compiled on Linux. |

## 9. Possible future work, not designed or planned

These came up during the work but have no design, estimate or decision. Treat them as starting
points:

- **CI** (for example GitHub Actions) running ctest and a small parity subset. The oracle and
  corpus live outside the repo, so this needs a data story (cached oracle build, licensing of the
  stock maps and WADs).
- **A Windows parity oracle** (oracle built with MSVC) to check that Windows output matches Linux,
  which S5/S6b now aim for.
- **Better wad lookup on Linux:** treating `\` in `"wad"` keys as a separator would find wads that
  are never found today. That would be a deliberate feature with new expected results, not a
  refactor (section 7).
- **hlrad performance:** about 74% of total pipeline time. hlvis `-fastthreads` shows the room
  between deterministic and nondeterministic scheduling; a faster deterministic scheduler is
  unexplored.
- **`lights.rad` support** in hlrad: absent in this tree.
- **Reducing mutable globals** (about 215). Judged not worth it so far; would only make sense
  together with a larger restructuring, for example making the tools usable as a library.
- **Remaining `sscanf` sites** could become explicit parsers that reproduce glibc's quirks, if a
  reason appears (S6a would catch mistakes).

---

## 10. Notes for agents

- Read `tests/README.md` before changing the harness. Never regenerate oracle references to make
  a check pass: references are produced by the oracle only.
- Before touching untested behaviour, add tests and mutation-check them (section 4.2).
- Prefer keeping odd behaviour with a `// compat:` comment over silently "fixing" it. A real
  behaviour change needs a test that pins the new behaviour, and a row in section 5.5.
- Parity needs `-threads 1` and the pinned build date. The harness reconfigures `build/` when it
  doesn't have the oracle's date (and Release); it did not have either until 2026-10-25, which is
  why the unit tests took a minute.
- `tests/parity/parity.py --help` lists every command. `check --stages csg` only reruns hlcsg
  when the downstream binaries are unchanged.
- Keep this file up to date when a step lands: sections 5, 8 and the state line at the top.
