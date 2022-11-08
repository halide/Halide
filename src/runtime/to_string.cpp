#include "HalideRuntime.h"

extern "C" {

WEAK char *halide_string_to_string(char *dst, char *end, const char *arg) {
    if (dst >= end) {
        return dst;
    }
    if (!arg) {
        // Crashing on nullptr here is a big debugging time sink.
        arg = "<nullptr>";
    }
    while (true) {
        if (dst == end) {
            dst[-1] = 0;
            return dst;
        }
        *dst = *arg;
        if (*dst == 0) {
            return dst;
        }
        dst++;
        arg++;
    }
}

WEAK char *halide_uint64_to_string(char *dst, char *end, uint64_t arg, int min_digits) {
    // 32 is more than enough chars to contain any 64-bit int.
    char buf[32];
    buf[31] = 0;
    char *digits = buf + 30;

    for (int i = 0; i < min_digits || arg; i++) {
        uint64_t top = arg / 10;
        uint64_t bottom = arg - top * 10;
        *digits = bottom + '0';
        digits--;
        arg = top;
    }
    digits++;

    return halide_string_to_string(dst, end, digits);
}

WEAK char *halide_int64_to_string(char *dst, char *end, int64_t arg, int min_digits) {
    if (arg < 0 && dst < end) {
        *dst++ = '-';
        arg = -arg;
    }
    return halide_uint64_to_string(dst, end, arg, min_digits);
}

WEAK char *halide_double_to_string(char *dst, char *end, double arg, int scientific) {
    uint64_t bits = 0;
    memcpy(&bits, &arg, sizeof(double));

    uint64_t one = 1;
    uint64_t mantissa = bits & ((one << 52) - 1);
    int biased_exponent = (bits >> 52) & ((1 << 11) - 1);
    int negative = (bits >> 63);

    // Handle special values
    if (biased_exponent == 2047) {
        if (mantissa) {
            if (negative) {
                return halide_string_to_string(dst, end, "-nan");
            } else {
                return halide_string_to_string(dst, end, "nan");
            }
        } else {
            if (negative) {
                return halide_string_to_string(dst, end, "-inf");
            } else {
                return halide_string_to_string(dst, end, "inf");
            }
        }
    } else if (biased_exponent == 0 && mantissa == 0) {
        if (scientific) {
            if (negative) {
                return halide_string_to_string(dst, end, "-0.000000e+00");
            } else {
                return halide_string_to_string(dst, end, "0.000000e+00");
            }
        } else {
            if (negative) {
                return halide_string_to_string(dst, end, "-0.000000");
            } else {
                return halide_string_to_string(dst, end, "0.000000");
            }
        }
    }

    if (negative) {
        dst = halide_string_to_string(dst, end, "-");
        arg = -arg;
    }

    // The desired number of decimal places.
    const int decimal_places = 6;

    // 10 ^ decimal places
    const uint64_t scale = 1000000;

    // The number of bits in the mantissa of an IEEE double.
    const int mantissa_bits = 52;

    if (scientific) {

        // Compute base 10 exponent and normalize the number to within [1, 10)
        int exponent_base_10 = 0;
        while (arg < 1) {
            arg *= 10;
            exponent_base_10--;
        }

        while (arg >= 10) {
            arg /= 10;
            exponent_base_10++;
        }

        // Convert to fixed-point;
        uint64_t fixed = (uint64_t)(arg * scale + 0.5);  // NOLINT(bugprone-incorrect-roundings)
        uint64_t top_digit = fixed / scale;
        uint64_t other_digits = fixed - top_digit * scale;

        dst = halide_int64_to_string(dst, end, top_digit, 1);
        dst = halide_string_to_string(dst, end, ".");
        dst = halide_int64_to_string(dst, end, other_digits, decimal_places);

        if (exponent_base_10 >= 0) {
            dst = halide_string_to_string(dst, end, "e+");
        } else {
            dst = halide_string_to_string(dst, end, "e-");
            exponent_base_10 = -exponent_base_10;
        }
        dst = halide_int64_to_string(dst, end, exponent_base_10, 2);

    } else {
        // Denormals flush to zero in non-scientific mode. We've
        // already printed the sign.
        if (biased_exponent == 0) {
            return halide_double_to_string(dst, end, 0.0, false);
        }

        // Express it as an integer times a power of two.
        uint64_t n = mantissa + (one << mantissa_bits);
        int exponent = biased_exponent - 1023 - mantissa_bits;

        // Break it into integer and fractional parts.
        uint64_t integer_part = n;
        int integer_exponent = exponent;

        uint64_t fractional_part = 0;
        if (exponent < 0) {
            // There is a fractional component.

            double f;
            if (exponent < -mantissa_bits) {
                // There's no integer component.
                integer_part = 0;
                f = n;
            } else {
                integer_part >>= (-exponent);
                f = n - (integer_part << (-exponent));
            }
            integer_exponent = 0;

            // Construct 10^decimal_places * 2^exponent exactly
            // (recall exponent is negative).
            union {
                uint64_t bits;
                double as_double;
            } multiplier;
            multiplier.as_double = scale;
            multiplier.bits += (uint64_t)(exponent) << mantissa_bits;

            // Use it to get the first 6 digits of the fractional part
            // into the integer part.
            f = f * multiplier.as_double + 0.5;

            // Round-to-even, to match glibc.
            fractional_part = (uint64_t)f;
            if (fractional_part == f &&
                (fractional_part & 1)) {
                fractional_part--;
            }

            // If we rounded the fractional part up to the scale
            // factor, we'd better reattribute it to the integer part.
            if (fractional_part == scale) {
                fractional_part = 0;
                integer_part++;
            }
        }

        // The number is now:
        // integer_part * 2^integer_exponent + fractional_part * 2^exponent.

        // Convert integer_part to decimal, then repeatedly double it.

        // The largest double is 310 digits long.
        char buf[512];
        // Start 32 chars before the end of the scratch buffer and work
        // backwards to allow space for carried digits.
        char *int_part_ptr = buf + 512 - 32;
        char *buf_end = halide_int64_to_string(int_part_ptr, buf + 512, integer_part, 1);
        for (int i = 0; i < integer_exponent; i++) {
            // Double each digit, with ripple carry.
            int carry = 0;
            for (char *p = buf_end - 1; p != int_part_ptr - 1; p--) {
                char old_digit = *p - '0';
                char new_digit = old_digit * 2 + carry;
                if (new_digit > 9) {
                    new_digit -= 10;
                    carry = 1;
                } else {
                    carry = 0;
                }
                *p = new_digit + '0';
            }
            if (carry) {
                // There was a carry in the last digit. Add a new '1'
                // at the start.
                int_part_ptr--;
                *int_part_ptr = '1';
            }
        }
        dst = halide_string_to_string(dst, end, int_part_ptr);
        dst = halide_string_to_string(dst, end, ".");
        dst = halide_int64_to_string(dst, end, fractional_part, decimal_places);
    }

    return dst;
}

WEAK char *halide_pointer_to_string(char *dst, char *end, const void *arg) {
    const char *hex_digits = "0123456789abcdef";
    char buf[20] = {0};
    char *buf_ptr = buf + 18;
    uint64_t bits = (uint64_t)arg;
    for (int i = 0; i < 16; i++) {
        *buf_ptr-- = hex_digits[bits & 15];
        bits >>= 4;
        if (!bits) {
            break;
        }
    }
    *buf_ptr-- = 'x';
    *buf_ptr = '0';
    return halide_string_to_string(dst, end, buf_ptr);
}

WEAK char *halide_type_to_string(char *dst, char *end, const halide_type_t *t) {
    const char *code_name = nullptr;
    switch (t->code) {
    case halide_type_int:
        code_name = "int";
        break;
    case halide_type_uint:
        code_name = "uint";
        break;
    case halide_type_float:
        code_name = "float";
        break;
    case halide_type_handle:
        code_name = "handle";
        break;
    case halide_type_bfloat:
        code_name = "bfloat";
        break;
    default:
        code_name = "bad_type_code";
        break;
    }
    dst = halide_string_to_string(dst, end, code_name);
    dst = halide_uint64_to_string(dst, end, t->bits, 1);
    if (t->lanes != 1) {
        dst = halide_string_to_string(dst, end, "x");
        dst = halide_uint64_to_string(dst, end, t->lanes, 1);
    }
    return dst;
}

WEAK char *halide_buffer_to_string(char *dst, char *end, const halide_buffer_t *buf) {
    if (buf == nullptr) {
        return halide_string_to_string(dst, end, "nullptr");
    }
    dst = halide_string_to_string(dst, end, "buffer(");
    dst = halide_uint64_to_string(dst, end, buf->device, 1);
    dst = halide_string_to_string(dst, end, ", ");
    dst = halide_pointer_to_string(dst, end, buf->device_interface);
    dst = halide_string_to_string(dst, end, ", ");
    dst = halide_pointer_to_string(dst, end, buf->host);
    dst = halide_string_to_string(dst, end, ", ");
    dst = halide_uint64_to_string(dst, end, buf->flags, 1);
    dst = halide_string_to_string(dst, end, ", ");
    dst = halide_type_to_string(dst, end, &(buf->type));
    for (int i = 0; i < buf->dimensions; i++) {
        dst = halide_string_to_string(dst, end, ", {");
        dst = halide_int64_to_string(dst, end, buf->dim[i].min, 1);
        dst = halide_string_to_string(dst, end, ", ");
        dst = halide_int64_to_string(dst, end, buf->dim[i].extent, 1);
        dst = halide_string_to_string(dst, end, ", ");
        dst = halide_int64_to_string(dst, end, buf->dim[i].stride, 1);
        dst = halide_string_to_string(dst, end, "}");
    }
    dst = halide_string_to_string(dst, end, ")");
    return dst;
}
}
