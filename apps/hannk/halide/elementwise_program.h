#ifndef HANNK_ELEMENTWISE_PROGRAM_H
#define HANNK_ELEMENTWISE_PROGRAM_H

#include <array>
#include <cstdint>
#include <vector>

#include <HalideRuntime.h>

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

    struct Slot {
        int index;
    };

private:
    Halide::Runtime::Buffer<int> instructions;
    int size = 0;

    Slot add_instruction(OpCode op, Slot op1, Slot op2, int op3) {
        assert(size < instructions.dim(1).extent());
        instructions(0, size) = op;
        instructions(1, size) = op1.index;
        instructions(2, size) = op2.index;
        instructions(3, size) = op3;
        // Slot 0 is the constant 0, instructions start after that.
        size++;
        return {size};
    }

public:
    ElementwiseProgram(int *buffer, int buffer_size)
        : instructions(buffer, 4, buffer_size / 4) {
    }
    template<size_t BufferSize>
    ElementwiseProgram(std::array<int, BufferSize> &buffer)
        : ElementwiseProgram(buffer.data(), buffer.size()) {
    }

    Halide::Runtime::Buffer<int> assemble(Slot output) {
        // The output must be the last instruction.
        assert(output.index == size - 1);
        return instructions.cropped(1, 0, size);
    }

    Slot zero() {
        return {0};
    }
    Slot immediate(int value) {
        return add_instruction(Const, zero(), zero(), value);
    }
    Slot input(int index) {
        return {-index - 1};
    }
    Slot add(Slot a, Slot b, int add_b = 0) {
        return add_instruction(Add, a, b, add_b);
    }
    Slot add(Slot a, int b) {
        return add(a, zero(), b);
    }
    Slot sub(Slot a, Slot b, int add_b = 0) {
        return add_instruction(Sub, a, b, add_b);
    }
    Slot sub(Slot a, int b) {
        return sub(a, zero(), b);
    }
    Slot min(Slot a, Slot b, int add_b = 0) {
        return add_instruction(Min, a, b, add_b);
    }
    Slot min(Slot a, int b) {
        return min(a, zero(), b);
    }
    Slot max(Slot a, Slot b, int add_b = 0) {
        return add_instruction(Max, a, b, add_b);
    }
    Slot max(Slot a, int b) {
        return max(a, zero(), b);
    }
    Slot rounding_mul_shift(Slot a, Slot b, int shift) {
        return add_instruction(RoundingMulShift, a, b, shift);
    }
    Slot rounding_mul_shift(Slot a, int b, int shift) {
        return add_instruction(RoundingMulShift, a, immediate(b), shift);
    }
    Slot rounding_shift(Slot a, Slot shift, int extra_shift = 0) {
        return add_instruction(RoundingShift, a, shift, extra_shift);
    }
    Slot rounding_shift(Slot a, int shift) {
        return rounding_shift(a, zero(), shift);
    }
    Slot logistic(int q, Slot a, Slot q_a) {
        return add_instruction(Logistic, a, q_a, q);
    }
    Slot logistic(int q, Slot a, int q_a) {
        return add_instruction(Logistic, a, immediate(q_a), q);
    }
    Slot tanh(int q, Slot a, Slot q_a) {
        return add_instruction(Tanh, a, q_a, q);
    }
    Slot tanh(int q, Slot a, int q_a) {
        return add_instruction(Tanh, a, immediate(q_a), q);
    }
};

}  // namespace hannk

#endif  // HANNK_ELEMENTWISE_PROGRAM_H