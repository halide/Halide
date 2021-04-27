#ifndef HANNK_ELEMENTWISE_PROGRAM_H
#define HANNK_ELEMENTWISE_PROGRAM_H

#include <array>
#include <cstdint>
#include <initializer_list>

#include "HalideBuffer.h"

namespace hannk {

class ElementwiseProgram {
public:
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
        // logistic(load(op1) / 2^load(op2)) * 2^op3
        Logistic,
        // tanh(load(op1) / 2^load(op2)) * 2^op3
        Tanh,
        OpCodeCount,
    };

    static const char *to_string(OpCode op);

    struct Slot {
        int index;
    };

private:
    Halide::Runtime::Buffer<int> instructions;
    int size = 0;

    Slot add_instruction(OpCode op, Slot op1, Slot op2, int op3);

public:
    ElementwiseProgram(int *buffer, int buffer_size);
    template<size_t BufferSize>
    ElementwiseProgram(std::array<int, BufferSize> &buffer)
        : ElementwiseProgram(buffer.data(), buffer.size()) {
    }

    Halide::Runtime::Buffer<int> assemble(std::initializer_list<Slot> outputs);

    void disassemble(std::ostream &output);

    Slot zero();
    Slot immediate(int value);
    Slot input(int index);
    Slot add(Slot a, Slot b, int add_b = 0);
    Slot add(Slot a, int b);
    Slot sub(Slot a, Slot b, int add_b = 0);
    Slot sub(Slot a, int b);
    Slot min(Slot a, Slot b, int add_b = 0);
    Slot min(Slot a, int b);
    Slot max(Slot a, Slot b, int add_b = 0);
    Slot max(Slot a, int b);
    Slot rounding_mul_shift(Slot a, Slot b, int shift);
    Slot rounding_mul_shift(Slot a, int b, int shift);
    Slot rounding_shift(Slot a, Slot shift, int extra_shift = 0);
    Slot rounding_shift(Slot a, int shift);
    Slot logistic(int q, Slot a, Slot q_a);
    Slot logistic(int q, Slot a, int q_a);
    Slot tanh(int q, Slot a, Slot q_a);
    Slot tanh(int q, Slot a, int q_a);
};

}  // namespace hannk

#endif  // HANNK_ELEMENTWISE_PROGRAM_H