#include "interpreter.hpp"
#include "src/decoder/decoder.hpp"
#include "src/interpreter/exception.hpp"

#include <cstddef>
#include <cstdint>
#include <algorithm>

namespace {

constexpr uint32_t kInitialTrapVector = 0x00001000;
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

constexpr uint8_t kFunct3Beq  = 0b000;
constexpr uint8_t kFunct3Bne  = 0b001;
constexpr uint8_t kFunct3Blt  = 0b100;
constexpr uint8_t kFunct3Bge  = 0b101;
constexpr uint8_t kFunct3Bltu = 0b110;
constexpr uint8_t kFunct3Bgeu = 0b111;

constexpr uint8_t kFunct3Lb  = 0b000;
constexpr uint8_t kFunct3Lh  = 0b001;
constexpr uint8_t kFunct3Lw  = 0b010;
constexpr uint8_t kFunct3Lbu = 0b100;
constexpr uint8_t kFunct3Lhu = 0b101;

constexpr uint8_t kFunct3Sb = 0b000;
constexpr uint8_t kFunct3Sh = 0b001;
constexpr uint8_t kFunct3Sw = 0b010;

constexpr uint8_t kFunct3AddSub = 0b000;
constexpr uint8_t kFunct3Sll    = 0b001;
constexpr uint8_t kFunct3Slt    = 0b010;
constexpr uint8_t kFunct3Sltu   = 0b011;
constexpr uint8_t kFunct3Xor    = 0b100;
constexpr uint8_t kFunct3Sr     = 0b101;
constexpr uint8_t kFunct3Or     = 0b110;
constexpr uint8_t kFunct3And    = 0b111;

constexpr uint8_t kFunct7SubSra = 0b0100000;

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
        case kFunct3Beq:  taken = (lhs == rhs); break;
        case kFunct3Bne:  taken = (lhs != rhs); break;
        case kFunct3Blt:  taken = (static_cast<int32_t>(lhs) < static_cast<int32_t>(rhs)); break;
        case kFunct3Bge:  taken = (static_cast<int32_t>(lhs) >= static_cast<int32_t>(rhs)); break;
        case kFunct3Bltu: taken = (lhs < rhs); break;
        case kFunct3Bgeu: taken = (lhs >= rhs); break;
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
        case kFunct3Lb:  value = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(memory.load8(address)))); break;
        case kFunct3Lh:  value = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(memory.load16(address)))); break;
        case kFunct3Lw:  value = memory.load32(address); break;
        case kFunct3Lbu: value = memory.load8(address); break;
        case kFunct3Lhu: value = memory.load16(address); break;
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
        case kFunct3Sb: memory.store8(address, static_cast<uint8_t>(read_register(state, d.rs2) & 0xFFu)); break;
        case kFunct3Sh: memory.store16(address, static_cast<uint16_t>(read_register(state, d.rs2) & 0xFFFFu)); break;
        case kFunct3Sw: memory.store32(address, read_register(state, d.rs2)); break;
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
        case kFunct3AddSub: result = source + immediate_u; break;
        case kFunct3Slt:    result = static_cast<int32_t>(source) < immediate ? 1u : 0u; break;
        case kFunct3Sltu:   result = source < immediate_u ? 1u : 0u; break;
        case kFunct3Xor:    result = source ^ immediate_u; break;
        case kFunct3Or:     result = source | immediate_u; break;
        case kFunct3And:    result = source & immediate_u; break;
        case kFunct3Sll:    result = source << (d.rs2 & 0x1Fu); break;
        case kFunct3Sr:
            result = (d.funct7 == kFunct7SubSra)
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
        case kFunct3AddSub:
            result = (d.funct7 == kFunct7SubSra) ? lhs - rhs : lhs + rhs;
            break;
        case kFunct3Sll:  result = lhs << (rhs & 0x1Fu); break;
        case kFunct3Slt:  result = static_cast<int32_t>(lhs) < static_cast<int32_t>(rhs) ? 1u : 0u; break;
        case kFunct3Sltu: result = lhs < rhs ? 1u : 0u; break;
        case kFunct3Xor:  result = lhs ^ rhs; break;
        case kFunct3Sr:
            result = (d.funct7 == kFunct7SubSra)
                ? static_cast<uint32_t>(static_cast<int32_t>(lhs) >> (rhs & 0x1Fu))
                : lhs >> (rhs & 0x1Fu);
            break;
        case kFunct3Or:   result = lhs | rhs; break;
        case kFunct3And:  result = lhs & rhs; break;
        default:
            trap_sink.raiseTrap(TrapCause::ILLEGAL_INSTRUCTION, d.raw);
            return;
    }

    write_register(state, d.rd, result);
    state.pc += 4;
}

void execute_system(CPUstate& state, TrapSink& trap_sink, const DecodedInstruction& d) {
    if (d.imm == 0) {
        trap_sink.raiseTrap(TrapCause::ENVIRONMENT_CALL_FROM_MMODE, 0);
        state.running = false;
    } else if (d.imm == 1) {
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
    state_.mtvec = kInitialTrapVector;
    state_.mepc = 0;
    state_.mcause = 0;
    state_.mtval = 0;
    state_.running = true;
    state_.trap_pending = false;
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

    state_.pc = state_.mtvec;
    state_.trap_pending = true;
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
    if (state_.trap_pending) {
        state_.trap_pending = false;
        return;
    }

    const uint32_t inst_word = fetch_instruction(state_.pc);

    if (state_.trap_pending) {
        state_.trap_pending = false;
        return;
    }

    const DecodedInstruction decoded = decode_raw_inst(inst_word, state_.pc);

    execute(decoded);

    if (state_.trap_pending) {
        state_.trap_pending = false;
        return;
    }
}

void Interpreter::run() {
    while (state_.running) {
        step();
    }
}