# VHLT v34 (modernized)

The Vluzacn's Half-Life compile tools (hlcsg, hlbsp, hlvis, hlrad) for GoldSrc BSP v30 maps,
rewritten in modern C++20.

The modernized tools write the same `.bsp` files as the original v34 tools, byte for byte. The
only exception is the padding between lumps: the original left stale memory there, and the
modernized tools write zeros. The test harness (`tests/`) compares every stage against the
original tools built with the same compiler. The last run of every option tier on the whole corpus
passed on 55 maps (46 tiers). Every change since then has passed the routine gates: the core map
set, the whole corpus at the quick tier, 255 command lines (option parsing, path handling, number
spellings) and 6 regression scenarios.

`MODERNIZATION.md` has the full context for contributors: what changed, what was audited, how the
tests keep the output identical, and what is still open.

## What changed

### C++20 and RAII

- All of common/ and all four tools build as C++20 with CMake.
- Every allocation has an owner: `std::vector`, `std::unique_ptr`, or a store that owns a set of
  objects. There is no stray `new`/`delete`.
  - `malloc`/`free` remain in two places only: hlbsp's `.void` loader, kept as it was, and the BSP
    data buffers, which use `calloc` for lazily zeroed pages and are owned by a `unique_ptr`.
  - hlbsp's BSP tree, portals, surfaces and brushes, and its brink tree, are linked structures
    that several lists share (a portal belongs to two nodes). They are created and destroyed only
    through one allocation function and one free function per type.
- Fixed:
  - undefined behaviour: misaligned and type-punned accesses, out-of-range shifts, signed
    overflows, uninitialized reads;
  - buffer overflows;
  - unchecked reads of BSP lumps and textures.

  Where the original's result depended on that undefined behaviour, the modernized code reproduces
  what the original actually did, marked `// compat:` in the source.
- Command lines are parsed from option tables (`common/cmdline.h`), with the original messages
  and quirks.

### Deterministic multi-threading

The output of any `-threads` count is identical to `-threads 1`: the `.bsp` and the log.

- **hlcsg**: brushes are processed in parallel and written in brush order.
- **hlbsp**: single-threaded. `-threads` now sets the count shown in the settings table.
- **hlvis**: portals are processed in single-threaded order. A flow waits for the earlier portals
  it uses.
  - `-fastthreads` turns on the original scheduling. It is faster with many threads, but its
    result can differ from run to run.
- **hlrad**: its lighting never depended on the thread count, and now its log does not either.
  - `ThreadSetOrderedLog` (`common/threads.h`) captures each work item's output and writes it in
    work order.
  - `LogInOrder` applies log output that depends on shared state (a warning counter,
    `PrintOnce`) in that same order.
  - An error in a work item ends the run after the output of the items before it, as a
    single-threaded run would.

### Fixes that change behaviour

- **hlrad `-waddir`**: the directory and the wad name are joined with the system's path
  separator. It used a backslash on every system, so on Linux no wad was ever found through
  `-waddir`.
- **hlrad `lightdata`**: an `info_compile_parameters` `lightdata` key larger than the buffer
  grows the buffer. It used to write past the buffer's end. hlcsg handles `texdata` the same way.
- **hlrad bad textures**: a face whose texture index is out of range is an error. It used to read
  outside the texture tables.
- **hlbsp leaks**: a map that leaks in several hulls writes every hull's leak trail. It used to
  crash in some runs.

### Quality

- **Warnings**: common/ and all four tools build with no warnings under GCC (`-Wall -Wextra
  -Wpedantic`).
- **Memory errors and leaks**: AddressSanitizer, UndefinedBehaviorSanitizer and LeakSanitizer
  find nothing across the test tiers, including all 20 hlrad option tiers.
- **Data races**: ThreadSanitizer finds none in hlcsg, hlvis or hlrad at 16 threads.
- **Unit tests**: built with doctest, in `tests/unit`.

## Build

```bat
git clone https://github.com/G2Pavon/VHLT-V34
cd VHLT-V34
```

```bat
mkdir build
cd build
```

Windows
```bat
cmake -S .. -B . -G "Visual Studio 18 2026" -A x64
cmake --build . --config Release
```

Linux
```bat
cmake -S .. -B . -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

The modernization was developed and verified on Linux with GCC. The Windows build has not been
tested since.

For a reproducible build, set `-DVHLT_BUILD_DATE="Mmm dd yyyy"`. The build date is written into the
banner and into every `.bsp` (the worldspawn `compiler` key).

## Tests

```sh
ctest --test-dir build --output-on-failure                    # unit tests
python3 tests/parity/parity.py check --bin build/bin          # parity with the original tools
```

The parity harness needs the original tools built from the `oracle-base` tag (`07575e9`) and the
test corpus.
`tests/README.md` covers its setup, the map sets, the option tiers, the sanitizer runs and the
other commands.
