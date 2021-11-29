#ifndef HANNK_CONSTANTS_H
#define HANNK_CONSTANTS_H

namespace hannk {

// After subtracing the zero point, we have 9 bits. We can shift
// up by a further 6 bits to 15 bits total to get more precision
// for the later operations.
constexpr int add_input_shift = 6;
constexpr int add_output_shift = 16;

constexpr int mul_input_shift = 6;

constexpr int softmax_input_shift = 6;

}  // namespace hannk

#endif  // HANNK_CONSTANTS_H
