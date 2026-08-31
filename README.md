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

Assembly and C sources work too: if the argument ends in `.S`, `.s` or `.c`,
the tool compiles it on the fly with a RISC-V cross-compiler found on `PATH`
(`riscv64-unknown-elf-gcc` or `riscv64-elf-gcc`, overridable via the
`RISCV_GCC` environment variable) and runs the result:

```sh
cd out && ./build/preset/risc-e ../tests/programs/src/branches.S
cd out && ./build/preset/risc-e ../tests/programs/src/branchy.c
```

C inputs are compiled freestanding (`-ffreestanding`, no libc): the tool
links them against a bundled `_start` crt0 that calls `main()` and exits with
its return value, so a C program's exit code is `main`'s. A small runtime
header, `risc-e.h`, is provided automatically (the compile passes `-I` to
its temp directory); include it to get inline-asm syscall wrappers for the
three emulated syscalls — `risc_e_write` (64), `risc_e_exit` (93) and
`risc_e_brk` (214). Division and modulo work through libgcc (`-lgcc` is
appended after the input files), but hosted-libc functions such as `printf`
or `malloc` are not available.

Predictions are target-aware: a control transfer (conditional branch, JAL,
JALR) is a hit when the predicted next PC matches the actual next PC. Direct
jumps (JAL) are trivially predictable and usually count as hits.

### Cycle model

The pipeline section converts mispredictions into cycle cost, not just counts.
The `PipelineModel` (`include/risc-e/cpu/pipeline.hpp`) assumes a classic
in-order pipeline: a branch resolves in the EX stage (stage 3), so a mispredict
flushes the two younger fetched stages. The default 5-stage pipeline therefore
costs **2 cycles per miss** (`penalty = stages - 3`). The model is
parameterised:

- `--pipeline-stages <N>` — pipeline depth (default `5`); the derived penalty
  grows with depth (`--pipeline-stages 10` → 7-cycle penalty).
- `--mispredict-penalty <N>` — override the per-miss penalty directly, for
  designs that resolve branches earlier/later or hide some of the cost with a
  BTB.

The report shows `ideal cycles` (a perfect predictor, 1 IPC), `penalty cycles`
(misses × penalty), `total cycles`, `CPI`, and `slowdown` vs the perfect
baseline. It also replays the recorded trace through an `always-not-taken`
predictor to report **cycles saved** — the real gain of the chosen predictor
versus doing nothing.

Every predictor declares its own tunables, set with
`--param <predictor>.<parameter>=<value>`; `--list-predictors` shows what each
accepts, with defaults and ranges. `0` disables the return-address stack:

```sh
cd out && ./build/preset/risc-e --predictor gshare --param gshare.history-bits=14 path/to/program.elf
cd out && ./build/preset/risc-e --predictor two-bit --param two-bit.table-size=4096 --param two-bit.ras-depth=0 path/to/program.elf
```

## Adding a predictor

Predictors implement the abstract `BranchPredictor` interface
(`include/risc-e/cpu/branch_predictor.hpp`):

- `predict(const BranchContext&)` returns the predicted next PC
  (`std::nullopt` means fall-through).
- `resolve(const BranchContext&, const Resolution&)` feeds the actual outcome
  back for learning.
- `name()` returns the CLI name; `reset()` clears learned state (called when
  the interpreter is reset).

`BranchContext` describes the instruction (PC, opcode, registers, immediate)
and classifies it (`is_conditional_branch`, `is_call`, `is_return`, ...), so a
predictor can own its own components — a BTB for JALR targets, a return
address stack for call/return, a direction table for conditional branches.

Table-based predictors can reuse two small helpers from
`branch_predictor.hpp`: `counter_is_taken()` (is a 2-bit counter "taken"?) and
`predict_taken_or_fallthrough()` (turn a resolved direction into a
`Prediction`, falling through for JALR when no target source exists). The
reusable `ReturnAddressStack` component
(`include/risc-e/cpu/return_address_stack.hpp`) provides the push/peek/pop
used for call/return prediction; a depth of 0 disables it.

Each predictor lives in its own pair of files under
`include/risc-e/cpu/predictors/` and `src/cpu/predictors/` — e.g.
`two_bit_saturating.*`, `gshare.*`, `tournament.*`. A new predictor must:

1. expose a `static constexpr std::string_view kName` with its CLI name,
2. be registered in `make_predictor()` in `src/cpu/branch_predictor.cpp`,
3. implement `parameters()` and `set_parameter()` to declare and validate its
   tunables; the CLI and `--list-predictors` pick them up automatically,

and it will automatically appear in `predictor_names()`, `--list-predictors`
and `--comparison`.

`TwoBitSaturatingPredictor`, `AlwaysNotTakenPredictor`, `GsharePredictor`,
`TournamentPredictor` and `RasPredictor` are the built-in examples.

## Layout

```
include/risc-e/   public headers
  cpu/                CPU state, halt reasons, trap interface, branch
                      stats, the branch predictor interface, the pipeline
                      model, and a predictors/ subdirectory with one file
                      per predictor
  decoder/            instruction decoding and the shared opcode
                      constants (opcodes.hpp)
  elf/                ELF loading
  interpreter/        the interpreter
  memory/             memory interface and physical memory
  report/             report-section interface and per-topic sections
src/                implementation, mirroring include/risc-e/
  main.cpp            CLI frontend
  cpu/                implementation (incl. the pipeline model)
  decoder/            implementation
  elf/                implementation
  interpreter/        implementation
  memory/             implementation
  report/             implementation
tests/
  unit tests and RISC-V test programs
tests/programs/
  src/                RISC-V assembly and C sources
  elf/                built ELF outputs (generated, not tracked)
files/
  input/exit.S        sample source
  output/sample.elf   default program loaded when no argument is given
CMakeLists.txt        build, library, and test registration
CMakePresets.json     `preset`, `ci`, and `sanitize` presets
.github/workflows/ci.yml   CI build + test
```

Each public header under `include/risc-e/` has a matching implementation in
`src/`, except a few header-only ones that expose only inline constants and
helpers (`decoder/opcodes.hpp`, plus the inline helper functions in
`cpu/branch_predictor.hpp`). `main.cpp` is the only frontend and drives the
interpreter directly.

## Test

If a RISC-V GCC cross-compiler is on the PATH (`riscv64-unknown-elf-gcc` or
`riscv64-elf-gcc`), the build also assembles the
programs under `tests/programs/src/` into `tests/programs/elf/` and registers
integration tests for them. C sources under `tests/programs/src/` are tested
through the tool itself: `risc-e` compiles them on the fly and the test
checks the printed output, exit code and branch stats. The C programs cover
different branch patterns (predictable loops, data-dependent branches,
nested conditionals, switches, recursion) plus general-purpose programs
(data/bss/stack memory, arithmetic, heap via `brk`) that are also handy for
exercising other emulator features.

```sh
cd out && ctest --test-dir build/preset --output-on-failure
```

Tests cover ELF loading, stack/heap memory, unmapped read/write faults,
misaligned access, `exit` codes, and branch prediction (including a
toolchain-free test that feeds hand-encoded instructions to the interpreter
and checks the predictor's hit/miss counts). A GitHub Actions workflow
(`.github/workflows/ci.yml`) builds and runs the whole suite on every push,
both with the default toolchain and under ASan + UBSan.
