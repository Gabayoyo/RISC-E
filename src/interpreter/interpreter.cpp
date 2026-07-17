#include "interpreter.hpp"
#include "src/decoder/decoder.hpp"

#include <cstring>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <utility>
#include <set>
#include <iterator>

Interpreter::Interpreter(LoadedElf elf)
    : base_vaddr_(elf.base_vaddr),
      entry_(elf.entry)
{
    mem_.setTrapSink(this);
    reset();
    load_elf_segments(mem_, elf);

    state_.mem = &mem_;

    state_.x[2] = 0x80000000; 

    // Maybe initialise some registers?
}

void Interpreter::load_elf_segments(PhysicalMemory& mem, const LoadedElf& elf) {
    for (const auto& seg : elf.segments) {
        // seg.data already covers [vaddr .. vaddr + size)
        // with .bss zero‑filled automatically
        for (size_t i = 0; i < seg.size; ++i)
            mem.store8(seg.vaddr + i, seg.data[i]);
    }
}


void Interpreter::reset() {
    std::memset(state_.x, 0, sizeof(state_.x));
    state_.pc = entry_;
    state_.mtvec = 0x00001000;
}

uint32_t Interpreter::get_register(int idx) const {
    if (idx == 0) return 0;
    return state_.x[idx];
}

void Interpreter::raiseTrap(TrapCause cause, uint32_t value) {
    // Save the PC of the instruction that caused the trap
    state_.mepc   = state_.pc;
    state_.mcause = static_cast<uint32_t>(cause);
    state_.mtval  = value;

    // Jump to the trap handler
    state_.pc = state_.mtvec;
    state_.trap_pending = true; 

    // No need for the exception flag – the trap has already been taken.
}

void Interpreter::execute(const DecodedInstruction& d) {
    auto& x = state_.x;
    auto& pc = state_.pc;

    x[0] = 0; // x0 is always zero

    switch (d.opcode) {
        case 0b0110111: // LUI
            if (d.rd) x[d.rd] = static_cast<uint32_t>(d.imm);
            pc += 4;
            break;

        case 0b0010111: // AUIPC
                if (d.rd) x[d.rd] = pc + static_cast<uint32_t>(d.imm);
                pc += 4;
                break;

        case 0b1101111: // JAL
            if (d.rd) x[d.rd] = pc + 4;
            pc = pc + static_cast<uint32_t>(d.imm);
            break;

        case 0b1100111: { // JALR (funct3 == 000)
            uint32_t target = (x[d.rs1] + static_cast<uint32_t>(d.imm)) & ~1u;
            if (d.rd) x[d.rd] = pc + 4;
            pc = target;
            break;
        }

        case 0b1100011: { // B-type
            bool taken = false;
            uint32_t a = x[d.rs1], b = x[d.rs2];
            switch (d.funct3) {
                case 0b000: taken = (a == b);          break; // BEQ
                case 0b001: taken = (a != b);          break; // BNE
                case 0b100: taken = ((int32_t)a < (int32_t)b); break; // BLT
                case 0b101: taken = ((int32_t)a >= (int32_t)b); break; // BGE
                case 0b110: taken = (a < b);           break; // BLTU
                case 0b111: taken = (a >= b);          break; // BGEU
                default:
                    ;
                    state_.mcause = 2; // Illegal instruction
                    return;
            }
            pc = taken ? pc + static_cast<uint32_t>(d.imm) : pc + 4;
            break;
        }

        case 0b0000011: { // LOAD
            uint32_t addr = x[d.rs1] + static_cast<uint32_t>(d.imm);
            uint32_t val = 0;
            switch (d.funct3) {
                case 0b000: val = (int32_t)(int8_t)mem_.load8(addr);   break; // LB
                case 0b001: val = (int32_t)(int16_t)mem_.load16(addr); break; // LH
                case 0b010: val = mem_.load32(addr);                   break; // LW
                case 0b100: val = mem_.load8(addr);                    break; // LBU
                case 0b101: val = mem_.load16(addr);                   break; // LHU
                default:
                    ;
                    state_.mcause = 4; // Load address misaligned / illegal
                    state_.mtval = addr;
                    return;
            }
            if (d.rd) x[d.rd] = val;
            pc += 4;
            break;
        }

        case 0b0100011: { // STORE
            uint32_t addr = x[d.rs1] + static_cast<uint32_t>(d.imm);
            switch (d.funct3) {
                case 0b000: mem_.store8(addr,  x[d.rs2] & 0xFF);        break; // SB
                case 0b001: mem_.store16(addr, x[d.rs2] & 0xFFFF);      break; // SH
                case 0b010: mem_.store32(addr, x[d.rs2]);                break; // SW
                default:
                    ;
                    state_.mcause = 6;
                    state_.mtval = addr;
                    return;
            }
            pc += 4;
            break;
        }

        case 0b0010011: { // OP-IMM
            uint32_t src = x[d.rs1];
            int32_t  imm = d.imm;
            uint32_t result = 0;
            switch (d.funct3) {
                case 0b000: result = src + imm;                             break; // ADDI
                case 0b010: result = (int32_t)src < imm ? 1 : 0;          break; // SLTI
                case 0b011: result = src < (uint32_t)imm ? 1 : 0;         break; // SLTIU
                case 0b100: result = src ^ (uint32_t)imm;                  break; // XORI
                case 0b110: result = src | (uint32_t)imm;                  break; // ORI
                case 0b111: result = src & (uint32_t)imm;                  break; // ANDI
                case 0b001: result = src << (d.rs2 & 0x1F);               break; // SLLI (shamt in rs2)
                case 0b101:
                    if (d.funct7 == 0b0100000)
                        result = (int32_t)src >> (d.rs2 & 0x1F);          // SRAI
                    else
                        result = src >> (d.rs2 & 0x1F);                   // SRLI
                    break;
                default:
                    state_.mcause = 2;
                    return;
            }
            if (d.rd) x[d.rd] = result;
            pc += 4;
            break;
        }

        case 0b0110011: { // OP
            uint32_t a = x[d.rs1], b = x[d.rs2];
            uint32_t result = 0;
            switch (d.funct3) {
                case 0b000:
                    result = (d.funct7 == 0b0100000) ? a - b : a + b; // SUB / ADD
                    break;
                case 0b001: result = a << (b & 0x1F);                  break; // SLL
                case 0b010: result = (int32_t)a < (int32_t)b ? 1 : 0; break; // SLT
                case 0b011: result = a < b ? 1 : 0;                    break; // SLTU
                case 0b100: result = a ^ b;                             break; // XOR
                case 0b101:
                    result = (d.funct7 == 0b0100000)
                        ? (uint32_t)((int32_t)a >> (b & 0x1F))          // SRA
                        : a >> (b & 0x1F);                              // SRL
                    break;
                case 0b110: result = a | b;  break; // OR
                case 0b111: result = a & b;  break; // AND
                default:
                    raiseTrap(TrapCause::ILLEGAL_INSTRUCTION, d.raw);
                    return;
            }
            if (d.rd) x[d.rd] = result;
            pc += 4;
            break;
        }

        case 0b1110011: // SYSTEM
            if (d.imm == 0) {      // ECALL
                raiseTrap(TrapCause::ENVIRONMENT_CALL_FROM_MMODE, 0);
                // TODO: unify trap handling
                state_.running = false; // stop execution after trap
            } else if (d.imm == 1) { // EBREAK
                state_.running = false;
            }
            // CSRR* instructions would go here (funct3 != 0)
            pc += 4;
            break;

        case 0b0001111: // FENCE — no-op for a simple in-order emulator
            pc += 4;
            break;

        default:
            raiseTrap(TrapCause::ILLEGAL_INSTRUCTION, d.raw);
            return;
    }
    x[0] = 0;
}

uint32_t Interpreter::fetch_instruction(uint32_t vaddr) const {
    // RISC‑V instructions are always 4‑byte aligned
    uint32_t raw = state_.mem->load32(vaddr);
    return raw;
}

void Interpreter::handle_ecall() {
    // Minimal Linux syscall interface: a7 = syscall number, a0 = arg
    // Stop interpreter? For now throw or set a flag.
    throw std::runtime_error("ecall encountered – syscall not implemented");
}

// fetch-decode-execute
void Interpreter::step() {

    if (state_.trap_pending) {
        state_.trap_pending = false;
        return;
    }

    auto prev_pc = state_.pc;
    auto inst_word = fetch_instruction(state_.pc);

    if (state_.trap_pending) {
        state_.trap_pending = false;
        return;                          // instruction fetch fault taken
    }
    auto decoded = decode_raw_inst(inst_word, state_.pc);

    execute(decoded);

    if (state_.trap_pending) {
        state_.trap_pending = false;
        return;                          // illegal instruction / load/store fault taken
    }

}

void Interpreter::run() {
    while (state_.running) {
        step();
    }
}