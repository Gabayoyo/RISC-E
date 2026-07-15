#pragma once

#include <cstdint>
#include <cstddef>
#include <stack>

class IRFunction;
class BasicBlock;

const int MAX_CALL_DEPTH = 64; // maximum depth of the call stack

struct InterpreterState {
    uint32_t xreg[32]; // General-purpose registers x0–x31
    
    // Flat memory modelled as a byte array (or sparse)
    uint8_t *memory;
    size_t   mem_size;
    
    // Call stack for function calls
    struct CallFrame {
        IRFunction     *callee;       // function we called (for debugging)
        IRFunction     *caller_func;  // function to return to
        BasicBlock   *caller_block; // block to resume in
        int           next_instr;   // index of instruction after the call
        int           dest_reg;     // register to write return value, -1 if void
    } call_stack[MAX_CALL_DEPTH];
    int call_sp;   // stack pointer into call_stack
    
    // Current execution context
    IRFunction   *current_func;
    BasicBlock *current_block;
    int         instr_index;       // index inside current_block->instructions
    
    BasicBlock *pred_block;        // predecessor block for phi nodes, NULL at function entry
    
    bool halted;
};