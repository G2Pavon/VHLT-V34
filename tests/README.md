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
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_FLAGS=-ffp-contract=off -fno-fast-math"
cmake --build build
ctest --test-dir build --output-on-failure
```

`common/` is compiled once per tool, so the tests are built twice: `common_tests_f64`
(as hlcsg sees it: `HLCSG`, `DOUBLEVEC_T`) and `common_tests_f32` (as hlrad sees it: `HLRAD`).
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
python3 $P corpus                # 18 RMFs via MESS + cstrike/maps/*.map, canonical WAD lists, placeholders
python3 $P ref --tier quick      # oracle outputs (vis/rad -fast), ~1 min
python3 $P ref --tier full       # oracle outputs (default vis/rad), slow
```

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
- `--padding`: the 0–3 bytes after each lump. The oracle leaks stale buffer bytes there; the
  modernized writer zero-fills. Default `zero-or-equal` accepts either; switch to `zero` once the
  new writer lands.

All tools run with `-threads 1`. Multi-threaded hlcsg is not deterministic even between two runs
of the same binary (plane numbering depends on scheduling), so it cannot be compared byte for byte.

### Other commands

```sh
python3 $P bench --bin DIR --repeat 3 [--maps cs_assault ...] [--threads N] [--baseline ../vhlt-parity/bench/oracle-quick-t1.json]
python3 $P sanitize --baseline ../vhlt-parity/sanitize/baseline-oracle-quick.txt   # ASan+UBSan (clang++) build of the tree; fails on new findings
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

No ASan findings. UBSan reports four known issues, all left untouched in Phase 0:

| Finding | Tools | Planned fix |
|---|---|---|
| `common/bspfile.cpp:204` `vecs[0][j]` with `j < 8` in the no-op `SwapBSPFile` | all | Phase 1 (delete `SwapBSPFile`) |
| `hlcsg/hlcsg.cpp:298` `g_texinfo[-1]` | hlcsg | hlcsg phase |
| `hlbsp/brink.cpp:1131` pointer arithmetic overflow | hlbsp | hlbsp phase |
| `hlvis/hlvis.cpp:123` member access through a null `winding_t*` | hlvis | hlvis phase |

### Environment

| Variable | Default |
|---|---|
| `VHLT_PARITY_DATA` | `<repo>/../vhlt-parity` |
| `VHLT_GAME_DIR` | `~/.steam/steam/steamapps/common/Half-Life` |
| `VHLT_MESS` | `<repo>/../mess_1.2.3_linux64/MESS` |
| `VHLT_RMF_ROOT` | `<repo>/../GoldSrc-map-sources-restored` |
