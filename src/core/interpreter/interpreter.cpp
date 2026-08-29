#include "risc-e/core/interpreter/interpreter.hpp"

#include "risc-e/core/cpu/trap.hpp"
#include "risc-e/core/decoder/decoder.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace {

constexpr uint8_t OPCODE_LOAD     = 0b0000011;
constexpr uint8_t OPCODE_MISC_MEM = 0b0001111;
constexpr uint8_t OPCODE_OP_IMM   = 0b0010011;
constexpr uint8_t OPCODE_AUIPC    = 0b0010111;
constexpr uint8_t OPCODE_STORE    = 0b0100011;
constexpr uint8_t OPCODE_LUI      = 0b0110111;
constexpr uint8_t OPCODE_OP       = 0b0110011;
constexpr uint8_t OPCODE_JAL      = 0b1101111;
constexpr uint8_t OPCODE_JALR     = 0b1100111;
constexpr uint8_t OPCODE_BRANCH   = 0b1100011;
constexpr uint8_t OPCODE_SYSTEM   = 0b1110011;

constexpr uint8_t F3_BEQ  = 0b000;
constexpr uint8_t F3_BNE  = 0b001;
constexpr uint8_t F3_BLT  = 0b100;
constexpr uint8_t F3_BGE  = 0b101;
constexpr uint8_t F3_BLTU = 0b110;
constexpr uint8_t F3_BGEU = 0b111;

constexpr uint8_t F3_ADDI  = 0b000;
constexpr uint8_t F3_SLTI  = 0b010;
constexpr uint8_t F3_SLTIU = 0b011;
constexpr uint8_t F3_XORI  = 0b100;
constexpr uint8_t F3_ORI   = 0b110;
constexpr uint8_t F3_ANDI  = 0b111;
constexpr uint8_t F3_SLLI  = 0b001;
constexpr uint8_t F3_SRLI  = 0b101;  // SRAI shares funct3 with SRLI

constexpr uint8_t F7_SLLI = 0x00;
constexpr uint8_t F7_SRAI = 0x20;

constexpr uint8_t F3_ADD_SUB = 0b000;
constexpr uint8_t F3_SLL     = 0b001;
constexpr uint8_t F3_SLT     = 0b010;
constexpr uint8_t F3_SLTU    = 0b011;
constexpr uint8_t F3_XOR     = 0b100;
constexpr uint8_t F3_SRL_SRA = 0b101;
constexpr uint8_t F3_OR      = 0b110;
constexpr uint8_t F3_AND     = 0b111;

constexpr uint8_t F7_SUB_SRA = 0x20;

constexpr uint8_t F3_LB  = 0b000;
constexpr uint8_t F3_LH  = 0b001;
constexpr uint8_t F3_LW  = 0b010;
constexpr uint8_t F3_LBU = 0b100;
constexpr uint8_t F3_LHU = 0b101;

constexpr uint8_t F3_SB = 0b000;
constexpr uint8_t F3_SH = 0b001;
constexpr uint8_t F3_SW = 0b010;

constexpr uint32_t kInitialStackPointer = 0x80000000;
constexpr uint32_t kStackSize           = 8u * 1024u * 1024u;  // 8 MiB

void write_register(CPUstate& state, uint8_t reg, uint32_t value) {
    if (reg != 0) state.x[reg] = value;
}

uint32_t align_up(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

bool is_valid_funct7(uint8_t funct7, uint8_t funct3) {
    switch (funct3) {
        case F3_ADD_SUB:
            return funct7 == 0x00 || funct7 == F7_SUB_SRA;
        case F3_SLL:
        case F3_SLT:
        case F3_SLTU:
        case F3_XOR:
        case F3_OR:
        case F3_AND:
            return funct7 == 0x00;
        case F3_SRL_SRA:
            return funct7 == 0x00 || funct7 == F7_SUB_SRA;
        default:
            return false;
    }
}

bool is_valid_shift(uint8_t funct7, uint8_t funct3) {
    switch (funct3) {
        case F3_SLLI:
            return funct7 == F7_SLLI;
        case F3_SRLI:  // SRAI shares funct3 with SRLI
            return funct7 == 0x00 || funct7 == F7_SRAI;
        default:
            return false;
    }
}

void execute_branch(CPUstate& state, TrapSink& sink, const DecodedInstruction& d) {
    const uint32_t rs1 = state.x[d.rs1];
    const uint32_t rs2 = state.x[d.rs2];
    bool taken = false;

    switch (d.funct3) {
        case F3_BEQ:  taken = rs1 == rs2; break;
        case F3_BNE:  taken = rs1 != rs2; break;
        case F3_BLT:  taken = static_cast<int32_t>(rs1) < static_cast<int32_t>(rs2); break;
        case F3_BGE:  taken = static_cast<int32_t>(rs1) >= static_cast<int32_t>(rs2); break;
        case F3_BLTU: taken = rs1 < rs2; break;
        case F3_BGEU: taken = rs1 >= rs2; break;
        default:
            sink.raiseTrap(TrapCause::ILLEGAL_INSTRUCTION, d.raw);
            return;
    }

    state.pc = taken ? state.pc + d.imm : state.pc + 4;
}

void execute_op_imm(CPUstate& state, TrapSink& sink, const DecodedInstruction& d) {
    const uint32_t rs1 = state.x[d.rs1];
    const uint32_t imm = static_cast<uint32_t>(d.imm);
    uint32_t result = 0;

    switch (d.funct3) {
        case F3_ADDI:
            result = rs1 + imm;
            break;
        case F3_SLTI:
            result = (static_cast<int32_t>(rs1) < d.imm) ? 1 : 0;
            break;
        case F3_SLTIU:
            result = (rs1 < imm) ? 1 : 0;
            break;
        case F3_XORI:
            result = rs1 ^ imm;
            break;
        case F3_ORI:
            result = rs1 | imm;
            break;
        case F3_ANDI:
            result = rs1 & imm;
            break;
        case F3_SLLI:
        case F3_SRLI:  // SRAI shares funct3 with SRLI
            if (!is_valid_shift(d.funct7, d.funct3)) {
                sink.raiseTrap(TrapCause::ILLEGAL_INSTRUCTION, d.raw);
                return;
            }
            {
                const uint32_t shamt = d.rs2 & 0x1F;
                if (d.funct3 == F3_SLLI) {
                    result = rs1 << shamt;
                } else if (d.funct7 == F7_SRAI) {
                    result = static_cast<uint32_t>(static_cast<int32_t>(rs1) >> shamt);
                } else {
                    result = rs1 >> shamt;
                }
            }
            break;
        default:
            sink.raiseTrap(TrapCause::ILLEGAL_INSTRUCTION, d.raw);
            return;
    }

    write_register(state, d.rd, result);
    state.pc += 4;
}

void execute_op(CPUstate& state, TrapSink& sink, const DecodedInstruction& d) {
    if (!is_valid_funct7(d.funct7, d.funct3)) {
        sink.raiseTrap(TrapCause::ILLEGAL_INSTRUCTION, d.raw);
        return;
    }

    const uint32_t rs1 = state.x[d.rs1];
    const uint32_t rs2 = state.x[d.rs2];
    uint32_t result = 0;

    switch (d.funct3) {
        case F3_ADD_SUB:
            result = (d.funct7 == F7_SUB_SRA) ? (rs1 - rs2) : (rs1 + rs2);
            break;
        case F3_SLL:
            result = rs1 << (rs2 & 0x1F);
            break;
        case F3_SLT:
            result = (static_cast<int32_t>(rs1) < static_cast<int32_t>(rs2)) ? 1 : 0;
            break;
        case F3_SLTU:
            result = (rs1 < rs2) ? 1 : 0;
            break;
        case F3_XOR:
            result = rs1 ^ rs2;
            break;
        case F3_SRL_SRA:
            result = (d.funct7 == F7_SUB_SRA)
                ? static_cast<uint32_t>(static_cast<int32_t>(rs1) >> (rs2 & 0x1F))
                : (rs1 >> (rs2 & 0x1F));
            break;
        case F3_OR:
            result = rs1 | rs2;
            break;
        case F3_AND:
            result = rs1 & rs2;
            break;
        default:
            sink.raiseTrap(TrapCause::ILLEGAL_INSTRUCTION, d.raw);
            return;
    }

    write_register(state, d.rd, result);
    state.pc += 4;
}

void execute_load(CPUstate& state, TrapSink& sink, const DecodedInstruction& d) {
    const uint32_t address = state.x[d.rs1] + static_cast<uint32_t>(d.imm);

    switch (d.funct3) {
        case F3_LB: {
            const int8_t val = static_cast<int8_t>(state.mem->load8(address));
            write_register(state, d.rd, static_cast<uint32_t>(static_cast<int32_t>(val)));
            break;
        }
        case F3_LH: {
            const int16_t val = static_cast<int16_t>(state.mem->load16(address));
            write_register(state, d.rd, static_cast<uint32_t>(static_cast<int32_t>(val)));
            break;
        }
        case F3_LW:
            write_register(state, d.rd, state.mem->load32(address));
            break;
        case F3_LBU:
            write_register(state, d.rd, state.mem->load8(address));
            break;
        case F3_LHU:
            write_register(state, d.rd, state.mem->load16(address));
            break;
        default:
            sink.raiseTrap(TrapCause::ILLEGAL_INSTRUCTION, d.raw);
            return;
    }

    if (!state.running) return;  // a memory fault halted execution
    state.pc += 4;
}

void execute_store(CPUstate& state, TrapSink& sink, const DecodedInstruction& d) {
    const uint32_t address = state.x[d.rs1] + static_cast<uint32_t>(d.imm);

    switch (d.funct3) {
        case F3_SB:
            state.mem->store8(address, static_cast<uint8_t>(state.x[d.rs2]));
            break;
        case F3_SH:
            state.mem->store16(address, static_cast<uint16_t>(state.x[d.rs2]));
            break;
        case F3_SW:
            state.mem->store32(address, state.x[d.rs2]);
            break;
        default:
            sink.raiseTrap(TrapCause::ILLEGAL_INSTRUCTION, d.raw);
            return;
    }

    if (!state.running) return;  // a memory fault halted execution
    state.pc += 4;
}

void execute_system(CPUstate& state, TrapSink& sink, const DecodedInstruction& d) {
    if (d.imm == 0) {
        // ECALL: trap now; handle_trap advances PC past the instruction
        sink.raiseTrap(TrapCause::ENVIRONMENT_CALL_FROM_MMODE, 0);
        return;
    }

    if (d.imm == 1) {  // EBREAK
        state.halt_reason = HaltReason::EBREAK;
        state.running     = false;
    } else {
        sink.raiseTrap(TrapCause::ILLEGAL_INSTRUCTION, d.raw);
        return;
    }

    state.pc += 4;
}

void load_elf_segments(PhysicalMemory& memory, const LoadedElf& elf) {
    for (const auto& segment : elf.segments) {
        memory.map_region(segment.vaddr, segment.size);
    }
    for (const auto& segment : elf.segments) {
        for (size_t index = 0; index < segment.size; ++index) {
            memory.store8(segment.vaddr + static_cast<uint32_t>(index), segment.data[index]);
        }
    }
}

} // namespace

Interpreter::Interpreter(LoadedElf elf)
    : entry_(static_cast<uint32_t>(elf.entry)),
      heap_break_(align_up(static_cast<uint32_t>(elf.end_vaddr), 16u))
{
    mem_.setTrapSink(this);
    state_.mem = &mem_;

    mem_.map_region(kInitialStackPointer - kStackSize, kStackSize);
    load_elf_segments(mem_, elf);

    reset();
    state_.x[2] = kInitialStackPointer;
}

void Interpreter::reset() {
    std::fill_n(state_.x, REG_COUNT, 0u);
    state_.pc          = entry_;
    state_.mepc        = 0;
    state_.mcause      = 0;
    state_.mtval       = 0;
    state_.running     = true;
    state_.halt_reason = HaltReason::NONE;
}

uint32_t Interpreter::fetch_instruction(uint32_t vaddr) const {
    return state_.mem->load32(vaddr);
}

void Interpreter::step() {
    if (!state_.running) return;

    const uint32_t inst = fetch_instruction(state_.pc);
    if (!state_.running) return;  // fetch may have trapped

    const DecodedInstruction d = decode_raw_inst(inst, state_.pc);
    execute(d);
}

void Interpreter::execute(const DecodedInstruction& d) {
    switch (d.opcode) {
        case OPCODE_LUI:
            write_register(state_, d.rd, static_cast<uint32_t>(d.imm));
            state_.pc += 4;
            break;

        case OPCODE_AUIPC:
            write_register(state_, d.rd, state_.pc + static_cast<uint32_t>(d.imm));
            state_.pc += 4;
            break;

        case OPCODE_JAL:
            write_register(state_, d.rd, state_.pc + 4);
            state_.pc += d.imm;
            break;

        case OPCODE_JALR: {
            const uint32_t target = (state_.x[d.rs1] + static_cast<uint32_t>(d.imm)) & ~1u;
            write_register(state_, d.rd, state_.pc + 4);
            state_.pc = target;
            break;
        }

        case OPCODE_BRANCH:
            execute_branch(state_, *this, d);
            break;

        case OPCODE_LOAD:
            execute_load(state_, *this, d);
            break;

        case OPCODE_STORE:
            execute_store(state_, *this, d);
            break;

        case OPCODE_OP_IMM:
            execute_op_imm(state_, *this, d);
            break;

        case OPCODE_OP:
            execute_op(state_, *this, d);
            break;

        case OPCODE_SYSTEM:
            execute_system(state_, *this, d);
            break;

        case OPCODE_MISC_MEM:  // FENCE
            state_.pc += 4;
            break;

        default:
            raiseTrap(TrapCause::ILLEGAL_INSTRUCTION, d.raw);
            break;
    }
}

void Interpreter::raiseTrap(TrapCause cause, uint32_t value) {
    state_.mepc   = state_.pc;
    state_.mcause = static_cast<uint32_t>(cause);
    state_.mtval  = value;
    handle_trap();
}

void Interpreter::handle_trap() {
    switch (static_cast<TrapCause>(state_.mcause)) {
        case TrapCause::ENVIRONMENT_CALL_FROM_MMODE: {
            switch (state_.x[17]) {  // a7 = syscall number
                case 64: {  // write(fd, buf, count)
                    const uint32_t fd    = state_.x[10];
                    const uint32_t buf   = state_.x[11];
                    const uint32_t count = state_.x[12];

                    if (fd == 1 || fd == 2) {
                        for (uint32_t i = 0; i < count; ++i) {
                            putchar(static_cast<char>(state_.mem->load8(buf + i)));
                        }
                        fflush(stdout);
                        state_.x[10] = count;
                    } else {
                        state_.x[10] = static_cast<uint32_t>(-1);
                    }
                    state_.pc = state_.mepc + 4;
                    return;
                }
                case 93: {  // exit(code)
                    state_.halt_reason = HaltReason::ECALL;
                    state_.running     = false;
                    state_.pc          = state_.mepc + 4;
                    return;
                }
                case 214: {  // brk(addr)
                    const uint32_t requested    = state_.x[10];
                    const uint32_t stack_bottom = kInitialStackPointer - kStackSize;

                    if (requested == 0 || requested < heap_break_ || requested > stack_bottom) {
                        state_.x[10] = heap_break_;
                    } else {
                        if (requested > heap_break_) {
                            mem_.map_region(heap_break_, requested - heap_break_);
                            heap_break_ = requested;
                        }
                        state_.x[10] = heap_break_;
                    }
                    state_.pc = state_.mepc + 4;
                    return;
                }
                default:
                    state_.x[10] = static_cast<uint32_t>(-1);
                    state_.pc    = state_.mepc + 4;
                    return;
            }
        }
        default:  // faults and misaligned access halt execution
            state_.halt_reason = HaltReason::TRAP;
            state_.running     = false;
            return;
    }
}

std::optional<uint32_t> Interpreter::run() {
    while (state_.running) {
        step();
    }

    if (state_.halt_reason == HaltReason::ECALL) {
        return state_.x[10];
    }
    return std::nullopt;
}

uint32_t Interpreter::get_register(int idx) const {
    if (idx < 0 || idx >= REG_COUNT) return 0;
    return state_.x[idx];
}
