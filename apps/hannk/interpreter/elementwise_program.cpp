#include "interpreter/elementwise_program.h"

#include <iomanip>

namespace hannk {

using Slot = ElementwiseProgram::Slot;

const char *ElementwiseProgram::to_string(OpCode op) {
    switch (op) {
    case Add: return "Add";
    case Sub: return "Sub";
    case RoundingMulShift: return "RoundingMulShift";
    case RoundingShift: return "RoundingShift";
    case Min: return "Min";
    case Max: return "Max";
    case Logistic: return "Logistic";
    case Tanh: return "Tanh";
    default: return "Unknown";
    }
}

namespace {

// Returns a mask with bit (1 << i) set to 1 if operand i is relevant for the op.
int get_opcode_operand_mask(ElementwiseProgram::OpCode op) {
    switch (op) {
    case ElementwiseProgram::Add: return 0x7;
    case ElementwiseProgram::Sub: return 0x7;
    case ElementwiseProgram::RoundingMulShift: return 0xf;
    case ElementwiseProgram::RoundingShift: return 0x7;
    case ElementwiseProgram::Min: return 0x7;
    case ElementwiseProgram::Max: return 0x7;
    case ElementwiseProgram::Logistic: return 0xf;
    case ElementwiseProgram::Tanh: return 0xf;
    default: return 0;
    }
}

}  // namespace

Slot ElementwiseProgram::add_instruction(OpCode op, Slot op1, Slot op2, int16_t op3, int16_t op4) {
    assert(size < instructions.dim(1).extent());
    instructions(0, size) = op;
    instructions(1, size) = op1.index;
    instructions(2, size) = op2.index;
    instructions(3, size) = op3;
    instructions(4, size) = op4;
    // Slot 0 is the constant 0, instructions start after that.
    size++;
    return {(int16_t)size};
}

ElementwiseProgram::ElementwiseProgram(int16_t *buffer, int buffer_size)
    : instructions(buffer, InstructionSize, buffer_size / InstructionSize) {
}

Halide::Runtime::Buffer<int16_t> ElementwiseProgram::assemble(std::initializer_list<Slot> outputs) {
    // The outputs must be in order at the end.
    bool in_order = true;
    int needed_index = size - (int)outputs.size() + 1;
    for (Slot i : outputs) {
        in_order = in_order && i.index == needed_index++;
    }
    if (!in_order) {
        for (Slot i : outputs) {
            add(i, 0);
        }
    }
    return instructions.cropped(1, 0, size);
}

void ElementwiseProgram::disassemble(std::ostream &output) {
    for (int i = 0; i < size; i++) {
        OpCode op = (OpCode)instructions(0, i);
        output
            << std::setw(3) << std::right << i + 1 << " "
            << std::setw(20) << std::left << to_string(op);

        int mask = get_opcode_operand_mask(op);
        for (int j = 0; j < 4; j++) {
            if (mask & (1 << j)) {
                int16_t operand = instructions(j + 1, i);
                if (j < 2) {
                    if (operand < 0) {
                        output << "in(" << 1 - operand << ") ";
                    } else if (operand > 0) {
                        output << "scratch(" << operand << ") ";
                    } else {
                        output << "0 ";
                    }
                } else {
                    output << operand << " ";
                }
            }
        }
        output << "\n";
    }
}

Slot ElementwiseProgram::constant(int16_t value) {
    if (value == 0) {
        return {0};
    } else {
        return add_instruction(Add, {0}, {0}, value);
    }
}

Slot ElementwiseProgram::input(int index) {
    return {(int16_t)(-index - 1)};
}

Slot ElementwiseProgram::add(Slot a, Slot b, int16_t add_b) {
    return add_instruction(Add, a, b, add_b);
}

Slot ElementwiseProgram::add(Slot a, int16_t b) {
    return add(a, constant(0), b);
}

Slot ElementwiseProgram::sub(Slot a, Slot b, int16_t add_b) {
    return add_instruction(Sub, a, b, add_b);
}

Slot ElementwiseProgram::sub(Slot a, int16_t b) {
    return sub(a, constant(0), b);
}

Slot ElementwiseProgram::min(Slot a, Slot b, int16_t add_b) {
    return add_instruction(Min, a, b, add_b);
}

Slot ElementwiseProgram::min(Slot a, int16_t b) {
    return min(a, constant(0), b);
}

Slot ElementwiseProgram::max(Slot a, Slot b, int16_t add_b) {
    return add_instruction(Max, a, b, add_b);
}

Slot ElementwiseProgram::max(Slot a, int16_t b) {
    return max(a, constant(0), b);
}

Slot ElementwiseProgram::rounding_mul_shift(Slot a, Slot b, int16_t shift) {
    return add_instruction(RoundingMulShift, a, b, 0, shift);
}

Slot ElementwiseProgram::rounding_mul_shift(Slot a, int16_t b, int16_t shift) {
    return add_instruction(RoundingMulShift, a, constant(0), b, shift);
}

Slot ElementwiseProgram::rounding_shift(Slot a, Slot shift, int16_t extra_shift) {
    return add_instruction(RoundingShift, a, shift, extra_shift);
}

Slot ElementwiseProgram::rounding_shift(Slot a, int16_t shift) {
    return rounding_shift(a, constant(0), shift);
}

Slot ElementwiseProgram::logistic(int16_t q, Slot a, Slot q_a) {
    return add_instruction(Logistic, a, q_a, 0, q);
}

Slot ElementwiseProgram::logistic(int16_t q, Slot a, int16_t q_a) {
    return add_instruction(Logistic, a, constant(0), q_a, q);
}

Slot ElementwiseProgram::tanh(int16_t q, Slot a, Slot q_a) {
    return add_instruction(Tanh, a, q_a, 0, q);
}

Slot ElementwiseProgram::tanh(int16_t q, Slot a, int16_t q_a) {
    return add_instruction(Tanh, a, constant(0), q_a, q);
}

}  // namespace hannk
