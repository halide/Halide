#include "HalideRuntime.h"

#include <math.h>
#include <stdio.h>
#include <iostream>

#include "metadata_tester.h"
#include "static_image.h"

const int kSize = 32;

#define EXPECT_EQ(exp, act)                                                                                  \
    do {                                                                                                     \
        if ((exp) != (act)) {                                                                                \
            fprintf(stderr, "%s == %s: Expected %f, Actual %f\n", #exp, #act, (double)(exp), (double)(act)); \
            exit(-1);                                                                                        \
        }                                                                                                    \
    } while (0);

#define EXPECT_STREQ(exp, act)                                                                       \
    do {                                                                                             \
        if (strcmp((exp), (act)) != 0) {                                                             \
            fprintf(stderr, "%s == %s: Expected \"%s\", Actual \"%s\"\n", #exp, #act, (exp), (act)); \
            exit(-1);                                                                                \
        }                                                                                            \
    } while (0);

#define EXPECT_SCALAR_UNION_EQ(code, bits, exp, act)                  \
    do {                                                              \
        if (!scalar_union_ptr_equal((code), (bits), (exp), (act))) {  \
            fprintf(stderr, "%s == %s: did not match\n", #exp, #act); \
            exit(-1);                                                 \
        }                                                             \
    } while (0);

bool scalar_union_equal(int32_t type_code,
                        int32_t type_bits,
                        const halide_scalar_value_t &e,
                        const halide_scalar_value_t &a) {
    switch (type_code) {
    case halide_type_int:
        switch (type_bits) {
        case 8:
            return e.i8 == a.i8;
        case 16:
            return e.i16 == a.i16;
        case 32:
            return e.i32 == a.i32;
        case 64:
            return e.i64 == a.i64;
        }
    case halide_type_uint:
        switch (type_bits) {
        case 8:
            return e.u8 == a.u8;
        case 16:
            return e.u16 == a.u16;
        case 32:
            return e.u32 == a.u32;
        case 64:
            return e.u64 == a.u64;
        }
    case halide_type_float:
        switch (type_bits) {
        case 32:
            return e.f32 == a.f32;
        case 64:
            return e.f64 == a.f64;
        }
    case halide_type_handle:
        return e.handle == a.handle;
    }
    fprintf(stderr, "Unsupported type %d or size %d\n", type_code, type_bits);
    exit(-1);
}

bool scalar_union_ptr_equal(int32_t type_code,
                            int32_t type_bits,
                            const halide_scalar_value_t *e,
                            const halide_scalar_value_t *a) {
    if (e) {
        if (a) {
            return scalar_union_equal(type_code, type_bits, *e, *a);
        } else {
            return false;
        }
    } else {
        if (a) {
            return false;
        } else {
            return true;
        }
    }
}

void match_argument(const halide_filter_argument_t &e, const halide_filter_argument_t &a) {
    EXPECT_STREQ(e.name, a.name);
    EXPECT_EQ(e.dimensions, a.dimensions);
    EXPECT_EQ(e.kind, a.kind);
    EXPECT_EQ(e.type_code, a.type_code);
    EXPECT_EQ(e.type_bits, a.type_bits);
    EXPECT_EQ(e.dimensions, a.dimensions);
    EXPECT_SCALAR_UNION_EQ(e.type_code, e.type_bits, e.def, a.def);
    EXPECT_SCALAR_UNION_EQ(e.type_code, e.type_bits, e.min, a.min);
    EXPECT_SCALAR_UNION_EQ(e.type_code, e.type_bits, e.max, a.max);
}

template <typename Type>
Image<Type> make_image() {
    Image<Type> im(kSize, kSize, 3);
    for (int x = 0; x < kSize; x++) {
        for (int y = 0; y < kSize; y++) {
            for (int c = 0; c < 3; c++) {
                im(x, y, c) = static_cast<Type>(x + y + c);
            }
        }
    }
    return im;
}

template <typename InputType, typename OutputType>
void verify(const Image<InputType> &input, const Image<OutputType> &output0, const Image<OutputType> &output1) {
    for (int x = 0; x < kSize; x++) {
        for (int y = 0; y < kSize; y++) {
            for (int c = 0; c < 3; c++) {
                const OutputType expected0 = static_cast<OutputType>(input(x, y, c));
                const float expected1 = expected0 + 1;
                const OutputType actual0 = output0(x, y, c);
                const float actual1 = output1(x, y, c);
                if (expected0 != actual0) {
                    fprintf(stderr, "img0[%d, %d, %d] = %f, expected %f\n", x, y, c, (double)actual0, (double)expected0);
                    exit(-1);
                }
                if (expected1 != actual1) {
                    fprintf(stderr, "img1[%d, %d, %d] = %f, expected %f\n", x, y, c, (double)actual1, (double)expected1);
                    exit(-1);
                }
            }
        }
    }
}

struct Scalar {
    halide_scalar_value_t scalar;

    template <typename T>
    const halide_scalar_value_t *init(T v);
};

template <>
const halide_scalar_value_t *Scalar::init(bool v) {
    scalar.b = v;
    return &scalar;
}

template <>
const halide_scalar_value_t *Scalar::init(int8_t v) {
    scalar.i8 = v;
    return &scalar;
}

template <>
const halide_scalar_value_t *Scalar::init(int16_t v) {
    scalar.i16 = v;
    return &scalar;
}

template <>
const halide_scalar_value_t *Scalar::init(int32_t v) {
    scalar.i32 = v;
    return &scalar;
}

template <>
const halide_scalar_value_t *Scalar::init(int64_t v) {
    scalar.i64 = v;
    return &scalar;
}

template <>
const halide_scalar_value_t *Scalar::init(uint8_t v) {
    scalar.u8 = v;
    return &scalar;
}

template <>
const halide_scalar_value_t *Scalar::init(uint16_t v) {
    scalar.u16 = v;
    return &scalar;
}

template <>
const halide_scalar_value_t *Scalar::init(uint32_t v) {
    scalar.u32 = v;
    return &scalar;
}

template <>
const halide_scalar_value_t *Scalar::init(uint64_t v) {
    scalar.u64 = v;
    return &scalar;
}

template <>
const halide_scalar_value_t *Scalar::init(float v) {
    scalar.f32 = v;
    return &scalar;
}

template <>
const halide_scalar_value_t *Scalar::init(double v) {
    scalar.f64 = v;
    return &scalar;
}

template <>
const halide_scalar_value_t *Scalar::init(void *v) {
    scalar.handle = v;
    return &scalar;
}

int main(int argc, char **argv) {

    const Image<uint8_t> input = make_image<uint8_t>();

    Image<float> output0(kSize, kSize, 3);
    Image<float> output1(kSize, kSize, 3);

    int result;

    result = metadata_tester(input, false, 0, 0, 0, 0, 0, 0, 0, 0, 0.f, 0.0, NULL, output0, output1);
    EXPECT_EQ(0, result);

    verify(input, output0, output1);

    const halide_filter_metadata_t &md = metadata_tester_metadata;

    // target will vary depending on where we are testing, but probably
    // will contain "x86" or "arm".
    if (!strstr(md.target, "x86") && !strstr(md.target, "arm")) {
        fprintf(stderr, "Expected x86 or arm, Actual %s\n", md.target);
        exit(-1);
    }

    static const halide_filter_argument_t kExpectedArguments[] = {
        {
          "input",
          halide_argument_kind_input_buffer,
          3,
          halide_type_uint,
          8,
          NULL,
          NULL,
          NULL,
        },
        {
          "b",
          halide_argument_kind_input_scalar,
          0,
          halide_type_uint,
          1,
          Scalar().init<bool>(true),
          NULL,
          NULL,
        },
        {
          "i8",
          halide_argument_kind_input_scalar,
          0,
          halide_type_int,
          8,
          Scalar().init<int8_t>(8),
          Scalar().init<int8_t>(-8),
          Scalar().init<int8_t>(127),
        },
        {
          "i16",
          halide_argument_kind_input_scalar,
          0,
          halide_type_int,
          16,
          Scalar().init<int16_t>(16),
          Scalar().init<int16_t>(-16),
          Scalar().init<int16_t>(127),
        },
        {
          "i32",
          halide_argument_kind_input_scalar,
          0,
          halide_type_int,
          32,
          Scalar().init<int32_t>(32),
          Scalar().init<int32_t>(-32),
          Scalar().init<int32_t>(127),
        },
        {
          "i64",
          halide_argument_kind_input_scalar,
          0,
          halide_type_int,
          64,
          Scalar().init<int64_t>(64),
          Scalar().init<int64_t>(-64),
          Scalar().init<int64_t>(127),
        },
        {
          "u8",
          halide_argument_kind_input_scalar,
          0,
          halide_type_uint,
          8,
          Scalar().init<uint8_t>(80),
          Scalar().init<uint8_t>(8),
          Scalar().init<uint8_t>(255),
        },
        {
          "u16",
          halide_argument_kind_input_scalar,
          0,
          halide_type_uint,
          16,
          Scalar().init<uint16_t>(160),
          Scalar().init<uint16_t>(16),
          Scalar().init<uint16_t>(2550),
        },
        {
          "u32",
          halide_argument_kind_input_scalar,
          0,
          halide_type_uint,
          32,
          Scalar().init<uint32_t>(320),
          Scalar().init<uint32_t>(32),
          Scalar().init<uint32_t>(2550),
        },
        {
          "u64",
          halide_argument_kind_input_scalar,
          0,
          halide_type_uint,
          64,
          Scalar().init<uint64_t>(640),
          Scalar().init<uint64_t>(64),
          Scalar().init<uint64_t>(2550),
        },
        {
          "f32",
          halide_argument_kind_input_scalar,
          0,
          halide_type_float,
          32,
          Scalar().init<float>(32.1234f),
          Scalar().init<float>(-3200.1234f),
          Scalar().init<float>(3200.1234f),
        },
        {
          "f64",
          halide_argument_kind_input_scalar,
          0,
          halide_type_float,
          64,
          Scalar().init<double>(64.25),
          Scalar().init<double>(-6400.25),
          Scalar().init<double>(6400.25),
        },
        {
          "h",
          halide_argument_kind_input_scalar,
          0,
          halide_type_handle,
          64,
          NULL,
          NULL,
          NULL,
        },
        {
          "output.0",
          halide_argument_kind_output_buffer,
          3,
          halide_type_float,
          32,
          NULL,
          NULL,
          NULL,
        },
        {
          "output.1",
          halide_argument_kind_output_buffer,
          3,
          halide_type_float,
          32,
          NULL,
          NULL,
          NULL,
        }
    };
    const int kExpectedArgumentCount = (int)sizeof(kExpectedArguments) / sizeof(kExpectedArguments[0]);

    EXPECT_EQ(kExpectedArgumentCount, md.num_arguments);

    for (int i = 0; i < kExpectedArgumentCount; ++i) {
        fprintf(stdout, "checking arg %d %s\n", i, md.arguments[i].name);
        match_argument(kExpectedArguments[i], md.arguments[i]);
    }
    printf("Success!\n");
    return 0;
}
