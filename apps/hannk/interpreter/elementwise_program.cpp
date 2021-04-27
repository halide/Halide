#include "interpreter/elementwise_program.h"

#include <iomanip>

namespace hannk {

using Slot = ElementwiseProgram::Slot;

const char *ElementwiseProgram::to_string(OpCode op) {
    switch (op) {
    case Const: return "Const";
    case Add: return "Add";
    case Sub: return "Sub";
    case RoundingMulShift: return "RoundingMulShift";
    case RoundingShift: return "RoundingShift";
    case Min: return "Min";
    case Max: return "Max";
    case Logistic: return "Logistic";
    case Tanh: return "Tanh";
    default: return nullptr;
    }
}

Slot ElementwiseProgram::add_instruction(OpCode op, Slot op1, Slot op2, int op3) {
    assert(size < instructions.dim(1).extent());
    instructions(0, size) = op;
    instructions(1, size) = op1.index;
    instructions(2, size) = op2.index;
    instructions(3, size) = op3;
    // Slot 0 is the constant 0, instructions start after that.
    size++;
    return {size};
}

ElementwiseProgram::ElementwiseProgram(int *buffer, int buffer_size)
    : instructions(buffer, 4, buffer_size / 4) {
}

Halide::Runtime::Buffer<int> ElementwiseProgram::assemble(std::initializer_list<Slot> outputs) {
    // The outputs must be in order at the end.
    // TODO: The outputs might be in order already...
    for (Slot i : outputs) {
        add(i, 0);
    }
    return instructions.cropped(1, 0, size);
}

void ElementwiseProgram::disassemble(std::ostream &output) {
    for (int i = 0; i < size; i++) {
        output
            << std::setw(3) << std::right << i + 1 << " "
            << std::setw(20) << std::left << to_string((OpCode)instructions(0, i))
            << instructions(1, i) << " "
            << instructions(2, i) << " "
            << instructions(3, i) << "\n";
    }
}

Slot ElementwiseProgram::zero() {
    return {0};
}
Slot ElementwiseProgram::immediate(int value) {
    return add_instruction(Const, zero(), zero(), value);
}
Slot ElementwiseProgram::input(int index) {
    return {-index - 1};
}
Slot ElementwiseProgram::add(Slot a, Slot b, int add_b) {
    return add_instruction(Add, a, b, add_b);
}
Slot ElementwiseProgram::add(Slot a, int b) {
    return add(a, zero(), b);
}
Slot ElementwiseProgram::sub(Slot a, Slot b, int add_b) {
    return add_instruction(Sub, a, b, add_b);
}
Slot ElementwiseProgram::sub(Slot a, int b) {
    return sub(a, zero(), b);
}
Slot ElementwiseProgram::min(Slot a, Slot b, int add_b) {
    return add_instruction(Min, a, b, add_b);
}
Slot ElementwiseProgram::min(Slot a, int b) {
    return min(a, zero(), b);
}
Slot ElementwiseProgram::max(Slot a, Slot b, int add_b) {
    return add_instruction(Max, a, b, add_b);
}
Slot ElementwiseProgram::max(Slot a, int b) {
    return max(a, zero(), b);
}
Slot ElementwiseProgram::rounding_mul_shift(Slot a, Slot b, int shift) {
    return add_instruction(RoundingMulShift, a, b, shift);
}
Slot ElementwiseProgram::rounding_mul_shift(Slot a, int b, int shift) {
    return add_instruction(RoundingMulShift, a, immediate(b), shift);
}
Slot ElementwiseProgram::rounding_shift(Slot a, Slot shift, int extra_shift) {
    return add_instruction(RoundingShift, a, shift, extra_shift);
}
Slot ElementwiseProgram::rounding_shift(Slot a, int shift) {
    return rounding_shift(a, zero(), shift);
}
Slot ElementwiseProgram::logistic(int q, Slot a, Slot q_a) {
    return add_instruction(Logistic, a, q_a, q);
}
Slot ElementwiseProgram::logistic(int q, Slot a, int q_a) {
    return add_instruction(Logistic, a, immediate(q_a), q);
}
Slot ElementwiseProgram::tanh(int q, Slot a, Slot q_a) {
    return add_instruction(Tanh, a, q_a, q);
}
Slot ElementwiseProgram::tanh(int q, Slot a, int q_a) {
    return add_instruction(Tanh, a, immediate(q_a), q);
}

}  // namespace hannk
