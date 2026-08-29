# RISC-E

A RISC-V (RV32I) ELF loader and interpreter, written in C++20.

## Status

- ELF32 RISC-V loading (`ET_EXEC`)
- RV32I base instruction set: LUI, AUIPC, JAL, JALR, branches, loads/stores,
  integer arithmetic/logical ops, ECALL, EBREAK, FENCE
- ECALL syscall emulation: `write` (1), `exit` (93), `brk` (214)
- Page-based memory with on-demand allocation, unmapped access and
  misalignment raise traps instead of crashing

Not yet implemented: M extension, Zicsr (CSRs), compressed instructions (RVC),
IR lifting, and JIT code generation.

## Layout

```
include/risc-e/   public headers
  core/               the execution engine
    cpu/                CPU state, halt reasons, trap interface
    decoder/            instruction decoding
    elf/                ELF loading
    interpreter/        the interpreter
    memory/             memory interface and physical memory
    host_interface.hpp  hook contract for frontends
  risc-e.h            facade header for frontends
src/
  core/               implementation (one .cpp per public header)
  main.cpp            CLI frontend
tests/                unit tests and RISC-V test programs
```

The engine lives under `core/`; frontends (CLI, playground, debugger) stay at
the top level and interact with it only through the facade and `HostInterface`
(which they subclass to override syscalls and tracing).

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

Pass `--branch-stats` to print per-type branch taken counts and the simulated
predictor's hit/miss rate:

```sh
cd out && ./build/preset/risc-e --branch-stats path/to/program.elf
```

## Test

If `riscv64-unknown-elf-gcc` is on the PATH, the build also assembles the
programs under `tests/programs/` and registers integration tests for them.

```sh
cd out && ctest --test-dir build/preset --output-on-failure
```

Tests cover ELF loading, stack/heap memory, unmapped read/write faults,
misaligned access, and `exit` codes.

Tests cover ELF loading, stack/heap memory, unmapped read/write faults,
misaligned access, and `exit` codes.