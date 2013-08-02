#include <Halide.h>
#include <stdio.h>
#include <limits>

using namespace Halide;

Var zero_val, one_val, weight;
Param<int> step;

template <typename weight_t>
double weight_type_scale() {
    if (std::numeric_limits<weight_t>::is_integer)
        return std::numeric_limits<weight_t>::max();
    else
      return static_cast<weight_t>(1.0);
}

template <typename value_t>
double conversion_rounding() {
    if (std::numeric_limits<value_t>::is_integer)
      return 0.5;
    else
      return 0.0;
}

template <typename value_t>
value_t convert_to_value(double interpolated) {
  return static_cast<value_t>(interpolated);
}

template <>
bool convert_to_value<bool>(double interpolated) {
  return interpolated >= 1; // Already has rounding added in
}

// Prevent iostream from printing 8-bit numbers as character constants.
template <typename t> struct promote_if_char { typedef t promoted;  };
template <> struct promote_if_char<signed char> { typedef int32_t promoted; };
template <> struct promote_if_char<unsigned char> { typedef int32_t promoted; };

template <typename value_t>
bool relatively_equal(value_t a, value_t b) {
    if (a == b) {
        return true;
    } else if (!std::numeric_limits<value_t>::is_integer) {
        value_t relative_error;

        // This test seems a bit high.
        if (fabs(b - a) < .00001)
          return true;

        if (fabs(a) > fabs(b))
            relative_error = fabs((b - a) / a);
        else
            relative_error = fabs((b - a) / b);

        if (relative_error < .0000002)
          return true;
        std::cerr << "relatively_equal failed for (" << a << ", " << b <<
          ") with relative error " << relative_error << std::endl;
    }
    return false;
}

template <typename value_t, typename weight_t>
void check_range(int32_t zero_min, int32_t zero_extent, value_t zero_offset, value_t zero_scale,
                 int32_t one_min, int32_t one_extent, value_t one_offset, value_t one_scale,
                 int32_t weight_min, int32_t weight_extent, weight_t weight_offset, weight_t weight_scale,
                 const char *name)
{
    // Stuff everything in Params as these can represent uint32_t where
    // that fails in converting to Expr is we just use the raw C++ variables.
    Param<value_t> zero_scale_p, zero_offset_p;
    zero_scale_p.set(zero_scale); zero_offset_p.set(zero_offset);
    Param<value_t> one_scale_p, one_offset_p;
    one_scale_p.set(one_scale); one_offset_p.set(one_offset);
    Param<weight_t> weight_scale_p, weight_offset_p;
    weight_scale_p.set(weight_scale); weight_offset_p.set(weight_offset);

    Func lerp_test("lerp_test");
    lerp_test(zero_val, one_val, weight) =
      lerp(cast<value_t>(zero_val * zero_scale_p + zero_offset_p),
           cast<value_t>(one_val * one_scale_p + one_offset_p),
           cast<weight_t>(weight * weight_scale_p + weight_offset_p));

    Buffer result(type_of<value_t>(), zero_extent, one_extent, weight_extent);
    result.raw_buffer()->min[0] = zero_min;
    result.raw_buffer()->min[1] = one_min;
    result.raw_buffer()->min[2] = weight_min;
    lerp_test.realize(result);

    const value_t *result_ptr = static_cast<value_t *>(result.host_ptr());
    buffer_t *buffer = result.raw_buffer();
    
    for (int32_t i = buffer->min[0]; i < buffer->min[0] + buffer->extent[0]; i++) {
        for (int32_t j = buffer->min[1]; j < buffer->min[1] + buffer->extent[1]; j++) {
            for (int32_t k = buffer->min[2]; k < buffer->min[2] + buffer->extent[2]; k++) {
                value_t zero_verify = (i * zero_scale + zero_offset);
                value_t one_verify =  (j * one_scale + one_offset);
                weight_t weight_verify = (weight_t)(k * weight_scale + weight_offset);
                double actual_weight = weight_verify / weight_type_scale<weight_t>();

                double verify_val_full = zero_verify * (1.0 - actual_weight) + one_verify * actual_weight;
                if (verify_val_full < 0)
                    verify_val_full -= conversion_rounding<value_t>();
                else
                    verify_val_full += conversion_rounding<value_t>();

                value_t verify_val = convert_to_value<value_t>(verify_val_full);
                value_t computed_val = result_ptr[(i - buffer->min[0]) * result.stride(0) +
                                                  (j - buffer->min[1]) * result.stride(1) +
                                                  (k - buffer->min[2]) * result.stride(2)];

                if (!relatively_equal(verify_val, computed_val)) {
                    std::cerr << "Expected " << (typename promote_if_char<value_t>::promoted)(verify_val) <<
                      " got " << (typename promote_if_char<value_t>::promoted)(computed_val) <<
                      " for lerp(" << (typename promote_if_char<value_t>::promoted)(zero_verify) << ", " <<
                      (typename promote_if_char<value_t>::promoted)(one_verify) << ", " <<
                      (typename promote_if_char<weight_t>::promoted)(weight_verify) <<
                      ") " << actual_weight << ". " << name << std::endl;
                    assert(false);
                }
            }
        }
    }
}

int main(int argc, char **argv) {
  // Test bool
  check_range<bool, uint8_t>(0, 2, 0, 1,
                             0, 2, 0, 1,
                             0, 256, 0, 1,
                             "<bool, uint8_t> exhaustive");

  // Exhaustively test 8-bit cases
  check_range<uint8_t, uint8_t>(0, 256, 0, 1,
                                0, 256, 0, 1,
                                0, 256, 0, 1,
                                "<uint8_t, uint8_t> exhaustive");
  check_range<int8_t, uint8_t>(0, 256, -128, 1,
                               0, 256, -128, 1,
                               0, 256, 0, 1,
                               "<int8_t, uint8_t> exhaustive");
  check_range<uint8_t, float>(0, 256, 0, 1,
                              0, 256, 0, 1,
                              0, 256, 0, 1/255.0f,
                              "<uint8_t, float> exhaustive");
  check_range<int8_t, float>(0, 256, -128, 1,
                             0, 256, -128, 1,
                             0, 256, 0, 1/255.0f,
                             "<int8_t, float> exhaustive");

  // Check all delta values for 16-bit, verify swapping arguments doesn't break
  check_range<uint16_t, uint16_t>(0, 65536, 0, 1,
                                  65535, 1, 0, 1,
                                  0, 257, 255, 1,
                                  "<uint16_t, uint16_t> all zero starts");
  check_range<uint16_t, uint16_t>(65535, 1, 0, 1,
                                  0, 65536, 0, 1,
                                  0, 257, 255, 1,
                                  "<uint16_t, uint16_t> all one starts");


  // Verify different bit sizes for value and weight types
  check_range<uint16_t, uint8_t>(0, 1, 0, 1,
                                 65535, 1, 0, 1,
                                 0, 255, 1, 1,
                                 "<uint16_t, uint8_t> zero, one uint8_t weight test");
  check_range<uint16_t, uint32_t>(0, 1, 0, 1,
                                 65535, 1, 0, 1,
                                 std::numeric_limits<int32_t>::min(), 257, 255 * 65535, 1,
                                 "<uint16_t, uint8_t> zero, one uint32_t weight test");
  check_range<uint32_t, uint8_t>(0, 1, 0, 1,
                                 1 << 31, 1, 0, 1,
                                 0, 255, 0, 1,
                                 "<uint32_t, uint8_t> weight test");
  check_range<uint32_t, uint16_t>(0, 1, 0, 1,
                                  1 << 31, 1, 0, 1,
                                  0, 65535, 0, 1,
                                 "<uint32_t, uint16_t> weight test");

  // Verify float weights with integer values
  check_range<uint16_t, float>(0, 1, 0, 1,
                               65535, 1, 0, 1,
                               0, 257, 255, 1/255.0f,
                               "<uint16_t, float> zero, one float weight test");

  check_range<int16_t, uint16_t>(0, 65536, -32768, 1,
                                 0, 1, 0, 1,
                                 0, 257, 255, 1,
                                 "<int16_t, uint16_t> all zero starts");

#if 0 // takes too long, difficult to test with uint32_t
  // Check all delta values for 32-bit, do it in signed arithmetic
  check_range<int32_t, uint32_t>(std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max(), 0, 1,
                                 1 << 31, 1, 0, 1,
                                 0, 1, 1 << 31, 1,
                                  "<uint32_t, uint32_t> all zero starts");
#endif

  check_range<float, float>(0, 100, 0, .01,
                            0, 100, 0, .01,
                            0, 100, 0, .01,
                            "<float, float> float values 0 to 1 by 1/100ths");

  check_range<float, float>(0, 100, -5, .1,
                            0, 100, 0, .1,
                            0, 100, 0, .1,
                            "<float, float> float values -5 to 5 by 1/100ths");

}
