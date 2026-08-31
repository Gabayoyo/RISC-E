# RISC-E

RISC-**Experiment** is a C++ prototyper for RISC-V (RV32I) hardware logic: model components such as branch prediction, pipelines, and memory at the C level, then run them on a simulated interpreter with real RV32I programs. Competing designs can be compared head-to-head on the same benchmark programs.

## Prerequisites

- A C++20 compiler (Clang or GCC) and CMake 3.10 or newer.
- A RISC-V GCC cross-compiler (`riscv64-unknown-elf-gcc` or `riscv64-elf-gcc`)
  to build the sample programs and integration tests, and to run `.c`/`.S`
  sources directly. Prebuilt ELF files run without it. Override the compiler
  with `RISCV_GCC`.

## Quickstart

```sh
cmake -S . -B out/build/preset -DCMAKE_BUILD_TYPE=Debug
cmake --build out/build/preset
cd out && ./build/preset/risc-e build/preset/tests/programs/elf/branches.elf
```

The run prints a branch-prediction and pipeline report, then the program's
exit code.

> [!NOTE]
> The sample programs are compiled at build time, so the Quickstart needs a
> RISC-V GCC cross-compiler on `PATH`. Prebuilt ELF files run without it.

## Features

- **ELF loading** — ELF32 RISC-V `ET_EXEC` images.
- **RV32I ISA** — LUI, AUIPC, JAL, JALR, branches, loads/stores, integer
  arithmetic and logical ops, ECALL, EBREAK, FENCE.
- **Syscall emulation** — `write` (64), `exit` (93), `brk` (214).
- **Memory model** — page-based memory with on-demand allocation; unmapped
  access and misalignment raise traps instead of crashing.
- **Branch prediction** — pluggable predictors behind one interface, each with
  its own tunables, plus a trace replay for side-by-side comparison. Built-ins:
  `two-bit` (default), `always-not-taken`, `gshare`, `tournament`, `ras`.
- **Pipeline model** — turns stall events into cycle cost with a configurable
  depth and penalty; branch mispredictions are today's stall source, and the
  report stays source-agnostic (no predictor names, worst-case baseline).
- **Dynamic profiling** — identifies basic blocks at run time (interned IDs
  keyed by entry PC, with execution and instruction counts) plus the static
  distinct-instruction footprint. A simulated instruction cache then prices
  the run's fetches — hits vs. misses with LRU eviction — and reports cycles
  saved. Reported as its own section and comparable via
  `--comparison icache`.
- **Components** — predictors and the pipeline model plug into one harness
  interface: tunables (`--param`), a report section, and within-type
  comparison. Memory and other component types slot in the same way.

## Build

```sh
cmake -S . -B out/build/preset -DCMAKE_BUILD_TYPE=Debug
cmake --build out/build/preset
```

A portable `ci` preset (default toolchain) and a `sanitize` preset
(ASan + UBSan) are also available; the CI workflow builds and tests both on
every push:

```sh
cmake -S . -B out/build/ci --preset ci
cmake -S . -B out/build/sanitize --preset sanitize
```

## Run

```sh
cd out && ./build/preset/risc-e [path-to.elf]
```

Without an argument the interpreter loads `../files/output/sample.elf`.
The program's exit status is printed, and the process exits with the same code.

Every run prints three report sections — **branch prediction** (predictor name,
hit/miss rate and counts, branches scored), **pipeline** (the cycle cost of the
run under a configurable pipeline model), and **profile** (block and
instruction-cache statistics):

```
branch prediction
  predictor: two-bit
  hit rate: 85.7143%
  miss rate: 14.2857%
  hits: 6
  misses: 1
  branches: 7

pipeline
  model: 5-stage pipeline (2-cycle stall penalty)
  instructions: 16
  ideal cycles: 16
  stall cycles: 2 (1 stall event x 2 cycles)
  total cycles: 18
  CPI: 1.125
  slowdown: +12.50% vs perfect
  cycles saved: 12 vs worst-case (7 stall events)

profile
  instructions executed: 16
  distinct instructions: 8
  basic blocks: 5
  instruction cache (miss penalty 50, cache 64 instrs):
    hits: 3 (37.50%)
    misses: 5
    compulsory misses: 5
    evictions: 0
    miss stalls: 250 (5 x 50 cycles)
    total cycles: 266
    no-cache baseline: 416
    cycles saved: 150 (36.06%)

exit code: 7
```

Blocks are identified dynamically: a new block starts wherever a control
transfer lands (program entry, branch/jump targets, not-taken fall-through).
A block first reached by fall-through that later becomes a jump target is
split on its first back-edge, so that first trip counts toward the
predecessor block. The instruction-cache section is a microarchitectural
model: blocks are the cache lines, fetched on first touch (compulsory miss),
hitting while resident, and missing again after LRU eviction when the cache
(`cache-capacity` instructions, `0` = unlimited) is too small. Each miss
stalls the fetch stage for `miss-penalty` cycles, and cycles saved is
measured against a machine with no instruction cache at all.

### CLI reference

| Flag | Value | Effect |
| --- | --- | --- |
| `--predictor <name>` | `two-bit` (default), `always-not-taken`, `gshare`, `tournament`, `ras` | Select the branch predictor. |
| `--param <c>.<k>=<v>` | e.g. `gshare.history-bits=14` | Set a component tunable; repeatable. |
| `--pipeline-stages <N>` | integer ≥ 1 (default `5`) | Pipeline depth; the derived penalty grows with depth. |
| `--stall-penalty <N>` | non-negative integer | Override the per-stall-event penalty directly. |
| `--comparison <name>` | required component or type name | Compare components of one type over the recorded run; pass a component to restrict the table to it, or a type (e.g. `predictor`) for the whole family. Never defaults. |
| `--list [name]` | optional component or type name | List components grouped by type; append a name for detail (`--list-predictors` is an alias). |

```sh
cd out && ./build/preset/risc-e path/to/program.elf
cd out && ./build/preset/risc-e --predictor gshare path/to/program.elf
cd out && ./build/preset/risc-e --list
```

```
predictor:
  two-bit [table-size=1024, ras-depth=16]
  always-not-taken
  gshare [history-bits=12, ras-depth=16]
  tournament [history-bits=10, ras-depth=16]
  ras [ras-depth=16]
pipeline:
  pipeline [stages=5, stall-penalty=0]
profile:
  icache [miss-penalty=50, cache-capacity=64]
```

`--comparison` requires a component or type name (it never defaults to a
type). It executes the program once and then runs every component of the type
over the recorded control-flow trace. Every comparison is the same three
columns — **cycles before** (the type's reference baseline), **cycles after**
(this design), and **speedup** (before / after):

```sh
cd out && ./build/preset/risc-e --comparison predictor path/to/program.elf
cd out && ./build/preset/risc-e --comparison gshare path/to/program.elf
cd out && ./build/preset/risc-e --comparison icache path/to/program.elf
```

```
comparison (7 events, 5-stage pipeline (2-cycle stall penalty); speedup vs no prediction):
  component         cycles before  cycles after   speedup
  two-bit           26             18             1.44x
  always-not-taken  26             26             1.00x
  gshare            26             18             1.44x
  tournament        26             18             1.44x
  ras               26             24             1.08x
```

The speedup baseline is a property of each type's cost model (`no prediction`
for predictors, `no instruction cache` for the profile, `stall-free` for the
pipeline), so the columns are comparable within a type but never across
types. This is comparison-only: the per-run `report()` sections still print
the full stats for every component.

You can also pass a source file directly — `.S`, `.s` or `.c` — and the tool
compiles it on the fly with a RISC-V cross-compiler from `PATH`
(`riscv64-unknown-elf-gcc` or `riscv64-elf-gcc`, overridable with `RISCV_GCC`):

```sh
cd out && ./build/preset/risc-e ../tests/programs/src/branches.S
cd out && ./build/preset/risc-e ../tests/programs/src/branchy.c
```

C programs run freestanding (no libc): `main()`'s return value becomes the
exit code, and a small runtime header provides inline-asm wrappers for the
emulated syscalls (`write`, `exit`, `brk`). `printf` and `malloc` are not
available.

### Cycle model

The pipeline report turns stall events into cycle cost. The default model is a
classic 5-stage in-order pipeline, where each stalled control transfer costs
2 cycles (a mispredicted branch is the stall source today, but the model and
report never name one); both depth and penalty are configurable:

- `--pipeline-stages <N>` — pipeline depth (default `5`); deeper pipelines cost
  more per stall event.
- `--stall-penalty <N>` — set the per-stall-event cost directly.

Both are also settable through the generic component path, e.g.
`--param pipeline.stages=10` or `--param pipeline.stall-penalty=4`.

The report shows ideal vs actual cycles, CPI, the slowdown against a
stall-free pipeline, and **cycles saved** versus a worst-case pipeline that
pays the penalty on every control transfer — a neutral baseline that depends
only on the run, not on any predictor or policy.

### Instruction cache

The profile section simulates a small instruction cache over the run's block
entries: blocks are the cache lines, fetched on first touch (compulsory
miss), hitting while resident, and missing again after eviction. A miss
stalls the fetch stage for `miss-penalty` cycles (default 50); the cache
holds `cache-capacity` instructions (default 64, `0` = unlimited). When a new
block does not fit, the least-recently-used block is evicted, so a block
re-entered after eviction misses again — a capacity miss. **Cycles saved** is
measured against a machine with no instruction cache, where every entry
misses.

The numbers are tunable rather than measured — a smaller cache forces
eviction, and the miss penalty scales the cost:

```sh
cd out && ./build/preset/risc-e --param icache.cache-capacity=8 path/to/program.elf
cd out && ./build/preset/risc-e --param icache.miss-penalty=100 path/to/program.elf
```

On `branch_loops.c` the default model reports a 95.65% hit rate and 80.62%
cycles saved: the two hot loops stay resident, so only the nine compulsory
misses stall.

`--comparison icache` compares on the same three columns as every other
type: cycles before (no instruction cache), cycles after, and speedup.

## Adding a component

Anything the harness manages — predictors, the pipeline, memory — is a
`Component`. The registry is two-level: types are declared once, and
implementations register into a type.

### Extending an existing definition

Most commonly, you will be adding a new definition to an existing `Component`. For example: a custom branch predictor extends `BranchPredictor`, allowing you to implement custom branch predictor logic:

```cpp
class MyPredictor : public BranchPredictor {
public:
    std::string_view name() const override { return "my-predictor"; }

    Prediction predict(const BranchContext& ctx) const override {
        // predict the next PC for a control transfer
    }
    void resolve(const BranchContext& ctx, const Resolution& res) override {
        // learn from the actual outcome
    }

    std::vector<ParamSpec> parameters() const override { /* tunables */ }
    bool set_parameter(std::string_view name, std::string_view value,
                       std::string& error) override { /* validate + apply */ }
};
```

The harness features — tunables, the report section, and comparison — come
automatically. Registration is one call in `src/harness/registry.cpp`:

```cpp
register_component<BranchPredictor, MyPredictor>(
    "predictor", MyPredictor::kName,
    []() -> std::unique_ptr<Component> { return std::make_unique<MyPredictor>(); });
```

The type's base class (`BranchPredictor`) is part of the registration, so a
`MyPredictor` that does not extend it fails to compile.

### Adding a new type

If you want to add a completely new overrideable component (e.g. PGO, memory model), you would have to extend `Component` directly and implement the hooks. The `icache` profile component is a live example of a type with a single implementation: `ProfileComponent` extends `Component`, records nothing itself, and only renders the stats the interpreter collected during the run (see `include/risc-e/cpu/profile.hpp`):

```cpp
class MyMemoryBase : public Component {
public:
    std::string_view name() const override = 0;
    std::string_view type() const override { return "memory"; }

    std::vector<ParamSpec> parameters() const override { /* tunables */ }
    bool set_parameter(std::string_view name, std::string_view value,
                       std::string& error) override { /* validate + apply */ }

    std::string_view report_title() const override { return "memory"; }
    void report(std::ostream& out, const RunContext& ctx) const override {
        // one output section for every run
    }
};
```

A component that models time also answers the comparison table by overriding
`cycle_cost` — the run's cycles under this design, the reference design's
cycles, and a short name for that baseline. `--comparison` then prints the
same three columns for every row: **cycles before** (the reference), **cycles
after** (this design), and **speedup** (before / after):

```cpp
    std::optional<CycleCost> cycle_cost(const RunContext& ctx) override {
        // this design's cycles, and the reference design it is compared to
        return CycleCost{total_cycles, baseline_cycles, "no memory cache"};
    }
```

Every cost-modeling type picks one reference baseline, so the columns stay
comparable within a type; components that do not model time leave the hook
defaulted and are skipped by `--comparison`. The `report()` section, by
contrast, is free-form — every run prints the full stats, independent of the
comparison table.

Then declare the type and register implementations into it:

```cpp
register_type("memory");

register_component<MyMemoryBase, MyMemory>(
    "memory", MyMemory::kName,
    []() -> std::unique_ptr<Component> { return std::make_unique<MyMemory>(); });
```

Implementations of the new type are added as described above. Registration is
what makes a class swappable: types and components that are never registered
are invisible to `--param`, `--list`, and `--comparison`, and
`register_component` enforces at compile time that an implementation extends
its type's base class (and `Component`).

## Layout

```
include/risc-e/   public headers
  cpu/                CPU state, traps, branch stats, the predictor
                      interface, pipeline model, dynamic profile, predictors/
  decoder/            instruction decoding and opcode constants
  elf/                ELF loading
  interpreter/        the interpreter
  memory/             memory interface and physical memory
  harness/            the component interface, registry, and run context
src/                implementation, mirroring include/risc-e/
  main.cpp            CLI frontend
tests/              unit tests and RISC-V test programs
  CMakeLists.txt      all test registration
tests/programs/
  src/                RISC-V assembly and C sources
  elf/                built ELF outputs (generated, not tracked)
files/
  output/sample.elf   default program loaded when no argument is given
CMakeLists.txt        core build (tests live in tests/CMakeLists.txt)
CMakePresets.json     `preset`, `ci`, and `sanitize` presets
.github/workflows/ci.yml   CI build + test
```

Most public headers under `include/risc-e/` have a matching implementation in
`src/`.

## Test

With a RISC-V cross-compiler on `PATH`, the build also compiles the programs
under `tests/programs/src/` and registers integration tests that check printed
output, exit codes, and branch stats.

```sh
cd out && ctest --test-dir build/preset --output-on-failure
```

Tests cover ELF loading, memory faults, misaligned access, exit codes, and
branch prediction. A GitHub Actions workflow builds and runs the whole suite
on every push, with the default toolchain and under ASan + UBSan.
