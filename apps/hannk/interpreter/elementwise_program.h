#ifndef HANNK_ELEMENTWISE_PROGRAM_H
#define HANNK_ELEMENTWISE_PROGRAM_H

#include <array>
#include <cstdint>
#include <initializer_list>

#include "HalideBuffer.h"

namespace hannk {

class ElementwiseProgram {
public:
    // Every instruction can use two memory locations op1 and op2, and immediates op3 and op4.
    // Memory location 0 is the constant 0.
    enum OpCode {
        // saturating_add(load(op1), load(op2) + op3)
        Add,
        // saturating_sub(load(op1), load(op2) + op3)
        Sub,
        // rounding_mul_shift_right(load(op1), load(op2) + op3, op4)
        RoundingMulShift,
        // rounding_shift_right(load(op1), load(op2) + op3)
        RoundingShift,
        // min(load(op1), load(op2) + op3)
        Min,
        // max(load(op1), load(op2) + op3)
        Max,
        // logistic(load(op1) / 2^(load(op2) + op3)) * 2^op4
        Logistic,
        // tanh(load(op1) / 2^(load(op2) + op3)) * 2^op4
        Tanh,
        OpCodeCount,
    };

    enum {
        // The "width" of each instruction.
        InstructionSize = 5,
    };

    static const char *to_string(OpCode op);

    struct Slot {
        int16_t index;
    };

private:
    Halide::Runtime::Buffer<int16_t> instructions;
    int size = 0;

    Slot add_instruction(OpCode op, Slot op1, Slot op2, int16_t op3, int16_t op4 = 0);

public:
    ElementwiseProgram(int16_t *buffer, int buffer_size);
    template<size_t BufferSize>
    ElementwiseProgram(std::array<int16_t, BufferSize> &buffer)
        : ElementwiseProgram(buffer.data(), buffer.size()) {
    }

    Halide::Runtime::Buffer<int16_t> assemble(std::initializer_list<Slot> outputs);

    void disassemble(std::ostream &output);

    Slot constant(int16_t value);
    Slot input(int index);
    Slot add(Slot a, Slot b, int16_t add_b = 0);
    Slot add(Slot a, int16_t b);
    Slot sub(Slot a, Slot b, int16_t add_b = 0);
    Slot sub(Slot a, int16_t b);
    Slot min(Slot a, Slot b, int16_t add_b = 0);
    Slot min(Slot a, int16_t b);
    Slot max(Slot a, Slot b, int16_t add_b = 0);
    Slot max(Slot a, int16_t b);
    Slot rounding_mul_shift(Slot a, Slot b, int16_t shift);
    Slot rounding_mul_shift(Slot a, int16_t b, int16_t shift);
    Slot rounding_shift(Slot a, Slot shift, int16_t extra_shift = 0);
    Slot rounding_shift(Slot a, int16_t shift);
    Slot logistic(int16_t q, Slot a, Slot q_a);
    Slot logistic(int16_t q, Slot a, int16_t q_a);
    Slot tanh(int16_t q, Slot a, Slot q_a);
    Slot tanh(int16_t q, Slot a, int16_t q_a);
};

}  // namespace hannk

#endif  // HANNK_ELEMENTWISE_PROGRAM_H