# RISC-E

RISC-**Experiment** is a C++20 prototyper for RISC-V (RV32I) hardware components. You implement a component (e.g. a branch predictor, a pipeline model, an instruction or data cache), plug it into the harness, and run it against a real RV32I program to see how it behaves. Competing designs can be measured head-to-head on the same benchmark programs.

## Features

**Modeling** — every design below is a swappable `Component`, allowing new implementations to plug easily into the pipeline.

- **Branch predictors** — `two-bit` (default), `always-not-taken`, `gshare`, `tournament`, `ras` behind one interface, plus a trace replay for side-by-side comparison.
- **Pipeline model** — turns stall events into cycle cost with a configurable depth and penalty.
- **Instruction caches** — four designs over the same block profile: fully associative, set associative, pseudo-LRU, next-line prefetch.
- **L1 + L2 data cache** — one component chaining two write-back levels over the recorded load/store trace, with documented latency constants and an L1-only baseline.

**Emulation**

- **RV32I ISA** — LUI, AUIPC, JAL/JALR, branches, loads/stores, integer ALU, ECALL, EBREAK, FENCE.
- **ELF loading** — ELF32 `ET_EXEC` images.
- **Syscall emulation** — `write`, `exit`, `brk`.
- **Memory model** — page-based with on-demand allocation; unmapped access and misalignment raise traps instead of crashing.

## Quickstart

Prerequisites: a C++20 compiler and CMake 3.10+. A RISC-V cross-compiler (e.g. `riscv64-unknown-elf-gcc` or `riscv64-elf-gcc`) is needed to build the sample programs and to run `.c`/`.S` sources.

```sh
cmake -S . -B out/build/preset -DCMAKE_BUILD_TYPE=Debug
cmake --build out/build/preset
cd out && ./build/preset/risc-e build/preset/tests/programs/elf/branches.elf
```

The run's stats are saved as a JSON report under `results/` at the project root.

> [!NOTE]
> The sample programs are compiled at build time, so the  tool needs a RISC-V cross-compiler on `PATH`. Prebuilt ELF files run without it.

## Usage

### Running a program

Pass in an ELF file, or a `.c`/`.S` source:

```sh
cd out && ./build/preset/risc-e path/to/program.elf
cd out && ./build/preset/risc-e ../tests/programs/c/branchy.c
cd out && ./build/preset/risc-e ../tests/programs/assembly/branches.S
```

The program's exit code is printed and returned as the process exit status. C programs run freestanding (no libc): `main()`'s return value becomes the exit code, and a small runtime header provides inline-asm wrappers for the emulated syscalls.

### Output

Each run's stats are saved as a JSON report to `results/<program>.json` (override with `--json <path>`). Pass `--print` to also print the full report to the terminal.

### Disassembly

An extra output option to output raw assembly + hot block counts after simulated execution. 

The `--disasm` flag lists the loaded program as assembly, with `=>` marking the basic-block entry points the run actually executed and a `; block N, xM` annotation of their dynamic execution counts:

```sh
cd out && ./build/preset/risc-e --disasm path/to/program.elf
```

```
----------------- disassembly ------------------
text segment: 0x0000f000 .. 0x00010020 (4128 bytes)
=> 0x00010000:  addi t0, zero, 5   ; block 0, x1
=> 0x00010004:  addi t0, t0, -1   ; block 4, x4
   0x00010008:  bne t0, zero, 0x00010004
=> 0x0001000c:  jal ra, 0x00010018   ; block 2, x1
=> 0x00010010:  addi a7, zero, 93   ; block 1, x1
   0x00010014:  ecall
=> 0x00010018:  addi a0, zero, 7   ; block 3, x1
   0x0001001c:  jalr zero, 0(ra)
```

### CLI reference

| Flag | Effect |
| --- | --- |
| `--print` | Print the verbose human-readable report (default is a JSON report saved to a file). |
| `--verbose` | Add detail to the report: component config, the basic-block table, and per-type branch counts. |
| `--disasm` | Print a static disassembly of the loaded program, annotated with basic-block markers. |
| `--json <path>` | Write the JSON report to `<path>` instead of the default `results/<program>.json`. |
| `--predictor <name>` | Select the branch predictor (default `two-bit`). |
| `--icache <name>` | Select the instruction-cache design (default `icache-fa`). |
| `--dcache <name>` | Select the data-cache design (default `l1-l2`). |
| `--param <component>.<tunable>=<value>` | Set a component tunable; repeatable. |
| `--pipeline-stages <N>` | Pipeline depth (default 5). |
| `--stall-penalty <N>` | Per-stall-event penalty (default derived from depth). |
| `--comparison <type or name>` | Compare every design of a type, or one design. |
| `--list [name]` | List components grouped by type; append a name for its tunables. |

Tunables are namespaced by component: `--param gshare.history-bits=14`, `--param icache-setassoc.ways=1`, `--param l1-l2.l2-sets=64`. `--list <name>` shows a component's tunables, defaults, and value ranges.

### Comparing designs

`--comparison` runs the program once, then replays the recorded trace through every design of the type. Combined with `--print` the table is also shown on the terminal:

```sh
cd out && ./build/preset/risc-e --comparison predictor path/to/program.elf
cd out && ./build/preset/risc-e --comparison icache path/to/program.elf
cd out && ./build/preset/risc-e --comparison l1-l2 path/to/program.elf
```

```
comparison (7 events, 5-stage pipeline (2-cycle stall penalty); speedup vs no prediction):
  component        cycles before cycles after speedup
  two-bit          26            18           1.44x
  always-not-taken 26            26           1.00x
  gshare           26            18           1.44x
  tournament       26            18           1.44x
  ras              26            24           1.08x
```

## Components

| Type | Designs | What it models |
| --- | --- | --- |
| `predictor` | `two-bit`, `always-not-taken`, `gshare`, `tournament`, `ras` | Next-PC prediction for control transfers; tunables like `history-bits`, `ras-depth`. |
| `pipeline` | `pipeline` | Stall events into cycle cost; `stages`, `stall-penalty`. |
| `icache` | `icache-fa`, `icache-setassoc`, `icache-plru`, `icache-prefetch` | Instruction fetch over the run's basic-block profile; line size, sets, ways, miss penalty. |
| `cache` | `l1-l2` | L1 + L2 data-cache hierarchy over the load/store trace; per-level geometry and latencies (`l1-*`, `l2-*`). |

`--list` prints the full catalog with each design's tunables and defaults.

## Adding a component

There are two ways to grow the catalog. The common case is a new **implementation** of an existing type; adding a brand-new **type** is for families that don't exist yet — there are only four today (predictor, pipeline, icache, cache), and candidates like a TLB or a prefetcher are natural additions.

### Adding an implementation

A new design for an existing type. Extend the type's base class, implement its hooks, and register it. A new branch predictor, for example:

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

The harness features — tunables, a report section, comparison — come automatically. Registration is one call in `src/component/registry.cpp`:

```cpp
register_component<BranchPredictor, MyPredictor>(
    "predictor", MyPredictor::kName,
    []() -> std::unique_ptr<Component> { return std::make_unique<MyPredictor>(); });
```

`register_component` requires the implementation to extend the type's base class, so a mismatch fails to compile. The icache designs are the live example of the pattern: `ICacheComponent` implements the shared `report()` and `cycle_cost()` once, and each design in `include/risc-e/component/icache/implementations/` only declares its name, defaults, and tunables.

### Adding a new type

A brand-new family extends `Component` directly and implements the hooks itself. A TLB, for instance:

```cpp
class TlbComponent : public Component {
public:
    static constexpr std::string_view kName = "tlb";

    std::string_view name() const override { return kName; }
    std::string_view type() const override { return "tlb"; }

    std::vector<ParamSpec> parameters() const override { /* tunables */ }
    bool set_parameter(std::string_view name, std::string_view value,
                       std::string& error) override { /* validate + apply */ }

    std::string_view report_title() const override { return "tlb"; }
    void report(std::ostream& out, const RunContext& ctx) const override {
        // one report section per run
    }
    void write_json(std::ostream& out, const RunContext& ctx) const override {
        // the component's section of the JSON report (optional)
    }
};
```

A design that models time also answers the comparison table by overriding `cycle_cost` — the run's cycles under this design, the reference design's cycles, and a short name for that baseline:

```cpp
std::optional<CycleCost> cycle_cost(const RunContext& ctx) override {
    // this design's cycles, and the reference design it is compared to
    return CycleCost{total_cycles, baseline_cycles, "no TLB"};
}
```

Components that do not model time leave the hook defaulted and are skipped by `--comparison`. Then declare the type and register its implementations:

```cpp
register_type("tlb");

register_component<TlbComponent, TlbComponent>(
    "tlb", TlbComponent::kName,
    []() -> std::unique_ptr<Component> { return std::make_unique<TlbComponent>(); });
```

Registration is what makes a class swappable: types and components that are never registered are invisible to `--param`, `--list`, and `--comparison`.

## Project layout

```
include/risc-e/   public headers
  component/          the component harness (component, registry, run context)
                      and one folder per family — predictor/, pipeline/,
                      icache/, dcache/ — each with its base and recorded-data
                      header at the top and its implementations/ underneath
  cpu/                core CPU state and traps (non-component)
  decoder/            instruction decoding and opcode constants
  elf/                ELF loading
  interpreter/        the interpreter
  memory/             memory interface and physical memory (non-component)
src/                implementation, mirroring include/risc-e/
  main.cpp            CLI frontend
tests/              unit tests and RISC-V test programs
  programs/assembly/  RISC-V assembly sources (.S)
  programs/c/         C sources (.c)
results/            generated JSON reports, at the project root (gitignored)
CMakeLists.txt        core build (tests live in tests/CMakeLists.txt)
CMakePresets.json     `preset`, `ci`, and `sanitize` presets
```

## Testing

```sh
cd out && ctest --test-dir build/preset --output-on-failure
```
