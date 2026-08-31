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
- **Pipeline model** — turns mispredictions into cycle cost with a configurable
  depth and penalty.
- **Components** — predictors and the pipeline model plug into one harness
  interface: tunables (`--param`), a report section, and within-type
  comparison. Memory and other component types slot in the same way.

> [!NOTE]
> Not yet implemented: M extension, Zicsr (CSRs), compressed instructions
> (RVC), IR lifting, and JIT code generation.

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

Every run prints two report sections — **branch prediction** (predictor name,
hit/miss rate and counts, branches scored) and **pipeline** (the cycle cost of
the run under a configurable pipeline model):

```
branch prediction
  predictor: two-bit
  hit rate: 85.7143%
  miss rate: 14.2857%
  hits: 6
  misses: 1
  branches: 7

pipeline
  model: 5-stage pipeline (2-cycle mispredict penalty)
  instructions: 16
  ideal cycles: 16
  penalty cycles: 2 (1 miss x 2 cycles)
  total cycles: 18
  CPI: 1.125
  slowdown: +12.50% vs perfect
  cycles saved: 8 vs always-not-taken

exit code: 7
```

### CLI reference

| Flag | Value | Effect |
| --- | --- | --- |
| `--predictor <name>` | `two-bit` (default), `always-not-taken`, `gshare`, `tournament`, `ras` | Select the branch predictor. |
| `--param <c>.<k>=<v>` | e.g. `gshare.history-bits=14` | Set a component tunable; repeatable. |
| `--pipeline-stages <N>` | integer ≥ 1 (default `5`) | Pipeline depth; the derived penalty grows with depth. |
| `--mispredict-penalty <N>` | non-negative integer | Override the per-miss penalty directly. |
| `--comparison [name]` | optional component name | Compare components of one type over the recorded run; a name restricts the table to that component. |
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
  pipeline [stages=5, mispredict-penalty=0]
```

`--comparison` executes the program once and then runs every predictor over
the recorded control-flow trace, printing a side-by-side comparison of hit
rate **and** total cycles under the configured pipeline. Pass a component name
to restrict the table to that one:

```sh
cd out && ./build/preset/risc-e --comparison path/to/program.elf
cd out && ./build/preset/risc-e --comparison gshare path/to/program.elf
```

```
comparison (7 events, 5-stage pipeline (2-cycle mispredict penalty)):
  component         hits        hit rate    cycles
  two-bit           6/7         85.71%      18
  always-not-taken  2/7         28.57%      26
  gshare            6/7         85.71%      18
  tournament        6/7         85.71%      18
  ras               3/7         42.86%      24
```

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

The pipeline report turns mispredictions into cycle cost. The default model
is a classic 5-stage in-order pipeline, where each mispredicted branch costs
2 cycles; both depth and penalty are configurable:

- `--pipeline-stages <N>` — pipeline depth (default `5`); deeper pipelines cost
  more per miss.
- `--mispredict-penalty <N>` — set the per-miss cost directly.

Both are also settable through the generic component path, e.g.
`--param pipeline.stages=10`.

The report shows ideal vs actual cycles, CPI, the slowdown against a perfect
predictor, and **cycles saved** versus doing nothing (`always-not-taken`).

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

If you want to add a completely new overrideable component (e.g. PGO, memory model), you would have to extend `Component` directly and implement the hooks:

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

    std::vector<Metric> metrics(const RunContext& ctx) override {
        // named numbers for the --comparison table
        return {{"hit rate", 85.71, std::nullopt, "%"}};
    }
};
```

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
                      interface and pipeline model, predictors/
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
