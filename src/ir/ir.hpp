#pragma once

#include "src/ir/operation.hpp"
#include <string>
#include <array>
#include <set>

// ============================================================
// RISC-V format base classes
// ============================================================

// R-type: rd, rs1, rs2 + funct3/funct7
class RType : public Operation {
public:
    Reg rd, rs1, rs2;
    uint8_t funct3, funct7;

    RType(Traits t = Traits::None) : Operation(t) {}
};

// I-type: rd, rs1, immediate + funct3
class IType : public Operation {
public:
    Reg rd, rs1;
    Imm imm;
    uint8_t funct3;

    IType(Traits t = Traits::None) : Operation(t) {}
};

// S-type: rs1 (base), rs2 (source), store offset + funct3
class SType : public Operation {
public:
    Reg rs1, rs2;
    Imm imm;
    uint8_t funct3;

    SType(Traits t = Traits::Store) : Operation(t) {}
};

// B-type: rs1, rs2, branch offset + funct3
class BType : public Operation {
public:
    Reg rs1, rs2;
    Imm imm;
    uint8_t funct3;

    uint32_t rawTarget  = 0;    // absolute target address (set by tryDecodeBranch)
    int32_t  targetIndex = -1;  // flat array index (set by lowerToFlatOps)

    std::optional<uint32_t> targetAddr() const override { return rawTarget; }
    void patchTargetIndex(int32_t idx)         override { targetIndex = idx; }

    BType(Traits t = Traits::Branch) : Operation(t) {}
};

// U-type: rd, upper immediate (bits [31:12])
class UType : public Operation {
public:
    Reg rd;
    Imm imm20;

    UType(Traits t = Traits::None) : Operation(t) {}
};

// J-type: rd (link), jump offset
class JType : public Operation {
public:
    Reg rd;
    Imm imm;

    uint32_t rawTarget   = 0; // absolute target address (set by tryDecodeJal)
    int32_t  targetIndex = -1;

    std::optional<uint32_t> targetAddr() const override { return rawTarget; }
    void patchTargetIndex(int32_t idx)         override { targetIndex = idx; }

    JType(Traits t = Traits::Jump) : Operation(t) {}
};

// ============================================================
// I-type — integer register-immediate
// ============================================================

class AddiOp : public IType {
public:
    AddiOp() : IType() {}
    std::string mnemonic() const override { return "addi"; }
};

class SltiOp : public IType {
public:
    SltiOp() : IType() {}
    std::string mnemonic() const override { return "slti"; }
};

class SltiuOp : public IType {
public:
    SltiuOp() : IType() {}
    std::string mnemonic() const override { return "sltiu"; }
};

class XoriOp : public IType {
public:
    XoriOp() : IType() {}
    std::string mnemonic() const override { return "xori"; }
};

class OriOp : public IType {
public:
    OriOp() : IType() {}
    std::string mnemonic() const override { return "ori"; }
};

class AndiOp : public IType {
public:
    AndiOp() : IType() {}
    std::string mnemonic() const override { return "andi"; }
};

// Shifts: imm field holds the shift amount (shamt); funct7 distinguishes SRL/SRA
class SlliOp : public IType {
public:
    SlliOp() : IType() {}
    std::string mnemonic() const override { return "slli"; }
};

class SrliOp : public IType {
public:
    SrliOp() : IType() {}
    std::string mnemonic() const override { return "srli"; }
};

class SraiOp : public IType {
public:
    SraiOp() : IType() {}
    std::string mnemonic() const override { return "srai"; }
};


// ============================================================
// I-type — loads  (rd ← mem[rs1 + imm])
// ============================================================

class LbOp : public IType {
public:
    LbOp() : IType() {}
    std::string mnemonic() const override { return "lb"; }
};

class LhOp : public IType {
public:
    LhOp() : IType() {}
    std::string mnemonic() const override { return "lh"; }
};

class LwOp : public IType {
public:
    LwOp() : IType() {}
    std::string mnemonic() const override { return "lw"; }
};

class LbuOp : public IType {
public:
    LbuOp() : IType() {}
    std::string mnemonic() const override { return "lbu"; }
};

class LhuOp : public IType {
public:
    LhuOp() : IType() {}
    std::string mnemonic() const override { return "lhu"; }
};


// ============================================================
// I-type — JALR  (indirect jump-and-link)
// ============================================================
//
// Target = (rs1 + imm) & ~1.  rd receives PC+4 (the return address).
// Inherits IType because the encoding is I-format (rs1, rd, imm).

class JalrOp : public IType {
public:
    JalrOp() : IType(Traits::Jump) {}
    std::string mnemonic() const override { return "jalr"; }
};


//
// ============================================================
// I-type — SYSTEM  (environment calls / breakpoints)
// ============================================================
//
// funct3 == 0 for both; imm bit 0 distinguishes ECALL (0) from EBREAK (1).

class EcallOp : public IType {
public:
    EcallOp() : IType(Traits::System) {}
    std::string mnemonic() const override { return "ecall"; }
};

class EbreakOp : public IType {
public:
    EbreakOp() : IType(Traits::System) {}
    std::string mnemonic() const override { return "ebreak"; }
};


//
// ============================================================
// R-type — integer register-register
// ============================================================
//

class AddOp : public RType {
public:
    AddOp() : RType() {}
    std::string mnemonic() const override { return "add"; }
};

class SubOp : public RType {
public:
    SubOp() : RType() {}
    std::string mnemonic() const override { return "sub"; }
};

class SllOp : public RType {
public:
    SllOp() : RType() {}
    std::string mnemonic() const override { return "sll"; }
};

class SltOp : public RType {
public:
    SltOp() : RType() {}
    std::string mnemonic() const override { return "slt"; }
};

class SltuOp : public RType {
public:
    SltuOp() : RType() {}
    std::string mnemonic() const override { return "sltu"; }
};

class XorOp : public RType {
public:
    XorOp() : RType() {}
    std::string mnemonic() const override { return "xor"; }
};

class SrlOp : public RType {
public:
    SrlOp() : RType() {}
    std::string mnemonic() const override { return "srl"; }
};

class SraOp : public RType {
public:
    SraOp() : RType() {}
    std::string mnemonic() const override { return "sra"; }
};

class OrOp : public RType {
public:
    OrOp() : RType() {}
    std::string mnemonic() const override { return "or"; }
};

class AndOp : public RType {
public:
    AndOp() : RType() {}
    std::string mnemonic() const override { return "and"; }
};


//
// ============================================================
// S-type — stores  (mem[rs1 + imm] ← rs2)
// ============================================================
//

class SbOp : public SType {
public:
    SbOp() : SType() {}
    std::string mnemonic() const override { return "sb"; }
};

class ShOp : public SType {
public:
    ShOp() : SType() {}
    std::string mnemonic() const override { return "sh"; }
};

class SwOp : public SType {
public:
    SwOp() : SType() {}
    std::string mnemonic() const override { return "sw"; }
};


//
// ============================================================
// B-type — conditional branches
// ============================================================
//
// All branches: if (rs1 <cond> rs2) PC += imm  else  PC += 4

class BeqOp : public BType {
public:
    BeqOp() : BType() {}
    std::string mnemonic() const override { return "beq"; }
};

class BneOp : public BType {
public:
    BneOp() : BType() {}
    std::string mnemonic() const override { return "bne"; }
};

class BltOp : public BType {
public:
    BltOp() : BType() {}
    std::string mnemonic() const override { return "blt"; }
};

class BgeOp : public BType {
public:
    BgeOp() : BType() {}
    std::string mnemonic() const override { return "bge"; }
};

class BltuOp : public BType {
public:
    BltuOp() : BType() {}
    std::string mnemonic() const override { return "bltu"; }
};

class BgeuOp : public BType {
public:
    BgeuOp() : BType() {}
    std::string mnemonic() const override { return "bgeu"; }
};


//
// ============================================================
// U-type — upper-immediate  (imm20 occupies bits [31:12])
// ============================================================
//

// rd = imm20 << 12
class LuiOp : public UType {
public:
    LuiOp() : UType() {}
    std::string mnemonic() const override { return "lui"; }
};

// rd = PC + (imm20 << 12)
class AuipcOp : public UType {
public:
    AuipcOp() : UType() {}
    std::string mnemonic() const override { return "auipc"; }
};


//
// ============================================================
// J-type — unconditional jump-and-link
// ============================================================
//
// PC += imm;  rd = PC + 4  (rd == x0 discards the return address)
//

class JalOp : public JType {
public:
    JalOp() : JType() {}
    std::string mnemonic() const override { return "jal"; }
};