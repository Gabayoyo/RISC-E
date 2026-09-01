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

Prerequisites: a C++20 compiler and CMake 3.10+. A RISC-V cross-compiler (`riscv64-unknown-elf-gcc` or `riscv64-elf-gcc`, overridable with `RISCV_GCC`) is needed to build the sample programs and to run `.c`/`.S` sources.

```sh
cmake -S . -B out/build/preset -DCMAKE_BUILD_TYPE=Debug
cmake --build out/build/preset
cd out && ./build/preset/risc-e build/preset/tests/programs/elf/branches.elf
```

> [!NOTE]
> The sample programs are compiled at build time, so the Quickstart needs a RISC-V cross-compiler on `PATH`. Prebuilt ELF files run without it.

## Usage

### Running a program

Pass an ELF file, or a `.c`/`.S` source that the tool compiles on the fly:

```sh
cd out && ./build/preset/risc-e path/to/program.elf
cd out && ./build/preset/risc-e ../tests/programs/c/branchy.c
cd out && ./build/preset/risc-e ../tests/programs/assembly/branches.S
```

The program's exit code is printed and returned as the process exit status. C programs run freestanding (no libc): `main()`'s return value becomes the exit code, and a small runtime header provides inline-asm wrappers for the emulated syscalls.

### The report

Every run prints four sections — **pipeline** (the run summary: instructions, distinct instructions, basic blocks, data accesses, and the cycle model), **branch prediction**, **icache**, and **cache**:

```
pipeline
  model: 5-stage pipeline (2-cycle stall penalty)
  instructions executed: 16
  distinct instructions: 8
  basic blocks: 5
  data accesses: 0 (0 loads, 0 stores)
  ideal cycles: 16
  stall cycles: 2 (1 stall event x 2 cycles)
  total cycles: 18
  CPI: 1.125
  slowdown: +12.50% vs perfect
  cycles saved: 12 vs worst-case (7 stall events)

branch prediction
  predictor: two-bit
  hit rate: 85.7143%
  miss rate: 14.2857%
  hits: 6
  misses: 1
  branches: 7

icache
  instruction cache (miss penalty 50, line 16 B, 1 set x 16 ways, LRU):
    hits: 6 (75.00%)
    misses: 2
    compulsory misses: 2
    conflict misses: 0
    capacity misses: 0
    evictions: 0
  cycles saved: 300 (72.12%) vs no instruction cache — 3.59x

cache
  L1 (16 sets x 4 ways, line 16 B, write-back, 4-cycle hit):
    hits: 0 (0.00%)
    misses: 0
    compulsory misses: 0
    conflict misses: 0
    capacity misses: 0
    evictions: 0
    writebacks: 0
  L2 (32 sets x 8 ways, line 64 B, write-back, 14-cycle hit):
    hits: 0 (0.00%)
    misses: 0
    compulsory misses: 0
    conflict misses: 0
    capacity misses: 0
    evictions: 0
    writebacks: 0
  cycles saved: 0 (0.00%) vs L1 only — 0.00x

exit code: 7
```

Each cache section ends with one line summarising its cycle cost against its baseline (`no instruction cache` for the icache, `L1 only` for the data cache).

### CLI reference

| Flag | Effect |
| --- | --- |
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

`--comparison` runs the program once, then replays the recorded trace through every design of the type, printing the same three columns for each row — cycles before (the type's baseline), cycles after, and speedup:

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

The baseline is a property of each type's cost model, so the columns are comparable within a type but never across types.

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
CMakeLists.txt        core build (tests live in tests/CMakeLists.txt)
CMakePresets.json     `preset`, `ci`, and `sanitize` presets
```

## Testing

```sh
cd out && ctest --test-dir build/preset --output-on-failure
```

The unit tests run without a cross-compiler. With one on `PATH`, the build also compiles the programs under `tests/programs/` and registers integration tests covering exit codes, printed output, memory faults, misalignment, and branch stats. The `sanitize` preset runs the whole suite under ASan + UBSan:

```sh
cmake -S . -B out/build/sanitize --preset sanitize
```

A GitHub Actions workflow builds and tests the default toolchain and the sanitize preset on every push.
