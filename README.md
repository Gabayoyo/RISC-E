# RISC-E

A RISC-V (RV32I) ELF loader and interpreter, written in C++20.

## Status

- ELF32 RISC-V loading (`ET_EXEC`)
- RV32I base instruction set: LUI, AUIPC, JAL, JALR, branches, loads/stores,
  integer arithmetic/logical ops, ECALL, EBREAK, FENCE
- ECALL syscall emulation: `write` (64), `exit` (93), `brk` (214)
- Page-based memory with on-demand allocation, unmapped access and
  misalignment raise traps instead of crashing
- Pluggable branch prediction: conditional branches, JAL and JALR all go
  through a small `BranchPredictor` interface; hit rates are measured
  target-aware (predicted next PC vs actual next PC). Built-in predictors:
  `two-bit`, `always-not-taken`, `gshare`, `tournament`, `ras` (selectable
  with `--predictor`). The table predictors own a small return-address stack
  (RAS) for call/return targets; every predictor declares its own tunables,
  set with `--param <predictor>.<parameter>=<value>` (see `--list-predictors`),
  and `--replay` runs every predictor over one recorded control-flow trace to
  compare them directly.

Not yet implemented: M extension, Zicsr (CSRs), compressed instructions (RVC),
IR lifting, and JIT code generation.

## Layout

```
include/risc-e/   public headers
  cpu/                CPU state, halt reasons, trap interface, branch
                      stats, the branch predictor interface, and a
                      predictors/ subdirectory with one file per predictor
  decoder/            instruction decoding and the shared opcode
                      constants (opcodes.hpp)
  elf/                ELF loading
  interpreter/        the interpreter
  memory/             memory interface and physical memory
src/
  main.cpp            CLI frontend
  cpu/                implementation
  decoder/            implementation
  elf/                implementation
  interpreter/        implementation
  memory/             implementation
tests/
  unit tests and RISC-V test programs
tests/programs/
  src/                RISC-V assembly and C sources
  elf/                built ELF outputs (generated, not tracked)
```

Each public header under `include/risc-e/` has a matching implementation in
`src/`, except a few header-only ones that expose only inline constants and
helpers (`decoder/opcodes.hpp`, plus the inline helper functions in
`cpu/branch_predictor.hpp`). `main.cpp` is the only frontend and drives the
interpreter directly.

## Build

```sh
cmake -S . -B out/build/preset -DCMAKE_BUILD_TYPE=Debug
cmake --build out/build/preset
```

A portable `ci` preset (default toolchain) and a `sanitize` preset
(ASan + UBSan) are also available; the CI workflow on GitHub builds and tests
both on every push:

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

Branch stats are printed on every run: per-type branch taken counts and the
simulated predictor's hit/miss rate. Choose the predictor with
`--predictor <name>` (default `two-bit`) and list the available predictors and
their parameters with `--list-predictors` (append a predictor name for a
detailed view):

```sh
cd out && ./build/preset/risc-e path/to/program.elf
cd out && ./build/preset/risc-e --predictor gshare path/to/program.elf
cd out && ./build/preset/risc-e --list-predictors
```

Every predictor declares its own tunables, set with
`--param <predictor>.<parameter>=<value>`; `--list-predictors` shows what each
accepts, with defaults and ranges. `0` disables the return-address stack:

```sh
cd out && ./build/preset/risc-e --predictor gshare --param gshare.history-bits=14 path/to/program.elf
cd out && ./build/preset/risc-e --predictor two-bit --param two-bit.table-size=4096 --param two-bit.ras-depth=0 path/to/program.elf
cd out && ./build/preset/risc-e --list-predictors
```

```
two-bit [table-size=1024, ras-depth=16]
always-not-taken
gshare [history-bits=12, ras-depth=16]
tournament [history-bits=10, ras-depth=16]
ras [ras-depth=16]
```

`--replay` executes the program once and then runs every available predictor
over the recorded control-flow trace, printing a side-by-side comparison:

```sh
cd out && ./build/preset/risc-e --replay path/to/program.elf
```

```
replay results (7 control transfers):
  two-bit: 6/7 hits (85.7143%), cond 4/5, indirect 1/1
  always-not-taken: 2/7 hits (28.5714%), cond 1/5, indirect 0/1
  gshare: 6/7 hits (85.7143%), cond 4/5, indirect 1/1
  tournament: 6/7 hits (85.7143%), cond 4/5, indirect 1/1
  ras: 3/7 hits (42.8571%), cond 1/5, indirect 1/1
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
jumps (JAL) are trivially predictable and usually count as hits; the
conditional and indirect (JALR) hit rates are reported separately.

### Adding a predictor

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
and `--replay`.

`TwoBitSaturatingPredictor`, `AlwaysNotTakenPredictor`, `GsharePredictor`,
`TournamentPredictor` and `RasPredictor` are the built-in examples.

## Test

If a RISC-V GCC cross-compiler is on the PATH (`riscv64-unknown-elf-gcc` or
`riscv64-elf-gcc`), the build also assembles the
programs under `tests/programs/src/` into `tests/programs/elf/` and registers
integration tests for them. C sources under `tests/programs/src/` are tested
through the tool itself: `risc-e` compiles them on the fly and the test
checks the printed output, exit code and branch stats.

```sh
cd out && ctest --test-dir build/preset --output-on-failure
```

Tests cover ELF loading, stack/heap memory, unmapped read/write faults,
misaligned access, `exit` codes, and branch prediction (including a
toolchain-free test that feeds hand-encoded instructions to the interpreter
and checks the predictor's hit/miss counts). A GitHub Actions workflow
(`.github/workflows/ci.yml`) builds and runs the whole suite on every push,
both with the default toolchain and under ASan + UBSan.
