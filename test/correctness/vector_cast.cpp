#include "Halide.h"
#include "halide_thread_pool.h"
#include "test_sharding.h"

#include <gtest/gtest.h>

using namespace Halide;

// TODO: factor this out into helpers header
template<typename...>
struct concat_types;

template<typename... Ts>
struct concat_types<::testing::Types<Ts...>> {
    using type = ::testing::Types<Ts...>;
};

template<typename... Ts, typename... Us, typename... Rest>
struct concat_types<::testing::Types<Ts...>, ::testing::Types<Us...>, Rest...> {
    using type = typename concat_types<::testing::Types<Ts..., Us...>, Rest...>::type;
};

template<typename... Ts>
using ConcatTypes = typename concat_types<Ts...>::type;

template<typename ListA, typename ListB>
struct combine_types;

template<typename... As, typename... Bs>
struct combine_types<::testing::Types<As...>, ::testing::Types<Bs...>> {
    template<typename A>
    using tuples_with = ::testing::Types<std::tuple<A, Bs>...>;

    using type = ConcatTypes<tuples_with<As>...>;
};

template<typename... Ts>
using CombineTypes = typename combine_types<Ts...>::type;

namespace {

template<typename T>
bool is_type_supported(int vec_width, const Target &target) {
    DeviceAPI device = DeviceAPI::Default_GPU;

    if (target.has_feature(Target::HVX)) {
        device = DeviceAPI::Hexagon;
    }
    if (target.has_feature(Target::Vulkan)) {
        if (type_of<T>() == Float(64)) {
            if ((target.os == Target::OSX || target.os == Target::IOS)) {
                return false;  // MoltenVK doesn't support Float64
            }
        }
    }
    return target.supports_type(type_of<T>().with_lanes(vec_width), device);
}

template<typename>
class VectorCastTest : public ::testing::Test {
protected:
    Target target{get_jit_target_from_environment()};
    // We only test power-of-two vector widths for now
    // The wasm jit is very slow, so shorten this test here.
    int vec_width_max{target.arch == Target::WebAssembly ? 16 : 64};
};
}  // namespace

using DataTypes = ::testing::Types<float, double, uint8_t, uint16_t, uint32_t, int8_t, int16_t, int32_t>;
using AllPairs = CombineTypes<DataTypes, DataTypes>;
TYPED_TEST_SUITE(VectorCastTest, AllPairs);
TYPED_TEST(VectorCastTest, CheckWidths) {
    using A = std::tuple_element_t<0, TypeParam>;
    using B = std::tuple_element_t<1, TypeParam>;
    for (int vec_width = 1; vec_width <= this->vec_width_max; vec_width *= 2) {
        if (!is_type_supported<A>(vec_width, this->target) || !is_type_supported<B>(vec_width, this->target)) {
            continue;
        }

        int W = 1024;
        int H = 1;

        Buffer<A> input(W, H);
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                // Casting from an out-of-range float to an int is UB, so
                // we have to pick our values a little carefully.
                input(x, y) = (A)((rand() & 0xffff) / 512.0);
            }
        }

        Var x, y;
        Func f;

        f(x, y) = cast<B>(input(x, y));

        if (this->target.has_gpu_feature()) {
            Var xo, xi;
            f.gpu_tile(x, xo, xi, 64);
        } else {
            if (this->target.has_feature(Target::HVX)) {
                // TODO: Non-native vector widths hang the compiler here.
                // f.hexagon();
            }
            if (vec_width > 1) {
                f.vectorize(x, vec_width);
            }
        }

        Buffer<B> output = f.realize({W, H});
        for (int yy = 0; yy < H; yy++) {
            for (int xx = 0; xx < W; xx++) {
                B expected = static_cast<B>(input(xx, yy));
                EXPECT_EQ(expected, output(xx, yy))
                    << type_of<A>() << " x " << vec_width << " -> " << type_of<B>() << " x " << vec_width << " failed. "
                    << "input(" << xx << ", " << yy << ") = " << static_cast<double>(input(xx, yy));
            }
        }
    }
}
