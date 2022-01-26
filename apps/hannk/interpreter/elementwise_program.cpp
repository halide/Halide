#include "interpreter/elementwise_program.h"

#include <iomanip>
#include <iostream>

namespace hannk {

using Slot = ElementwiseAssembler::Slot;

const char *ElementwiseAssembler::to_string(OpCode op) {
    switch (op) {
    case Noop:
        return "Noop";
    case Add:
        return "Add";
    case Sub:
        return "Sub";
    case MulAdd:
        return "MulAdd";
    case MulShift:
        return "MulShift";
    case Shift:
        return "Shift";
    case Min:
        return "Min";
    case Max:
        return "Max";
    case Clamp:
        return "Clamp";
    case Logistic:
        return "Logistic";
    case Tanh:
        return "Tanh";
    default:
        return "Unknown";
    }
}

namespace {

// Returns a mask with bit (1 << i) set to 1 if operand i is relevant for the op.
int get_opcode_operand_mask(ElementwiseAssembler::OpCode op) {
    switch (op) {
    case ElementwiseAssembler::Noop:
        return 0x0;
    case ElementwiseAssembler::Add:
        return 0x7;
    case ElementwiseAssembler::Sub:
        return 0x7;
    case ElementwiseAssembler::MulAdd:
        return 0xf;
    case ElementwiseAssembler::MulShift:
        return 0xf;
    case ElementwiseAssembler::Shift:
        return 0x7;
    case ElementwiseAssembler::Min:
        return 0x7;
    case ElementwiseAssembler::Max:
        return 0x7;
    case ElementwiseAssembler::Clamp:
        return 0xd;
    case ElementwiseAssembler::Logistic:
        return 0xf;
    case ElementwiseAssembler::Tanh:
        return 0xf;
    default:
        return 0;
    }
}

}  // namespace

Slot ElementwiseAssembler::add_instruction(OpCode op, Slot op1, Slot op2, int16_t op3, int16_t op4) {
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

ElementwiseAssembler::ElementwiseAssembler(int16_t *buffer, int buffer_size)
    : instructions(buffer, InstructionSize, buffer_size / InstructionSize) {
}

Halide::Runtime::Buffer<int16_t, 2> ElementwiseAssembler::assemble(std::initializer_list<Slot> outputs) {
    // Check if the outputs are in the right place already.
    bool in_order = true;
    int needed_index = size - (int)outputs.size() + 1;
    for (Slot i : outputs) {
        in_order = in_order && i.index == needed_index++;
    }
    // If not, we need to add some dummy instructions that
    // load the value into the right place. We do this by
    // adding 0 to the value.
    if (!in_order) {
        for (Slot i : outputs) {
            add(i, 0);
        }
    }
    return instructions.cropped(1, 0, size);
}

void ElementwiseAssembler::disassemble(std::ostream &output) {
    for (int i = 0; i < size; i++) {
        OpCode op = (OpCode)instructions(0, i);
        output
            << std::setw(3) << std::right << i + 1 << " "
            << std::setw(12) << std::left << to_string(op);

        int mask = get_opcode_operand_mask(op);
        for (int j = 0; j < 4; j++) {
            if (mask & (1 << j)) {
                int16_t operand = instructions(j + 1, i);
                if (j < 2) {
                    if (operand < 0) {
                        output << "input[" << -operand - 1 << "] ";
                    } else if (operand > 0) {
                        output << "scratch[" << operand << "] ";
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

Slot ElementwiseAssembler::constant(int16_t value) {
    if (value == 0) {
        return {0};
    } else {
        return add_instruction(Add, {0}, {0}, value);
    }
}

Slot ElementwiseAssembler::input(int index) {
    return {(int16_t)(-index - 1)};
}

Slot ElementwiseAssembler::add(Slot a, Slot b, int16_t add_b) {
    return add_instruction(Add, a, b, add_b);
}

Slot ElementwiseAssembler::add(Slot a, int16_t b) {
    return add(a, constant(0), b);
}

Slot ElementwiseAssembler::sub(Slot a, Slot b, int16_t add_b) {
    return add_instruction(Sub, a, b, add_b);
}

Slot ElementwiseAssembler::sub(Slot a, int16_t b) {
    return sub(a, constant(0), b);
}

Slot ElementwiseAssembler::mul(Slot a, Slot b, int16_t add_b) {
    return add_instruction(MulAdd, a, b, add_b);
}

Slot ElementwiseAssembler::mul(Slot a, int16_t b) {
    return mul(a, constant(0), b);
}

Slot ElementwiseAssembler::mul_add(Slot a, Slot b, int16_t add) {
    return add_instruction(MulAdd, a, b, 0, add);
}

Slot ElementwiseAssembler::mul_add(Slot a, int16_t b, int16_t add) {
    return add_instruction(MulAdd, a, constant(0), b, add);
}

Slot ElementwiseAssembler::mul_shift(Slot a, Slot b, int16_t shift) {
    return add_instruction(MulShift, a, b, 0, shift);
}

Slot ElementwiseAssembler::mul_shift(Slot a, int16_t b, int16_t shift) {
    return add_instruction(MulShift, a, constant(0), b, shift);
}

Slot ElementwiseAssembler::shift(Slot a, Slot b, int16_t extra_shift) {
    return add_instruction(Shift, a, b, extra_shift);
}

Slot ElementwiseAssembler::shift(Slot a, int16_t b) {
    return shift(a, constant(0), b);
}

Slot ElementwiseAssembler::min(Slot a, Slot b, int16_t add_b) {
    return add_instruction(Min, a, b, add_b);
}

Slot ElementwiseAssembler::min(Slot a, int16_t b) {
    return min(a, constant(0), b);
}

Slot ElementwiseAssembler::max(Slot a, Slot b, int16_t add_b) {
    return add_instruction(Max, a, b, add_b);
}

Slot ElementwiseAssembler::max(Slot a, int16_t b) {
    return max(a, constant(0), b);
}

Slot ElementwiseAssembler::clamp(Slot x, int16_t min, int16_t max) {
    // op2 is unused, seems best to just give it x again.
    return add_instruction(Clamp, x, x, min, max);
}

Slot ElementwiseAssembler::logistic(int16_t q, Slot a, Slot q_a) {
    return add_instruction(Logistic, a, q_a, 0, q);
}

Slot ElementwiseAssembler::logistic(int16_t q, Slot a, int16_t q_a) {
    return add_instruction(Logistic, a, constant(0), q_a, q);
}

Slot ElementwiseAssembler::tanh(int16_t q, Slot a, Slot q_a) {
    return add_instruction(Tanh, a, q_a, 0, q);
}

Slot ElementwiseAssembler::tanh(int16_t q, Slot a, int16_t q_a) {
    return add_instruction(Tanh, a, constant(0), q_a, q);
}

}  // namespace hannk
