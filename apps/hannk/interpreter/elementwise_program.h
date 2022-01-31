#ifndef HANNK_ELEMENTWISE_PROGRAM_H
#define HANNK_ELEMENTWISE_PROGRAM_H

#include <array>
#include <cstdint>
#include <initializer_list>

#include "HalideBuffer.h"

namespace hannk {

class ElementwiseAssembler {
public:
    // Every instruction can use two memory locations op1 and op2, and immediates op3 and op4.
    // Memory location 0 is the constant 0.
    enum OpCode {
        // Nothing
        Noop,
        // saturating_add(load(op1), load(op2) + op3)
        Add,
        // saturating_sub(load(op1), load(op2) + op3)
        Sub,
        // multiply_2x_high(load(op1), load(op2) + op3) + op4
        MulAdd,
        // rounding_mul_shift_right(load(op1), load(op2) + op3, op4)
        MulShift,
        // rounding_shift_right(load(op1), load(op2) + op3)
        Shift,
        // min(load(op1), load(op2) + op3)
        Min,
        // max(load(op1), load(op2) + op3)
        Max,
        // clamp(load(op1), op3, op4)
        Clamp,
        // logistic(load(op1) / 2^(load(op2) + op3)) * 2^op4
        Logistic,
        // tanh(load(op1) / 2^(load(op2) + op3)) * 2^op4
        Tanh,
        OpCodeCount,
    };
    static const char *to_string(OpCode op);

    enum {
        // The "width" of each instruction.
        InstructionSize = 5,
    };

    // Represents a scratch slot. Can't be implicitly converted to an integer to
    // avoid confusion between immediates and scratch references.
    struct Slot {
        int16_t index;
    };

private:
    Halide::Runtime::Buffer<int16_t, 2> instructions;
    int size = 0;

    Slot add_instruction(OpCode op, Slot op1, Slot op2, int16_t op3, int16_t op4 = 0);

public:
    // Create an assembler that builds programs in the given buffer.
    ElementwiseAssembler(int16_t *buffer, int buffer_size);
    template<size_t BufferSize>
    ElementwiseAssembler(std::array<int16_t, BufferSize> &buffer)
        : ElementwiseAssembler(buffer.data(), buffer.size()) {
    }

    // Assemble the current program. The return value is the buffer
    // fromt his assembler cropped to the region needed for the program.
    Halide::Runtime::Buffer<int16_t, 2> assemble(std::initializer_list<Slot> outputs);

    // Write the current program to the given stream.
    void disassemble(std::ostream &output);

    // Generate instructions in the program to implement the given operation.
    Slot constant(int16_t value);
    Slot input(int index);
    Slot add(Slot a, Slot b, int16_t add_b = 0);
    Slot add(Slot a, int16_t b);
    Slot sub(Slot a, Slot b, int16_t add_b = 0);
    Slot sub(Slot a, int16_t b);
    Slot mul(Slot a, Slot b, int16_t add_b = 0);
    Slot mul(Slot a, int16_t b);
    Slot mul_add(Slot a, Slot b, int16_t add);
    Slot mul_add(Slot a, int16_t b, int16_t add);
    Slot mul_shift(Slot a, Slot b, int16_t shift);
    Slot mul_shift(Slot a, int16_t b, int16_t shift);
    Slot shift(Slot a, Slot shift, int16_t extra_shift = 0);
    Slot shift(Slot a, int16_t shift);
    Slot min(Slot a, Slot b, int16_t add_b = 0);
    Slot min(Slot a, int16_t b);
    Slot max(Slot a, Slot b, int16_t add_b = 0);
    Slot max(Slot a, int16_t b);
    Slot clamp(Slot x, int16_t min, int16_t max);
    Slot logistic(int16_t q, Slot a, Slot q_a);
    Slot logistic(int16_t q, Slot a, int16_t q_a);
    Slot tanh(int16_t q, Slot a, Slot q_a);
    Slot tanh(int16_t q, Slot a, int16_t q_a);
};

}  // namespace hannk

#endif  // HANNK_ELEMENTWISE_PROGRAM_H