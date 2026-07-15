#include "src/builder/op_builder.hpp"
#include "src/ir/operand.hpp"
#include "src/ir/ir.hpp"

#include <stdexcept>
#include <algorithm>
#include <sstream>
#include <iomanip>

// RISC-V RV32I major opcodes (bits [6:0])
namespace Opcode {
    constexpr uint8_t LOAD   = 0b000'0011;
    constexpr uint8_t STORE  = 0b010'0011;
    constexpr uint8_t OP_IMM = 0b001'0011;
    constexpr uint8_t OP     = 0b011'0011;
    constexpr uint8_t BRANCH = 0b110'0011;
    constexpr uint8_t JAL    = 0b110'1111;
    constexpr uint8_t JALR   = 0b110'0111;
    constexpr uint8_t LUI    = 0b011'0111;
    constexpr uint8_t AUIPC  = 0b001'0111;
    constexpr uint8_t SYSTEM = 0b111'0011;
}

// funct3 codes reused across opcode groups
namespace Funct3 {
    // LOAD
    constexpr uint8_t LB  = 0x0, LH  = 0x1, LW  = 0x2, LBU = 0x4, LHU = 0x5;
    // STORE
    constexpr uint8_t SB  = 0x0, SH  = 0x1, SW  = 0x2;
    // OP-IMM / OP
    constexpr uint8_t ADD  = 0x0;  // SUB shares funct3 0x0 but funct7 differs
    constexpr uint8_t SLL  = 0x1;
    constexpr uint8_t SLT  = 0x2;
    constexpr uint8_t SLTU = 0x3;
    constexpr uint8_t XOR  = 0x4;
    constexpr uint8_t SRL  = 0x5;  // SRA shares funct3 0x5 but funct7 differs
    constexpr uint8_t OR   = 0x6;
    constexpr uint8_t AND  = 0x7;
    // BRANCH
    constexpr uint8_t BEQ  = 0x0, BNE = 0x1, BLT = 0x4,
                      BGE  = 0x5, BLTU = 0x6, BGEU = 0x7;
}

// Populate common I-type fields.
static void fillIType(IType& op, const DecodedInstruction& d) {
    op.rd     = Reg{d.rd};
    op.rs1    = Reg{d.rs1};
    op.imm    = Imm{d.imm};
    op.funct3 = d.funct3;
}

// Populate common R-type fields.
static void fillRType(RType& op, const DecodedInstruction& d) {
    op.rd     = Reg{d.rd};
    op.rs1    = Reg{d.rs1};
    op.rs2    = Reg{d.rs2};
    op.funct3 = d.funct3;
    op.funct7 = d.funct7;
}

// Populate common S-type fields.
static void fillSType(SType& op, const DecodedInstruction& d) {
    op.rs1    = Reg{d.rs1};
    op.rs2    = Reg{d.rs2};
    op.imm    = Imm{d.imm};
    op.funct3 = d.funct3;
}

// Populate common B-type fields.
static void fillBType(BType& op, const DecodedInstruction& d) {
    op.rs1    = Reg{d.rs1};
    op.rs2    = Reg{d.rs2};
    op.imm    = Imm{d.imm};   // already sign-extended PC-relative offset
    op.funct3 = d.funct3;
    op.rawTarget = d.addr + static_cast<uint32_t>(d.imm);
}

// Primary decode dispatch

std::unique_ptr<Operation> OpBuilder::decode(const DecodedInstruction& d) const
{
    using TryFn = std::unique_ptr<Operation> (OpBuilder::*)(const DecodedInstruction&) const;

    static constexpr TryFn kTryFns[] = {
        &OpBuilder::tryDecodeLoad,
        &OpBuilder::tryDecodeStore,
        &OpBuilder::tryDecodeOpImm,
        &OpBuilder::tryDecodeOp,
        &OpBuilder::tryDecodeBranch,
        &OpBuilder::tryDecodeJal,
        &OpBuilder::tryDecodeJalr,
        &OpBuilder::tryDecodeLui,
        &OpBuilder::tryDecodeAuipc,
        &OpBuilder::tryDecodeSystem,
    };

    for (auto fn : kTryFns) {
        if (auto op = (this->*fn)(d)) return op;
    }

    std::ostringstream msg;
    msg << "unknown or illegal instruction encoding: opcode=0x"
        << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(d.opcode);
    throw std::invalid_argument(msg.str());
}

// Lightweight queries

bool OpBuilder::isBranchOrJump(const DecodedInstruction& d) const
{
    return d.opcode == Opcode::BRANCH || d.opcode == Opcode::JAL || d.opcode == Opcode::JALR;
}

bool OpBuilder::hasStaticTarget(const DecodedInstruction& d) const
{
    // JALR uses rs1+imm — not statically resolvable from the encoding alone.
    return d.opcode != Opcode::JALR;
}

int32_t OpBuilder::staticOffset(const DecodedInstruction& d) const
{
    // The immediate is already sign-extended and represents a PC-relative byte offset.
    return d.imm;
}

// Per-opcode factories

std::unique_ptr<Operation> OpBuilder::tryDecodeLoad(const DecodedInstruction& d) const
{
    if (d.opcode != Opcode::LOAD) return nullptr;

    std::unique_ptr<IType> op;
    switch (d.funct3) {
    case Funct3::LB:  op = std::make_unique<LbOp>();  break;
    case Funct3::LH:  op = std::make_unique<LhOp>();  break;
    case Funct3::LW:  op = std::make_unique<LwOp>();  break;
    case Funct3::LBU: op = std::make_unique<LbuOp>(); break;
    case Funct3::LHU: op = std::make_unique<LhuOp>(); break;
    default:
        throw std::invalid_argument("unknown LOAD funct3");
    }
    fillIType(*op, d);
    return op;
}

std::unique_ptr<Operation> OpBuilder::tryDecodeStore(const DecodedInstruction& d) const
{
    if (d.opcode != Opcode::STORE) return nullptr;

    std::unique_ptr<SType> op;
    switch (d.funct3) {
    case Funct3::SB: op = std::make_unique<SbOp>(); break;
    case Funct3::SH: op = std::make_unique<ShOp>(); break;
    case Funct3::SW: op = std::make_unique<SwOp>(); break;
    default:
        throw std::invalid_argument("unknown STORE funct3");
    }
    fillSType(*op, d);
    return op;
}

std::unique_ptr<Operation> OpBuilder::tryDecodeOpImm(const DecodedInstruction& d) const
{
    if (d.opcode != Opcode::OP_IMM) return nullptr;

    std::unique_ptr<IType> op;
    switch (d.funct3) {
    case Funct3::ADD:  op = std::make_unique<AddiOp>();  break;
    case Funct3::SLT:  op = std::make_unique<SltiOp>();  break;
    case Funct3::SLTU: op = std::make_unique<SltiuOp>(); break;
    case Funct3::XOR:  op = std::make_unique<XoriOp>();  break;
    case Funct3::OR:   op = std::make_unique<OriOp>();   break;
    case Funct3::AND:  op = std::make_unique<AndiOp>();  break;
    case Funct3::SLL:  op = std::make_unique<SlliOp>();  break;
    case Funct3::SRL:
        // funct7 bit 30 distinguishes SRLI (arithmetic) from SRAI (logical)
        op = (d.funct7 & 0x20) ? std::unique_ptr<IType>(std::make_unique<SraiOp>())
                               : std::unique_ptr<IType>(std::make_unique<SrliOp>());
        break;
    default:
        throw std::invalid_argument("unknown OP-IMM funct3");
    }
    fillIType(*op, d);
    return op;
}

std::unique_ptr<Operation> OpBuilder::tryDecodeOp(const DecodedInstruction& d) const
{
    if (d.opcode != Opcode::OP) return nullptr;

    std::unique_ptr<RType> op;
    const bool alt = (d.funct7 & 0x20); // bit 30 selects SUB vs ADD, SRA vs SRL, etc.

    switch (d.funct3) {
    case Funct3::ADD:
        op = alt ? std::unique_ptr<RType>(std::make_unique<SubOp>())
                 : std::unique_ptr<RType>(std::make_unique<AddOp>());
        break;
    case Funct3::SLL:  op = std::make_unique<SllOp>();  break;
    case Funct3::SLT:  op = std::make_unique<SltOp>();  break;
    case Funct3::SLTU: op = std::make_unique<SltuOp>(); break;
    case Funct3::XOR:  op = std::make_unique<XorOp>();  break;
    case Funct3::SRL:  
        op = alt ? std::unique_ptr<RType>(std::make_unique<SraOp>())
                 : std::unique_ptr<RType>(std::make_unique<SrlOp>());
        break; 
    case Funct3::OR:   op = std::make_unique<OrOp>();   break;
    case Funct3::AND:  op = std::make_unique<AndOp>();  break;
    default:
        throw std::invalid_argument("unknown OP funct3");
    }
    fillRType(*op, d);
    return op;
}

std::unique_ptr<Operation> OpBuilder::tryDecodeBranch(const DecodedInstruction& d) const
{
    if (d.opcode != Opcode::BRANCH) return nullptr;

    std::unique_ptr<BType> op;
    switch (d.funct3) {
    case Funct3::BEQ:  op = std::make_unique<BeqOp>();  break;
    case Funct3::BNE:  op = std::make_unique<BneOp>();  break;
    case Funct3::BLT:  op = std::make_unique<BltOp>();  break;
    case Funct3::BGE:  op = std::make_unique<BgeOp>();  break;
    case Funct3::BLTU: op = std::make_unique<BltuOp>(); break;
    case Funct3::BGEU: op = std::make_unique<BgeuOp>(); break;
    default:
        throw std::invalid_argument("unknown BRANCH funct3");
    }
    fillBType(*op, d);
    return op;
}

std::unique_ptr<Operation> OpBuilder::tryDecodeJal(const DecodedInstruction& d) const
{
    if (d.opcode != Opcode::JAL) return nullptr;

    auto op = std::make_unique<JalOp>();
    op->rd  = Reg{d.rd};
    op->imm = Imm{d.imm};  // already sign-extended PC-relative offset
    op->rawTarget = d.addr + static_cast<uint32_t>(d.imm);
    return op;
}

std::unique_ptr<Operation> OpBuilder::tryDecodeJalr(const DecodedInstruction& d) const
{
    if (d.opcode != Opcode::JALR) return nullptr;

    auto op   = std::make_unique<JalrOp>();
    op->rd    = Reg{d.rd};
    op->rs1   = Reg{d.rs1};
    op->imm   = Imm{d.imm};  // already sign-extended
    op->funct3 = d.funct3;
    return op;
}

std::unique_ptr<Operation> OpBuilder::tryDecodeLui(const DecodedInstruction& d) const
{
    if (d.opcode != Opcode::LUI) return nullptr;

    auto op = std::make_unique<LuiOp>();
    op->rd  = Reg{d.rd};
    op->imm20 = Imm{d.imm};  // already sign-extended
    return op;
}

std::unique_ptr<Operation> OpBuilder::tryDecodeAuipc(const DecodedInstruction& d) const
{
    if (d.opcode != Opcode::AUIPC) return nullptr;

    auto op = std::make_unique<AuipcOp>();
    op->rd  = Reg{d.rd};
    op->imm20 = Imm{d.imm};  // already sign-extended
    return op;
}

std::unique_ptr<Operation> OpBuilder::tryDecodeSystem(const DecodedInstruction& d) const
{
    if (d.opcode != Opcode::SYSTEM) return nullptr;

    // Minimal: ECALL / EBREAK distinguished by imm bit 0.
    if (d.imm == 0) return std::make_unique<EcallOp>();
    if (d.imm == 1) return std::make_unique<EbreakOp>();

    throw std::invalid_argument("unknown SYSTEM encoding");
}