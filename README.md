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
  `two-bit`, `always-not-taken`, `gshare`, `tournament` (selectable with
  `--predictor`)

Not yet implemented: M extension, Zicsr (CSRs), compressed instructions (RVC),
IR lifting, and JIT code generation.

## Layout

```
include/risc-e/   public headers
  cpu/                CPU state, halt reasons, trap interface, branch
                      stats, the branch predictor interface, and a
                      predictors/ subdirectory with one file per predictor
  decoder/            instruction decoding
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
  src/                RISC-V assembly sources
  elf/                built ELF outputs (generated, not tracked)
```

Each public header under `include/risc-e/` has a matching implementation in
`src/`; `main.cpp` is the only frontend and drives the interpreter directly.

## Build

```sh
cmake -S . -B out/build/preset -DCMAKE_BUILD_TYPE=Debug
cmake --build out/build/preset
```

## Run

```sh
cd out && ./build/preset/risc-e [path-to.elf]
```

Without an argument the interpreter loads `../files/output/sample.elf`.
The program's exit status is printed, and the process exits with the same code.

Branch stats are printed on every run: per-type branch taken counts and the
simulated predictor's hit/miss rate. Choose the predictor with
`--predictor <name>` (default `two-bit`) and list the available names with
`--list-predictors`:

```sh
cd out && ./build/preset/risc-e path/to/program.elf
cd out && ./build/preset/risc-e --predictor gshare path/to/program.elf
cd out && ./build/preset/risc-e --list-predictors
```

Assembly sources work too: if the argument ends in `.S` or `.s`, the tool
assembles it on the fly with a RISC-V cross-compiler found on `PATH`
(`riscv64-unknown-elf-gcc` or `riscv64-elf-gcc`, overridable via the
`RISCV_GCC` environment variable) and runs the result:

```sh
cd out && ./build/preset/risc-e ../tests/programs/src/branches.S
```

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

Each predictor lives in its own pair of files under
`include/risc-e/cpu/predictors/` and `src/cpu/predictors/` — e.g.
`two_bit_saturating.*`, `gshare.*`, `tournament.*`. A new predictor must:

1. expose a `static constexpr std::string_view kName` with its CLI name,
2. be registered in `make_predictor()` in `src/cpu/branch_predictor.cpp`,

and it will automatically appear in `predictor_names()` and
`--list-predictors`.

`TwoBitSaturatingPredictor`, `AlwaysNotTakenPredictor`, `GsharePredictor` and
`TournamentPredictor` are the built-in examples.

## Test

If a RISC-V GCC cross-compiler is on the PATH (`riscv64-unknown-elf-gcc` or
`riscv64-elf-gcc`), the build also assembles the
programs under `tests/programs/src/` into `tests/programs/elf/` and registers
integration tests for them.

```sh
cd out && ctest --test-dir build/preset --output-on-failure
```

Tests cover ELF loading, stack/heap memory, unmapped read/write faults,
misaligned access, `exit` codes, and branch prediction (including a
toolchain-free test that feeds hand-encoded instructions to the interpreter
and checks the predictor's hit/miss counts).
