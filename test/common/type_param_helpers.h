#ifndef HALIDE_TEST_TYPE_PARAM_HELPERS_H
#define HALIDE_TEST_TYPE_PARAM_HELPERS_H

#include <gtest/gtest.h>
#include <tuple>

// Helper templates for creating type parameter combinations in Google Test

// Concatenates multiple ::testing::Types lists into a single list
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

// Combines two type lists into a cartesian product using std::tuple
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

#endif  // HALIDE_TEST_TYPE_PARAM_HELPERS_H