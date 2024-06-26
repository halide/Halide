#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include <cmath>
#include <iostream>
#include <map>
#include <string>

#include "metadata_tester.function_info.h"
#include "metadata_tester.h"
#include "metadata_tester_ucon.function_info.h"
#include "metadata_tester_ucon.h"

namespace {

using namespace Halide::Runtime;

const int kSize = 32;

inline std::ostream &operator<<(std::ostream &o, const halide_type_t &type) {
    if (type.code == halide_type_uint && type.bits == 1) {
        o << "bool";
    } else {
        assert(type.code >= 0 && type.code <= 3);
        static const char *const names[4] = {"int", "uint", "float", "handle"};
        o << names[type.code] << (int)type.bits;
    }
    if (type.lanes > 1) {
        o << "x" << (int)type.lanes;
    }
    return o;
}

struct typed_scalar {
    halide_type_t type;
    halide_scalar_value_t value;

    typed_scalar(const halide_type_t &t, const halide_scalar_value_t &v)
        : type(t), value(v) {
    }

    bool operator==(const typed_scalar &that) const {
        if (this->type != that.type) {
            std::cerr << "Mismatched types\n";
            exit(1);
        }
        switch (type.element_of().as_u32()) {
        case halide_type_t(halide_type_float, 32).as_u32():
            return value.u.f32 == that.value.u.f32;
        case halide_type_t(halide_type_float, 64).as_u32():
            return value.u.f64 == that.value.u.f64;
        case halide_type_t(halide_type_int, 8).as_u32():
            return value.u.i8 == that.value.u.i8;
        case halide_type_t(halide_type_int, 16).as_u32():
            return value.u.i16 == that.value.u.i16;
        case halide_type_t(halide_type_int, 32).as_u32():
            return value.u.i32 == that.value.u.i32;
        case halide_type_t(halide_type_int, 64).as_u32():
            return value.u.i64 == that.value.u.i64;
        case halide_type_t(halide_type_uint, 1).as_u32():
            return value.u.b == that.value.u.b;
        case halide_type_t(halide_type_uint, 8).as_u32():
            return value.u.u8 == that.value.u.u8;
        case halide_type_t(halide_type_uint, 16).as_u32():
            return value.u.u16 == that.value.u.u16;
        case halide_type_t(halide_type_uint, 32).as_u32():
            return value.u.u32 == that.value.u.u32;
        case halide_type_t(halide_type_uint, 64).as_u32():
            return value.u.u64 == that.value.u.u64;
        case halide_type_t(halide_type_handle, 64).as_u32():
            return value.u.handle == that.value.u.handle;
        default:
            std::cerr << "Unsupported type\n";
            exit(1);
            break;
        }
    }

    bool operator!=(const typed_scalar &that) const {
        return !(*this == that);
    }

    friend std::ostream &operator<<(std::ostream &o, const typed_scalar &s) {
        switch (s.type.element_of().as_u32()) {
        case halide_type_t(halide_type_float, 32).as_u32():
            o << s.value.u.f32;
            break;
        case halide_type_t(halide_type_float, 64).as_u32():
            o << s.value.u.f64;
            break;
        case halide_type_t(halide_type_int, 8).as_u32():
            o << (int)s.value.u.i8;
            break;
        case halide_type_t(halide_type_int, 16).as_u32():
            o << s.value.u.i16;
            break;
        case halide_type_t(halide_type_int, 32).as_u32():
            o << s.value.u.i32;
            break;
        case halide_type_t(halide_type_int, 64).as_u32():
            o << s.value.u.i64;
            break;
        case halide_type_t(halide_type_uint, 1).as_u32():
            o << (s.value.u.b ? "true" : "false");
            break;
        case halide_type_t(halide_type_uint, 8).as_u32():
            o << (int)s.value.u.u8;
            break;
        case halide_type_t(halide_type_uint, 16).as_u32():
            o << s.value.u.u16;
            break;
        case halide_type_t(halide_type_uint, 32).as_u32():
            o << s.value.u.u32;
            break;
        case halide_type_t(halide_type_uint, 64).as_u32():
            o << s.value.u.u64;
            break;
        case halide_type_t(halide_type_handle, 64).as_u32():
            o << (uint64_t)s.value.u.handle;
            break;
        default:
            std::cerr << "Unsupported type\n";
            exit(1);
            break;
        }
        return o;
    }
};

#define EXPECT_EQ(exp, act)                                                                            \
    do {                                                                                               \
        if ((exp) != (act)) {                                                                          \
            std::cerr << #exp << " == " << #act << ": Expected " << exp << ", Actual " << act << "\n"; \
            exit(1);                                                                                   \
        }                                                                                              \
    } while (0);

#define EXPECT_STREQ(exp, act) \
    EXPECT_EQ(std::string(exp), std::string(act))

#define EXPECT_TYPE_AND_SCALAR_EQ(etype, exp, atype, act) \
    EXPECT_EQ(typed_scalar((etype), (exp)), typed_scalar((atype), (act)))

#define EXPECT_TYPE_AND_SCALAR_PTR_EQ(etype, exp_ptr, atype, act_ptr)            \
    do {                                                                         \
        if ((exp_ptr) && (act_ptr)) {                                            \
            EXPECT_TYPE_AND_SCALAR_EQ((etype), *(exp_ptr), (atype), *(act_ptr)); \
        } else if (!(exp_ptr) && !(act_ptr)) {                                   \
            EXPECT_EQ(etype, atype);                                             \
        } else {                                                                 \
            std::cerr << "One null, one non-null\n";                             \
            exit(1);                                                             \
        }                                                                        \
    } while (0);

void match_argument(const halide_filter_argument_t &e, const halide_filter_argument_t &a) {
    EXPECT_STREQ(e.name, a.name);
    EXPECT_EQ(e.dimensions, a.dimensions);
    EXPECT_EQ(e.kind, a.kind);
    EXPECT_EQ(e.type.code, a.type.code);
    EXPECT_EQ(e.type.bits, a.type.bits);
    EXPECT_EQ(e.dimensions, a.dimensions);
    EXPECT_TYPE_AND_SCALAR_PTR_EQ(e.type, e.scalar_def, a.type, a.scalar_def);
    EXPECT_TYPE_AND_SCALAR_PTR_EQ(e.type, e.scalar_min, a.type, a.scalar_min);
    EXPECT_TYPE_AND_SCALAR_PTR_EQ(e.type, e.scalar_max, a.type, a.scalar_max);
    EXPECT_TYPE_AND_SCALAR_PTR_EQ(e.type, e.scalar_estimate, a.type, a.scalar_estimate);

    // we want to treat
    //      buffer_estimates == nullptr
    // as equivalent to
    //      buffer_estimates == { nullptr, nullptr, ... nullptr },
    // so do a little pre-flighting to simplify things.

    int64_t const *const *eb = e.buffer_estimates;
    int64_t const *const *ab = a.buffer_estimates;
    int e_null = 0, a_null = 0;
    for (int i = 0; i < e.dimensions * 2; ++i) {
        if (eb && !eb[i]) e_null++;
        if (ab && !ab[i]) a_null++;
    }
    if (e_null == e.dimensions * 2) eb = nullptr;
    if (a_null == a.dimensions * 2) ab = nullptr;

    EXPECT_EQ((eb != nullptr), (ab != nullptr));
    if (eb && ab) {
        for (int i = 0; i < e.dimensions * 2; ++i) {
            EXPECT_TYPE_AND_SCALAR_PTR_EQ(
                halide_type_t(halide_type_int, 64),
                (const halide_scalar_value_t *)eb[i],
                halide_type_t(halide_type_int, 64),
                (const halide_scalar_value_t *)ab[i]);
        }
    }
}

template<typename Type>
Buffer<Type, 3> make_image() {
    Buffer<Type, 3> im(kSize, kSize, 3);
    for (int x = 0; x < kSize; x++) {
        for (int y = 0; y < kSize; y++) {
            for (int c = 0; c < 3; c++) {
                im(x, y, c) = static_cast<Type>(x + y + c);
            }
        }
    }
    return im;
}

void verify(const Buffer<uint8_t, 3> &input,
            const Buffer<float, 3> &output0,
            const Buffer<float, 3> &output1,
            const Buffer<float, 0> &output_scalar,
            const Buffer<float, 3> &output_array0,
            const Buffer<float, 3> &output_array1,
            const Buffer<float, 3> &untyped_output_buffer,
            const Buffer<float, 3> &tupled_output_buffer0,
            const Buffer<int32_t, 3> &tupled_output_buffer1) {
    if (output_scalar.dimensions() != 0) {
        std::cerr << "output_scalar should be zero-dimensional\n";
        exit(1);
    }
    if (output_scalar() != 1234.25f) {
        std::cerr << "output_scalar value is wrong (" << output_scalar() << "\n";
        exit(1);
    }
    for (int x = 0; x < kSize; x++) {
        for (int y = 0; y < kSize; y++) {
            for (int c = 0; c < 3; c++) {
                const float expected0 = static_cast<float>(input(x, y, c) + 1);
                const float expected1 = expected0 + 1;
                const float actual0 = output0(x, y, c);
                const float actual1 = output1(x, y, c);
                if (expected0 != actual0) {
                    std::cerr << "img0[" << x << "," << y << "," << c << "] = " << actual0 << ", expected " << expected0 << "\n";
                    exit(1);
                }
                if (expected1 != actual1) {
                    std::cerr << "img1[" << x << "," << y << "," << c << "] = " << actual1 << ", expected " << expected1 << "\n";
                    exit(1);
                }
                if (output_array0(x, y, c) != 1.5f) {
                    std::cerr << "output_array0[" << x << "," << y << "," << c << "] = " << output_array0(x, y, c) << ", expected " << 1.5f << "\n";
                    exit(1);
                }
                if (output_array1(x, y, c) != 3.0f) {
                    std::cerr << "output_array1[" << x << "," << y << "," << c << "] = " << output_array1(x, y, c) << ", expected " << 2.0f << "\n";
                    exit(1);
                }
                if (untyped_output_buffer(x, y, c) != expected1) {
                    std::cerr << "untyped_output_buffer[" << x << "," << y << "," << c << "] = " << untyped_output_buffer(x, y, c) << ", expected " << expected1 << "\n";
                    exit(1);
                }
                if (tupled_output_buffer0(x, y, c) != expected1) {
                    std::cerr << "tupled_output_buffer0[" << x << "," << y << "," << c << "] = " << tupled_output_buffer0(x, y, c) << ", expected " << expected1 << "\n";
                    exit(1);
                }
            }
        }
    }
}

template<typename T>
const halide_scalar_value_t *make_scalar(T v);

template<>
const halide_scalar_value_t *make_scalar(bool v) {
    halide_scalar_value_t *s = new halide_scalar_value_t();
    s->u.b = v;
    return s;
}

template<>
const halide_scalar_value_t *make_scalar(int8_t v) {
    halide_scalar_value_t *s = new halide_scalar_value_t();
    s->u.i8 = v;
    return s;
}

template<>
const halide_scalar_value_t *make_scalar(int16_t v) {
    halide_scalar_value_t *s = new halide_scalar_value_t();
    s->u.i16 = v;
    return s;
}

template<>
const halide_scalar_value_t *make_scalar(int32_t v) {
    halide_scalar_value_t *s = new halide_scalar_value_t();
    s->u.i32 = v;
    return s;
}

template<>
const halide_scalar_value_t *make_scalar(int64_t v) {
    halide_scalar_value_t *s = new halide_scalar_value_t();
    s->u.i64 = v;
    return s;
}

template<>
const halide_scalar_value_t *make_scalar(uint8_t v) {
    halide_scalar_value_t *s = new halide_scalar_value_t();
    s->u.u8 = v;
    return s;
}

template<>
const halide_scalar_value_t *make_scalar(uint16_t v) {
    halide_scalar_value_t *s = new halide_scalar_value_t();
    s->u.u16 = v;
    return s;
}

template<>
const halide_scalar_value_t *make_scalar(uint32_t v) {
    halide_scalar_value_t *s = new halide_scalar_value_t();
    s->u.u32 = v;
    return s;
}

template<>
const halide_scalar_value_t *make_scalar(uint64_t v) {
    halide_scalar_value_t *s = new halide_scalar_value_t();
    s->u.u64 = v;
    return s;
}

template<>
const halide_scalar_value_t *make_scalar(float v) {
    halide_scalar_value_t *s = new halide_scalar_value_t();
    s->u.f32 = v;
    return s;
}

template<>
const halide_scalar_value_t *make_scalar(double v) {
    halide_scalar_value_t *s = new halide_scalar_value_t();
    s->u.f64 = v;
    return s;
}

template<>
const halide_scalar_value_t *make_scalar(void *v) {
    halide_scalar_value_t *s = new halide_scalar_value_t();
    s->u.handle = v;
    return s;
}

constexpr int64_t NO_VALUE = (int64_t)0xFFFFFFFFFFFFFFFF;

int64_t const *const *make_int64_array(const std::vector<int64_t> &v) {
    using int64_t_ptr = int64_t *;
    int64_t **s = new int64_t_ptr[v.size()]();
    for (size_t i = 0; i < v.size(); ++i) {
        s[i] = (v[i] == NO_VALUE) ? nullptr : new int64_t(v[i]);
    }
    return s;
}

void check_metadata(const halide_filter_metadata_t &md, bool expect_ucon_at_0) {
    assert(md.version == halide_filter_metadata_t::VERSION);

    // target will vary depending on where we are testing, but probably
    // will contain "x86" or "arm".
    if (!strstr(md.target, "x86") &&
        !strstr(md.target, "powerpc") &&
        !strstr(md.target, "wasm") &&
        !strstr(md.target, "arm")) {
        std::cerr << "Expected x86 or arm, Actual " << md.target << "\n";
        exit(1);
    }

    // Not static, since we free make_scalar() results each time
    const halide_filter_argument_t kExpectedArguments[] = {
        {
            "__user_context",
            halide_argument_kind_input_scalar,
            0,
            halide_type_t(halide_type_handle, 64),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "input",
            halide_argument_kind_input_buffer,
            3,
            halide_type_t(halide_type_uint, 8),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            make_int64_array({10, 2592, 20, 1968, 0, 3}),
        },
        {
            "typed_input_buffer",
            halide_argument_kind_input_buffer,
            3,
            halide_type_t(halide_type_uint, 8),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            make_int64_array({0, 2592, 42, 1968, NO_VALUE, NO_VALUE}),
        },
        {
            "dim_only_input_buffer",
            halide_argument_kind_input_buffer,
            3,
            halide_type_t(halide_type_uint, 8),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "untyped_input_buffer",
            halide_argument_kind_input_buffer,
            3,
            halide_type_t(halide_type_uint, 8),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "no_default_value",
            halide_argument_kind_input_scalar,
            0,
            halide_type_t(halide_type_int, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "b",
            halide_argument_kind_input_scalar,
            0,
            halide_type_t(halide_type_uint, 1),
            make_scalar<bool>(true),
            nullptr,
            nullptr,
            make_scalar<bool>(false),
            nullptr,
        },
        {
            "i8",
            halide_argument_kind_input_scalar,
            0,
            halide_type_t(halide_type_int, 8),
            make_scalar<int8_t>(8),
            make_scalar<int8_t>(-8),
            make_scalar<int8_t>(127),
            make_scalar<int8_t>(3),
            nullptr,
        },
        {
            "i16",
            halide_argument_kind_input_scalar,
            0,
            halide_type_t(halide_type_int, 16),
            make_scalar<int16_t>(16),
            make_scalar<int16_t>(-16),
            make_scalar<int16_t>(127),
            nullptr,
            nullptr,
        },
        {
            "i32",
            halide_argument_kind_input_scalar,
            0,
            halide_type_t(halide_type_int, 32),
            make_scalar<int32_t>(32),
            make_scalar<int32_t>(-32),
            make_scalar<int32_t>(127),
            nullptr,
            nullptr,
        },
        {
            "i64",
            halide_argument_kind_input_scalar,
            0,
            halide_type_t(halide_type_int, 64),
            make_scalar<int64_t>(64),
            make_scalar<int64_t>(-64),
            make_scalar<int64_t>(127),
            nullptr,
            nullptr,
        },
        {
            "u8",
            halide_argument_kind_input_scalar,
            0,
            halide_type_t(halide_type_uint, 8),
            make_scalar<uint8_t>(80),
            make_scalar<uint8_t>(8),
            make_scalar<uint8_t>(255),
            nullptr,
            nullptr,
        },
        {
            "u16",
            halide_argument_kind_input_scalar,
            0,
            halide_type_t(halide_type_uint, 16),
            make_scalar<uint16_t>(160),
            make_scalar<uint16_t>(16),
            make_scalar<uint16_t>(2550),
            nullptr,
            nullptr,
        },
        {
            "u32",
            halide_argument_kind_input_scalar,
            0,
            halide_type_t(halide_type_uint, 32),
            make_scalar<uint32_t>(320),
            make_scalar<uint32_t>(32),
            make_scalar<uint32_t>(2550),
            nullptr,
            nullptr,
        },
        {
            "u64",
            halide_argument_kind_input_scalar,
            0,
            halide_type_t(halide_type_uint, 64),
            make_scalar<uint64_t>(640),
            make_scalar<uint64_t>(64),
            make_scalar<uint64_t>(2550),
            nullptr,
            nullptr,
        },
        {
            "f32",
            halide_argument_kind_input_scalar,
            0,
            halide_type_t(halide_type_float, 32),
            make_scalar<float>(32.1234f),
            make_scalar<float>(-3200.1234f),
            make_scalar<float>(3200.1234f),
            make_scalar<float>(48.5f),
            nullptr,
        },
        {
            "f64",
            halide_argument_kind_input_scalar,
            0,
            halide_type_t(halide_type_float, 64),
            make_scalar<double>(64.25),
            make_scalar<double>(-6400.25),
            make_scalar<double>(6400.25),
            nullptr,
            nullptr,
        },
        {
            "h",
            halide_argument_kind_input_scalar,
            0,
            halide_type_t(halide_type_handle, 64),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "input_not_nod",
            halide_argument_kind_input_buffer,
            3,
            halide_type_t(halide_type_uint, 8),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "input_nod",
            halide_argument_kind_input_buffer,
            3,
            halide_type_t(halide_type_uint, 8),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "input_not",
            halide_argument_kind_input_buffer,
            3,
            halide_type_t(halide_type_uint, 8),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array_input_0",
            halide_argument_kind_input_buffer,
            3,
            halide_type_t(halide_type_uint, 8),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array_input_1",
            halide_argument_kind_input_buffer,
            3,
            halide_type_t(halide_type_uint, 8),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array2_input_0",
            halide_argument_kind_input_buffer,
            3,
            halide_type_t(halide_type_uint, 8),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array2_input_1",
            halide_argument_kind_input_buffer,
            3,
            halide_type_t(halide_type_uint, 8),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array_i8_0",
            halide_argument_kind_input_scalar,
            0,
            halide_type_t(halide_type_int, 8),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array_i8_1",
            halide_argument_kind_input_scalar,
            0,
            halide_type_t(halide_type_int, 8),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array2_i8_0",
            halide_argument_kind_input_scalar,
            0,
            halide_type_t(halide_type_int, 8),
            nullptr,
            nullptr,
            nullptr,
            make_scalar<int8_t>(42),
            nullptr,
        },
        {
            "array2_i8_1",
            halide_argument_kind_input_scalar,
            0,
            halide_type_t(halide_type_int, 8),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array_i16_0",
            halide_argument_kind_input_scalar,
            0,
            halide_type_t(halide_type_int, 16),
            make_scalar<int16_t>(16),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array_i16_1",
            halide_argument_kind_input_scalar,
            0,
            halide_type_t(halide_type_int, 16),
            make_scalar<int16_t>(16),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array2_i16_0",
            halide_argument_kind_input_scalar,
            0,
            halide_type_t(halide_type_int, 16),
            make_scalar<int16_t>(16),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array2_i16_1",
            halide_argument_kind_input_scalar,
            0,
            halide_type_t(halide_type_int, 16),
            make_scalar<int16_t>(16),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array_i32_0",
            halide_argument_kind_input_scalar,
            0,
            halide_type_t(halide_type_int, 32),
            make_scalar<int32_t>(32),
            make_scalar<int32_t>(-32),
            make_scalar<int32_t>(127),
            nullptr,
            nullptr,
        },
        {
            "array_i32_1",
            halide_argument_kind_input_scalar,
            0,
            halide_type_t(halide_type_int, 32),
            make_scalar<int32_t>(32),
            make_scalar<int32_t>(-32),
            make_scalar<int32_t>(127),
            nullptr,
            nullptr,
        },
        {
            "array2_i32_0",
            halide_argument_kind_input_scalar,
            0,
            halide_type_t(halide_type_int, 32),
            make_scalar<int32_t>(32),
            make_scalar<int32_t>(-32),
            make_scalar<int32_t>(127),
            nullptr,
            nullptr,
        },
        {
            "array2_i32_1",
            halide_argument_kind_input_scalar,
            0,
            halide_type_t(halide_type_int, 32),
            make_scalar<int32_t>(32),
            make_scalar<int32_t>(-32),
            make_scalar<int32_t>(127),
            nullptr,
            nullptr,
        },
        {
            "array_h_0",
            halide_argument_kind_input_scalar,
            0,
            halide_type_t(halide_type_handle, 64),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array_h_1",
            halide_argument_kind_input_scalar,
            0,
            halide_type_t(halide_type_handle, 64),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "buffer_array_input1_0",
            halide_argument_kind_input_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "buffer_array_input1_1",
            halide_argument_kind_input_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "buffer_array_input2_0",
            halide_argument_kind_input_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "buffer_array_input2_1",
            halide_argument_kind_input_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "buffer_array_input3_0",
            halide_argument_kind_input_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "buffer_array_input3_1",
            halide_argument_kind_input_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "buffer_array_input4_0",
            halide_argument_kind_input_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "buffer_array_input4_1",
            halide_argument_kind_input_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "buffer_array_input5_0",
            halide_argument_kind_input_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "buffer_array_input5_1",
            halide_argument_kind_input_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "buffer_array_input6_0",
            halide_argument_kind_input_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "buffer_array_input6_1",
            halide_argument_kind_input_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "buffer_array_input7_0",
            halide_argument_kind_input_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "buffer_array_input7_1",
            halide_argument_kind_input_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "buffer_array_input8_0",
            halide_argument_kind_input_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "buffer_array_input8_1",
            halide_argument_kind_input_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "buffer_f16_typed",
            halide_argument_kind_input_buffer,
            1,
            halide_type_t(halide_type_float, 16),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "buffer_f16_untyped",
            halide_argument_kind_input_buffer,
            1,
            halide_type_t(halide_type_float, 16),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "untyped_scalar_input",
            halide_argument_kind_input_scalar,
            0,
            halide_type_t(halide_type_uint, 8),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "output.0",
            halide_argument_kind_output_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            make_int64_array({10, 2592, 20, 1968, 0, 3}),
        },
        {
            "output.1",
            halide_argument_kind_output_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            make_int64_array({10, 2592, 20, 1968, 0, 3}),
        },
        {
            "typed_output_buffer",
            halide_argument_kind_output_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            make_int64_array({10, 2592, 20, 1968, NO_VALUE, NO_VALUE}),
        },
        {
            "type_only_output_buffer",
            halide_argument_kind_output_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            make_int64_array({NO_VALUE, NO_VALUE, 0, 32, 0, 3}),
        },
        {
            "dim_only_output_buffer",
            halide_argument_kind_output_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "untyped_output_buffer",
            halide_argument_kind_output_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "tupled_output_buffer.0",
            halide_argument_kind_output_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "tupled_output_buffer.1",
            halide_argument_kind_output_buffer,
            3,
            halide_type_t(halide_type_int, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "output_scalar",
            halide_argument_kind_output_buffer,
            0,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array_outputs_0",
            halide_argument_kind_output_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array_outputs_1",
            halide_argument_kind_output_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array_outputs2_0.0",
            halide_argument_kind_output_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array_outputs2_0.1",
            halide_argument_kind_output_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array_outputs2_1.0",
            halide_argument_kind_output_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array_outputs2_1.1",
            halide_argument_kind_output_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array_outputs3_0",
            halide_argument_kind_output_buffer,
            0,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array_outputs3_1",
            halide_argument_kind_output_buffer,
            0,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array_outputs4_0",
            halide_argument_kind_output_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array_outputs4_1",
            halide_argument_kind_output_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array_outputs5_0",
            halide_argument_kind_output_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array_outputs5_1",
            halide_argument_kind_output_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array_outputs6_0",
            halide_argument_kind_output_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array_outputs6_1",
            halide_argument_kind_output_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array_outputs7_0",
            halide_argument_kind_output_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array_outputs7_1",
            halide_argument_kind_output_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array_outputs8_0",
            halide_argument_kind_output_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array_outputs8_1",
            halide_argument_kind_output_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array_outputs9_0",
            halide_argument_kind_output_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        },
        {
            "array_outputs9_1",
            halide_argument_kind_output_buffer,
            3,
            halide_type_t(halide_type_float, 32),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        }};
    const int kExpectedArgumentCount = (int)sizeof(kExpectedArguments) / sizeof(kExpectedArguments[0]);

    EXPECT_EQ(kExpectedArgumentCount - (expect_ucon_at_0 ? 0 : 1), md.num_arguments);

    const halide_filter_argument_t *expected = &kExpectedArguments[expect_ucon_at_0 ? 0 : 1];
    for (int i = 0; i < md.num_arguments; ++i) {
        // std::cout << "checking arg " << i << " " << md.arguments[i].name << "\n";
        match_argument(expected[i], md.arguments[i]);
    }

    for (int i = 0; i < kExpectedArgumentCount; ++i) {
        delete kExpectedArguments[i].scalar_def;
        delete kExpectedArguments[i].scalar_min;
        delete kExpectedArguments[i].scalar_max;
        delete kExpectedArguments[i].scalar_estimate;
        if (kExpectedArguments[i].buffer_estimates) {
            for (int j = 0; j < kExpectedArguments[i].dimensions * 2; ++j) {
                delete kExpectedArguments[i].buffer_estimates[j];
            }
            delete[] kExpectedArguments[i].buffer_estimates;
        }
    }
}

template<size_t arg_count>
constexpr size_t count_buffers(const std::array<::HalideFunctionInfo::ArgumentInfo, arg_count> args) {
    size_t buffer_count = 0;
    for (const auto a : args) {
        if (a.kind == HalideFunctionInfo::InputBuffer || a.kind == HalideFunctionInfo::OutputBuffer) {
            buffer_count++;
        }
    }
    return buffer_count;
}

// clang-format off
constexpr char arginfo_to_sigchar(::HalideFunctionInfo::ArgumentInfo arg) {
    if (arg.kind == HalideFunctionInfo::InputBuffer) {
        return '@';
    } else if (arg.kind == HalideFunctionInfo::OutputBuffer) {
        return '#';
    } else {

        #define HANDLE_CASE(CODE, BITS, CHAR)        \
            case halide_type_t(CODE, BITS).as_u32(): \
                return (CHAR);

        switch (arg.type.as_u32()) {
            HANDLE_CASE(halide_type_bfloat, 16, '!')
            HANDLE_CASE(halide_type_float, 16, 'e')
            HANDLE_CASE(halide_type_float, 32, 'f')
            HANDLE_CASE(halide_type_float, 64, 'd')
            HANDLE_CASE(halide_type_int, 8, 'b')
            HANDLE_CASE(halide_type_int, 16, 'h')
            HANDLE_CASE(halide_type_int, 32, 'i')
            HANDLE_CASE(halide_type_int, 64, 'q')
            HANDLE_CASE(halide_type_uint, 1, '?')
            HANDLE_CASE(halide_type_uint, 8, 'B')
            HANDLE_CASE(halide_type_uint, 16, 'H')
            HANDLE_CASE(halide_type_uint, 32, 'I')
            HANDLE_CASE(halide_type_uint, 64, 'Q')
            HANDLE_CASE(halide_type_handle, 64, 'P')
        }

        #undef HANDLE_CASE
    }

    // Shouldn't ever get here, but if we do, we'll fail at *compile* time
    abort();
}
// clang-format on

template<size_t arg_count, size_t... Indices>
constexpr std::array<char, arg_count + 1> compute_signature_impl(const std::array<::HalideFunctionInfo::ArgumentInfo, arg_count> args,
                                                                 std::index_sequence<Indices...>) {
    return {arginfo_to_sigchar(args[Indices])..., 0};
}

template<size_t arg_count>
constexpr auto compute_signature(const std::array<::HalideFunctionInfo::ArgumentInfo, arg_count> args) {
    return compute_signature_impl(args, std::make_index_sequence<arg_count>{});
}

}  // namespace

int main(int argc, char **argv) {
    void *user_context = nullptr;

    int result;

    Buffer<uint8_t, 3> input = make_image<uint8_t>();
    Buffer<float, 3> input_array[2] = {make_image<float>(), make_image<float>()};
    // TODO: there is no runtime type for float16, so we'll declare this using a halide_type_t
    const halide_type_t halide_type_float16 = halide_type_t(halide_type_float, 16, 1);
    Buffer<> input_f16 = Buffer<>(halide_type_float16, kSize);

    Buffer<float, 3> output0(kSize, kSize, 3);
    Buffer<float, 3> output1(kSize, kSize, 3);
    Buffer<float, 3> typed_output_buffer(kSize, kSize, 3);
    Buffer<float, 3> type_only_output_buffer(kSize, kSize, 3);
    Buffer<float, 3> dim_only_output_buffer(kSize, kSize, 3);
    Buffer<float, 3> untyped_output_buffer(kSize, kSize, 3);
    Buffer<float, 3> tupled_output_buffer0(kSize, kSize, 3);
    Buffer<int32_t, 3> tupled_output_buffer1(kSize, kSize, 3);
    Buffer<float, 0> output_scalar = Buffer<float>::make_scalar();
    Buffer<float, 3> output_array[2] = {{kSize, kSize, 3}, {kSize, kSize, 3}};
    Buffer<float, 3> output_array2[4] = {{kSize, kSize, 3}, {kSize, kSize, 3}, {kSize, kSize, 3}, {kSize, kSize, 3}};
    Buffer<float, 0> output_array3[2] = {Buffer<float>::make_scalar(), Buffer<float>::make_scalar()};
    Buffer<float, 3> output_array4[2] = {{kSize, kSize, 3}, {kSize, kSize, 3}};
    Buffer<float, 3> output_array5[2] = {{kSize, kSize, 3}, {kSize, kSize, 3}};
    Buffer<float, 3> output_array6[2] = {{kSize, kSize, 3}, {kSize, kSize, 3}};
    Buffer<float, 3> output_array7[2] = {{kSize, kSize, 3}, {kSize, kSize, 3}};
    Buffer<float, 3> output_array8[2] = {{kSize, kSize, 3}, {kSize, kSize, 3}};
    Buffer<float, 3> output_array9[2] = {{kSize, kSize, 3}, {kSize, kSize, 3}};

    result = metadata_tester(
        input,                                                                   // Input<Func>
        input,                                                                   // Input<Buffer<uint8_t>>
        input,                                                                   // Input<Buffer<>>(3)
        input,                                                                   // Input<Buffer<>>
        0,                                                                       // Input<i32>
        false,                                                                   // Input<bool>
        0,                                                                       // Input<i8>
        0,                                                                       // Input<i16>
        0,                                                                       // Input<i32>
        0,                                                                       // Input<i64>
        0,                                                                       // Input<u8>
        0,                                                                       // Input<u16>
        0,                                                                       // Input<u32>
        0,                                                                       // Input<u64>
        0.f,                                                                     // Input<float>
        0.0,                                                                     // Input<double>
        nullptr,                                                                 // Input<void*>
        input,                                                                   // Input<Func>
        input,                                                                   // Input<Func>
        input,                                                                   // Input<Func>
        input, input,                                                            // Input<Func[]>
        input, input,                                                            // Input<Func[2]>
        0, 0,                                                                    // Input<int8_t[]>
        0, 0,                                                                    // Input<int8_t[2]>
        0, 0,                                                                    // Input<int16_t[]>
        0, 0,                                                                    // Input<int16_t[2]>
        0, 0,                                                                    // Input<int32_t[]>
        0, 0,                                                                    // Input<int32_t[2]>
        nullptr, nullptr,                                                        // Input<void*[]>
        input_array[0], input_array[1],                                          // Input<Buffer<float>[2]>
        input_array[0], input_array[1],                                          // Input<Buffer<float>[2]>
        input_array[0], input_array[1],                                          // Input<Buffer<float>[2]>
        input_array[0], input_array[1],                                          // Input<Buffer<float>[2]>
        input_array[0], input_array[1],                                          // Input<Buffer<float>[2]>
        input_array[0], input_array[1],                                          // Input<Buffer<float>[2]>
        input_array[0], input_array[1],                                          // Input<Buffer<float>[2]>
        input_array[0], input_array[1],                                          // Input<Buffer<float>[2]>
        input_f16,                                                               // Input<Buffer<float16>>
        input_f16,                                                               // Input<Buffer<float16>>
        1,                                                                       // Input<u8>
        output0, output1,                                                        // Output<Tuple(Func, Func)>
        typed_output_buffer,                                                     // Output<Buffer<float>>(3)
        type_only_output_buffer,                                                 // Output<Buffer<float>>
        dim_only_output_buffer,                                                  // Output<Buffer<>>(3)
        untyped_output_buffer,                                                   // Output<Buffer<>>
        tupled_output_buffer0,                                                   // Output<Buffer<>> with tuple type
        tupled_output_buffer1,                                                   // Output<Buffer<>> with tuple type
        output_scalar,                                                           // Output<float>
        output_array[0], output_array[1],                                        // Output<Func[]>
        output_array2[0], output_array2[1], output_array2[2], output_array2[3],  // Output<Func[2]>(Tuple)
        output_array3[0], output_array3[1],                                      // Output<float[2]>
        output_array4[0], output_array4[1],                                      // Output<Buffer<float>[2]>
        output_array5[0], output_array5[1],                                      // Output<Buffer<float>[2]>
        output_array6[0], output_array6[1],                                      // Output<Buffer<float>[2]>
        output_array7[0], output_array7[1],                                      // Output<Buffer<float>[2]>
        output_array8[0], output_array8[1],                                      // Output<Buffer<float>[2]>
        output_array9[0], output_array9[1]                                       // Output<Buffer<float>[2]>
    );
    EXPECT_EQ(0, result);

    result = metadata_tester_ucon(
        user_context,
        input,                                                                   // Input<Func>
        input,                                                                   // Input<Buffer<uint8_t>>
        input,                                                                   // Input<Buffer<>>(3)
        input,                                                                   // Input<Buffer<>>
        0,                                                                       // Input<i32>
        false,                                                                   // Input<bool>
        0,                                                                       // Input<i8>
        0,                                                                       // Input<i16>
        0,                                                                       // Input<i32>
        0,                                                                       // Input<i64>
        0,                                                                       // Input<u8>
        0,                                                                       // Input<u16>
        0,                                                                       // Input<u32>
        0,                                                                       // Input<u64>
        0.f,                                                                     // Input<float>
        0.0,                                                                     // Input<double>
        nullptr,                                                                 // Input<void*>
        input,                                                                   // Input<Func>
        input,                                                                   // Input<Func>
        input,                                                                   // Input<Func>
        input, input,                                                            // Input<Func[]>
        input, input,                                                            // Input<Func[2]>
        0, 0,                                                                    // Input<int8_t[]>
        0, 0,                                                                    // Input<int8_t[2]>
        0, 0,                                                                    // Input<int16_t[]>
        0, 0,                                                                    // Input<int16_t[2]>
        0, 0,                                                                    // Input<int32_t[]>
        0, 0,                                                                    // Input<int32_t[2]>
        nullptr, nullptr,                                                        // Input<void*[]>
        input_array[0], input_array[1],                                          // Input<Buffer<float>[2]>
        input_array[0], input_array[1],                                          // Input<Buffer<float>[2]>
        input_array[0], input_array[1],                                          // Input<Buffer<float>[2]>
        input_array[0], input_array[1],                                          // Input<Buffer<float>[2]>
        input_array[0], input_array[1],                                          // Input<Buffer<float>[2]>
        input_array[0], input_array[1],                                          // Input<Buffer<float>[2]>
        input_array[0], input_array[1],                                          // Input<Buffer<float>[2]>
        input_array[0], input_array[1],                                          // Input<Buffer<float>[2]>
        input_f16,                                                               // Input<Buffer<float16>>
        input_f16,                                                               // Input<Buffer<float16>>
        1,                                                                       // Input<u8>
        output0, output1,                                                        // Output<Tuple(Func, Func)>
        typed_output_buffer,                                                     // Output<Buffer<float>>(3)
        type_only_output_buffer,                                                 // Output<Buffer<float>>
        dim_only_output_buffer,                                                  // Output<Buffer<>>(3)
        untyped_output_buffer,                                                   // Output<Buffer<>>
        tupled_output_buffer0,                                                   // Output<Buffer<>> with tuple type
        tupled_output_buffer1,                                                   // Output<Buffer<>> with tuple type
        output_scalar,                                                           // Output<float>
        output_array[0], output_array[1],                                        // Output<Func[]>
        output_array2[0], output_array2[1], output_array2[2], output_array2[3],  // Output<Func[2]>(Tuple)
        output_array3[0], output_array3[1],                                      // Output<float[2]>
        output_array4[0], output_array4[1],                                      // Output<Buffer<float>[2]>
        output_array5[0], output_array5[1],                                      // Output<Buffer<float>[2]>
        output_array6[0], output_array6[1],                                      // Output<Buffer<float>[2]>
        output_array7[0], output_array7[1],                                      // Output<Buffer<float>[2]>
        output_array8[0], output_array8[1],                                      // Output<Buffer<float>[2]>
        output_array9[0], output_array9[1]                                       // Output<Buffer<float>[2]>
    );
    EXPECT_EQ(0, result);

    verify(input, output0, output1, output_scalar, output_array[0], output_array[1], untyped_output_buffer, tupled_output_buffer0, tupled_output_buffer1);

    check_metadata(*metadata_tester_metadata(), false);
    if (strcmp(metadata_tester_metadata()->name, "metadata_tester")) {
        std::cerr << "Expected name metadata_tester, got " << metadata_tester_metadata()->name << "\n";
        exit(1);
    }

    check_metadata(*metadata_tester_ucon_metadata(), true);
    if (strcmp(metadata_tester_ucon_metadata()->name, "metadata_tester_ucon")) {
        std::cerr << "Expected name metadata_tester_ucon, got " << metadata_tester_ucon_metadata()->name << "\n";
        exit(1);
    }

    constexpr auto sig = compute_signature(metadata_tester_argument_info());
    if (strcmp(&sig[0], "@@@@i?bhiqBHIQfdP@@@@@@@bbbbhhhhiiiiPP@@@@@@@@@@@@@@@@@@B#############################")) {
        // NOLINTNEXTLINE(clang-diagnostic-unreachable-code)
        std::cerr << "Incorrect signature for metadata_tester_ucon_argument_info(): " << &sig[0] << "\n";
        exit(1);
    }

    constexpr auto usig = compute_signature(metadata_tester_ucon_argument_info());
    if (strcmp(&usig[0], "P@@@@i?bhiqBHIQfdP@@@@@@@bbbbhhhhiiiiPP@@@@@@@@@@@@@@@@@@B#############################")) {
        // NOLINTNEXTLINE(clang-diagnostic-unreachable-code)
        std::cerr << "Incorrect signature for metadata_tester_ucon_argument_info(): " << &usig[0] << "\n";
        exit(1);
    }

    constexpr size_t count = count_buffers(metadata_tester_argument_info());
    static_assert(count == 58, "Incorrect buffer count for metadata_tester_argument_info");

    constexpr size_t ucount = count_buffers(metadata_tester_ucon_argument_info());
    static_assert(ucount == 58, "Incorrect buffer count for metadata_tester_ucon_argument_info");

    std::cout << "Success!\n";
    return 0;
}
