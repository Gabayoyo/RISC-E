# RISC-E

RISC-**Experiment** is a C++ prototyper for RISC-V (RV32I) hardware logic: model components such as branch prediction, pipelines, and memory at the C level, then run them on a simulated interpreter with real RV32I programs. Competing designs can be compared head-to-head on the same benchmark programs.

## Prerequisites

- A C++20 compiler (Clang or GCC) and CMake 3.10 or newer.
- A RISC-V GCC cross-compiler (`riscv64-unknown-elf-gcc` or `riscv64-elf-gcc`)
  only if you want to run `.c`/`.S` sources directly or run the integration
  tests. ELF files run without it. Override the compiler with `RISCV_GCC`.

## Quickstart

```sh
cmake -S . -B out/build/preset -DCMAKE_BUILD_TYPE=Debug
cmake --build out/build/preset
cd out && ./build/preset/risc-e
```

The first run loads `../files/output/sample.elf`, prints a branch-prediction
and pipeline report, then the program's exit code.

> [!NOTE]
> The Quickstart needs no cross-compiler. A RISC-V GCC on `PATH` is only
> required to compile `.c`/`.S` inputs and to build the integration tests.

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
| `--param <p>.<k>=<v>` | e.g. `gshare.history-bits=14` | Set a predictor tunable; repeatable. |
| `--pipeline-stages <N>` | integer ≥ 1 (default `5`) | Pipeline depth; the derived penalty grows with depth. |
| `--mispredict-penalty <N>` | non-negative integer | Override the per-miss penalty directly. |
| `--comparison [name]` | optional predictor name | Compare predictors over one recorded trace; a name restricts the table to that predictor. |
| `--list-predictors [name]` | optional predictor name | List predictors and parameters; append a name for detail. |

```sh
cd out && ./build/preset/risc-e path/to/program.elf
cd out && ./build/preset/risc-e --predictor gshare path/to/program.elf
cd out && ./build/preset/risc-e --list-predictors
```

```
two-bit [table-size=1024, ras-depth=16]
always-not-taken
gshare [history-bits=12, ras-depth=16]
tournament [history-bits=10, ras-depth=16]
ras [ras-depth=16]
```

`--comparison` executes the program once and then runs every available
predictor over the recorded control-flow trace, printing a side-by-side
comparison of hit rate **and** total cycles under the configured pipeline.
Pass a predictor name to restrict the table to that one:

```sh
cd out && ./build/preset/risc-e --comparison path/to/program.elf
cd out && ./build/preset/risc-e --comparison gshare path/to/program.elf
```

```
comparison (7 branches, 5-stage pipeline (2-cycle mispredict penalty)):
  predictor         hits      hit rate    cycles
  two-bit           6/7       85.71%      18
  always-not-taken  2/7       28.57%      26
  gshare            6/7       85.71%      18
  tournament        6/7       85.71%      18
  ras               3/7       42.86%      24
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

The report shows ideal vs actual cycles, CPI, the slowdown against a perfect
predictor, and **cycles saved** versus doing nothing (`always-not-taken`).

## Adding a predictor

Predictors implement the `BranchPredictor` interface
(`include/risc-e/cpu/branch_predictor.hpp`) with three methods: `predict()`
returns the predicted next PC, `resolve()` feeds the actual outcome back for
learning, and `name()` identifies the predictor on the CLI. The context passed
in tells you whether the instruction is a conditional branch, call, or return,
so a predictor can keep its own BTB, return-address stack, or direction tables.

A new predictor is one file pair under `include/risc-e/cpu/predictors/` and
`src/cpu/predictors/`: declare its tunables with `parameters()` /
`set_parameter()`, then register it in `make_predictor()`. It automatically
appears in `--list-predictors` and `--comparison`.

## Layout

```
include/risc-e/   public headers
  cpu/                CPU state, traps, branch stats, the predictor
                      interface and pipeline model, predictors/
  decoder/            instruction decoding and opcode constants
  elf/                ELF loading
  interpreter/        the interpreter
  memory/             memory interface and physical memory
  report/             report sections
src/                implementation, mirroring include/risc-e/
  main.cpp            CLI frontend
tests/              unit tests and RISC-V test programs
tests/programs/
  src/                RISC-V assembly and C sources
  elf/                built ELF outputs (generated, not tracked)
files/
  output/sample.elf   default program loaded when no argument is given
CMakeLists.txt        build and test registration
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
