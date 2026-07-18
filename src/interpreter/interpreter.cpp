#include "interpreter.hpp"
#include "src/decoder/decoder.hpp"
#include "src/interpreter/exception.hpp"

#include <cstddef>
#include <cstdint>
#include <algorithm>

namespace {

    constexpr uint32_t kInitialStackPointer = 0x80000000;

    constexpr uint8_t kOpcodeLui    = 0b0110111;
    constexpr uint8_t kOpcodeAuipc  = 0b0010111;
    constexpr uint8_t kOpcodeJal    = 0b1101111;
    constexpr uint8_t kOpcodeJalr   = 0b1100111;
    constexpr uint8_t kOpcodeBranch = 0b1100011;
    constexpr uint8_t kOpcodeLoad   = 0b0000011;
    constexpr uint8_t kOpcodeStore  = 0b0100011;
    constexpr uint8_t kOpcodeOpImm  = 0b0010011;
    constexpr uint8_t kOpcodeOp     = 0b0110011;
    constexpr uint8_t kOpcodeSystem = 0b1110011;
    constexpr uint8_t kOpcodeFence  = 0b0001111;

    constexpr uint8_t kBeq  = 0b000;
    constexpr uint8_t kBne  = 0b001;
    constexpr uint8_t kBlt  = 0b100;
    constexpr uint8_t kBge  = 0b101;
    constexpr uint8_t kBltu = 0b110;
    constexpr uint8_t kBgeu = 0b111;

    constexpr uint8_t kLb  = 0b000;
    constexpr uint8_t kLh  = 0b001;
    constexpr uint8_t kLw  = 0b010;
    constexpr uint8_t kLbu = 0b100;
    constexpr uint8_t kLhu = 0b101;

    constexpr uint8_t kSb = 0b000;
    constexpr uint8_t kSh = 0b001;
    constexpr uint8_t kSw = 0b010;

    constexpr uint8_t kAddSub = 0b000;
    constexpr uint8_t kSll    = 0b001;
    constexpr uint8_t kSlt    = 0b010;
    constexpr uint8_t kSltu   = 0b011;
    constexpr uint8_t kXor    = 0b100;
    constexpr uint8_t kSr     = 0b101;
    constexpr uint8_t kOr     = 0b110;
    constexpr uint8_t kAnd    = 0b111;

    constexpr uint8_t kSubSra = 0b0100000;

    inline uint32_t read_register(const CPUstate& state, uint8_t index) {
        return index == 0 ? 0u : state.x[index];
    }

    inline void write_register(CPUstate& state, uint8_t index, uint32_t value) {
        if (index != 0) {
            state.x[index] = value;
        }
    }

    void load_elf_segments(PhysicalMemory& memory, const LoadedElf& elf) {
        for (const auto& segment : elf.segments) {
            for (size_t index = 0; index < segment.size; ++index) {
                memory.store8(segment.vaddr + static_cast<uint32_t>(index), segment.data[index]);
            }
        }
    }

    void execute_branch(CPUstate& state, const DecodedInstruction& d) {
        const uint32_t lhs = read_register(state, d.rs1);
        const uint32_t rhs = read_register(state, d.rs2);
        bool taken = false;

        switch (d.funct3) {
            case kBeq:  taken = (lhs == rhs); break;
            case kBne:  taken = (lhs != rhs); break;
            case kBlt:  taken = (static_cast<int32_t>(lhs) < static_cast<int32_t>(rhs)); break;
            case kBge:  taken = (static_cast<int32_t>(lhs) >= static_cast<int32_t>(rhs)); break;
            case kBltu: taken = (lhs < rhs); break;
            case kBgeu: taken = (lhs >= rhs); break;
            default:
                state.mcause = static_cast<uint32_t>(TrapCause::ILLEGAL_INSTRUCTION);
                return;
        }

        state.pc = taken ? state.pc + static_cast<uint32_t>(d.imm) : state.pc + 4;
    }

    void execute_load(CPUstate& state, PhysicalMemory& memory, const DecodedInstruction& d) {
        const uint32_t address = read_register(state, d.rs1) + static_cast<uint32_t>(d.imm);
        uint32_t value = 0;

        switch (d.funct3) {
            case kLb:  value = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(memory.load8(address)))); break;
            case kLh:  value = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(memory.load16(address)))); break;
            case kLw:  value = memory.load32(address); break;
            case kLbu: value = memory.load8(address); break;
            case kLhu: value = memory.load16(address); break;
            default:
                state.mcause = static_cast<uint32_t>(TrapCause::LOAD_MISALIGNED);
                state.mtval = address;
                return;
        }

        write_register(state, d.rd, value);
        state.pc += 4;
    }

    void execute_store(CPUstate& state, PhysicalMemory& memory, const DecodedInstruction& d) {
        const uint32_t address = read_register(state, d.rs1) + static_cast<uint32_t>(d.imm);

        switch (d.funct3) {
            case kSb: memory.store8(address, static_cast<uint8_t>(read_register(state, d.rs2) & 0xFFu)); break;
            case kSh: memory.store16(address, static_cast<uint16_t>(read_register(state, d.rs2) & 0xFFFFu)); break;
            case kSw: memory.store32(address, read_register(state, d.rs2)); break;
            default:
                state.mcause = static_cast<uint32_t>(TrapCause::STORE_MISALIGNED);
                state.mtval = address;
                return;
        }

        state.pc += 4;
    }

    void execute_op_imm(CPUstate& state, const DecodedInstruction& d) {
        const uint32_t source = read_register(state, d.rs1);
        const int32_t immediate = d.imm;
        const uint32_t immediate_u = static_cast<uint32_t>(immediate);
        uint32_t result = 0;

        switch (d.funct3) {
            case kAddSub: result = source + immediate_u; break;
            case kSlt:    result = static_cast<int32_t>(source) < immediate ? 1u : 0u; break;
            case kSltu:   result = source < immediate_u ? 1u : 0u; break;
            case kXor:    result = source ^ immediate_u; break;
            case kOr:     result = source | immediate_u; break;
            case kAnd:    result = source & immediate_u; break;
            case kSll:    result = source << (d.rs2 & 0x1Fu); break;
            case kSr:
                result = (d.funct7 == kSubSra)
                    ? static_cast<uint32_t>(static_cast<int32_t>(source) >> (d.rs2 & 0x1Fu))
                    : source >> (d.rs2 & 0x1Fu);
                break;
            default:
                state.mcause = static_cast<uint32_t>(TrapCause::ILLEGAL_INSTRUCTION);
                return;
        }

        write_register(state, d.rd, result);
        state.pc += 4;
    }

    void execute_op(CPUstate& state, TrapSink& trap_sink, const DecodedInstruction& d) {
        const uint32_t lhs = read_register(state, d.rs1);
        const uint32_t rhs = read_register(state, d.rs2);
        uint32_t result = 0;

        switch (d.funct3) {
            case kAddSub:
                result = (d.funct7 == kSubSra) ? lhs - rhs : lhs + rhs;
                break;
            case kSll:  result = lhs << (rhs & 0x1Fu); break;
            case kSlt:  result = static_cast<int32_t>(lhs) < static_cast<int32_t>(rhs) ? 1u : 0u; break;
            case kSltu: result = lhs < rhs ? 1u : 0u; break;
            case kXor:  result = lhs ^ rhs; break;
            case kSr:
                result = (d.funct7 == kSubSra)
                    ? static_cast<uint32_t>(static_cast<int32_t>(lhs) >> (rhs & 0x1Fu))
                    : lhs >> (rhs & 0x1Fu);
                break;
            case kOr:   result = lhs | rhs; break;
            case kAnd:  result = lhs & rhs; break;
            default:
                trap_sink.raiseTrap(TrapCause::ILLEGAL_INSTRUCTION, d.raw);
                return;
        }

        write_register(state, d.rd, result);
        state.pc += 4;
    }

    void execute_system(CPUstate& state, TrapSink& trap_sink, const DecodedInstruction& d) {
        if (d.imm == 0) {
            // ECALL — raise trap; handle_trap will advance PC past the instruction
            trap_sink.raiseTrap(TrapCause::ENVIRONMENT_CALL_FROM_MMODE, 0);
            return;  // do NOT advance PC here — handle_trap does it
        } else if (d.imm == 1) {
            // EBREAK
            state.halt_reason = HaltReason::EBREAK;
            state.running = false;
        }

        state.pc += 4;
    }

} // namespace

Interpreter::Interpreter(LoadedElf elf)
    : base_vaddr_(elf.base_vaddr),
      entry_(elf.entry)
{
    mem_.setTrapSink(this);
    state_.mem = &mem_;
    reset();

    load_elf_segments(mem_, elf);
    state_.x[2] = kInitialStackPointer;
}

void Interpreter::reset() {
    std::fill_n(state_.x, REG_COUNT, 0u);
    state_.pc = entry_;
    state_.mepc = 0;
    state_.mcause = 0;
    state_.mtval = 0;
    state_.running = true;
    state_.halt_reason = HaltReason::NONE;
    state_.jit_state = nullptr;
}

uint32_t Interpreter::get_register(int idx) const {
    if (idx == 0) return 0;
    return state_.x[idx];
}

void Interpreter::raiseTrap(TrapCause cause, uint32_t value) {
    state_.mepc = state_.pc;
    state_.mcause = static_cast<uint32_t>(cause);
    state_.mtval = value;

    handle_trap();
}

void Interpreter::handle_trap() {
    switch (static_cast<TrapCause>(state_.mcause)) {
        case TrapCause::ENVIRONMENT_CALL_FROM_MMODE: {
            const uint32_t syscall_num = state_.x[17]; // a7

            switch (syscall_num) {
                case 64: { // write(fd, buf, count) — fd in a0, buf in a1, count in a2
                    uint32_t fd  = state_.x[10];
                    uint32_t buf = state_.x[11];
                    uint32_t len = state_.x[12];

                    // Only emulate write to stdout/stderr by piping through stdio
                    if (fd <= 2 && state_.mem) {
                        for (uint32_t i = 0; i < len; ++i) {
                            char c = static_cast<char>(state_.mem->load8(buf + i));
                            putchar(c);
                        }
                        fflush(stdout);
                        state_.x[10] = len; // return bytes written
                    } else {
                        state_.x[10] = -1; // EBADF
                    }
                    state_.pc = state_.mepc + 4;
                    return;
                }
                case 93: // exit(code) — code in a0
                    state_.halt_reason = HaltReason::ECALL;
                    state_.running = false;
                    state_.pc = state_.mepc + 4;
                    return;
                case 214: // brk(addr) — addr in a0; no-op, return current break
                    // Minimal: just return 0x80000000 (top of heap approximation)
                    state_.x[10] = 0x80000000;
                    state_.pc = state_.mepc + 4;
                    return;
                default:
                    // Unknown syscall — return -ENOSYS
                    state_.x[10] = static_cast<uint32_t>(-1);
                    state_.pc = state_.mepc + 4;
                    return;
            }
        }
        case TrapCause::ILLEGAL_INSTRUCTION:
        case TrapCause::LOAD_FAULT:
        case TrapCause::STORE_FAULT:
        case TrapCause::LOAD_MISALIGNED:
        case TrapCause::STORE_MISALIGNED:
            state_.halt_reason = HaltReason::TRAP;
            state_.running = false;
            break;
    }
}

void Interpreter::execute(const DecodedInstruction& d) {
    switch (d.opcode) {
        case kOpcodeLui:
            write_register(state_, d.rd, static_cast<uint32_t>(d.imm));
            state_.pc += 4;
            break;

        case kOpcodeAuipc:
            write_register(state_, d.rd, state_.pc + static_cast<uint32_t>(d.imm));
            state_.pc += 4;
            break;

        case kOpcodeJal:
            write_register(state_, d.rd, state_.pc + 4);
            state_.pc = state_.pc + static_cast<uint32_t>(d.imm);
            break;

        case kOpcodeJalr: {
            const uint32_t target = (read_register(state_, d.rs1) + static_cast<uint32_t>(d.imm)) & ~1u;
            write_register(state_, d.rd, state_.pc + 4);
            state_.pc = target;
            break;
        }

        case kOpcodeBranch:
            execute_branch(state_, d);
            break;

        case kOpcodeLoad:
            execute_load(state_, mem_, d);
            break;

        case kOpcodeStore:
            execute_store(state_, mem_, d);
            break;

        case kOpcodeOpImm:
            execute_op_imm(state_, d);
            break;

        case kOpcodeOp:
            execute_op(state_, *this, d);
            break;

        case kOpcodeSystem:
            execute_system(state_, *this, d);
            break;

        case kOpcodeFence:
            state_.pc += 4;
            break;

        default:
            raiseTrap(TrapCause::ILLEGAL_INSTRUCTION, d.raw);
            return;
    }

    state_.x[0] = 0;
}

uint32_t Interpreter::fetch_instruction(uint32_t vaddr) const {
    return state_.mem->load32(vaddr);
}

void Interpreter::step() {
    const uint32_t inst_word = fetch_instruction(state_.pc);

    const DecodedInstruction decoded = decode_raw_inst(inst_word, state_.pc);

    execute(decoded);
}

std::optional<uint32_t> Interpreter::run() {
    while (state_.running) {
        step();
    }

    // If halted via ECALL, the exit code is conventionally in a0 (x10).
    if (state_.halt_reason == HaltReason::ECALL) {
        return state_.x[10]; // a0
    }

    // EBREAK, trap, or other — no meaningful exit code
    return std::nullopt;
}