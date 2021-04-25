#ifndef HANNK_ELEMENTWISE_PROGRAM_H
#define HANNK_ELEMENTWISE_PROGRAM_H

#include <cstdint>

namespace hannk {

// TODO: Try to make a wrapper for these programs so they are a little easier to write?
struct ElementwiseInstruction {
    // Every instruction can use two memory locations op1 and op2, and an immediate op3.
    // Memory location 0 is the constant 0.
    enum OpCode {
        // op3
        Const = 0,
        // saturating_add(load(op1), load(op2) + op3)
        Add,
        // saturating_sub(load(op1), load(op2) + op3)
        Sub,
        // rounding_mul_shift_right(load(op1), load(op2), op3)
        RoundingMulShift,
        // rounding_shift_right(load(op1), load(op2) + op3)
        RoundingShift,
        // min(load(op1), load(op2) + op3)
        Min,
        // max(load(op1), load(op2) + op3)
        Max,
        // logistic(load(op1) / 2^load(op2)) / 2^op3
        Logistic,
        // tanh(load(op1) / 2^load(op2)) / 2^op3
        Tanh,
        OpCodeCount,
    };
};

}  // namespace hannk

#endif  // HANNK_ELEMENTWISE_PROGRAM_H