#include <set>

#include "test.h"
#include "Tiling.h"

using namespace Halide;
using namespace Halide::Internal;
using namespace Halide::Internal::Autoscheduler;

using tilings_t = vector<vector<int64_t>>;

std::string to_string(const tilings_t& tilings) {
    std::ostringstream s;
    s << "[\n";
    bool first_tiling = true;
    for (const auto& t : tilings) {
        if (!first_tiling) {
            s << ",\n";
        }
        s << "  [";
        bool first = true;
        for (const auto& x : t) {
            if (!first) {
                s << ", ";
            }
            s << x;
            first = false;
        }
        s << "]";
        first_tiling = false;
    }
    s << "\n]";

    return s.str();
}

template <>
void Halide::Internal::Autoscheduler::expect_eq(int line, const tilings_t& expected, const tilings_t& actual) {
    expect_eq(line, to_string(expected), to_string(actual));
}

void test_serial_tilings() {
    {
        // Don't split small, odd extents
        vector<int64_t> s;
        s.push_back(3);

        vector<vector<int64_t>> expected;
        expected.push_back({3});

        vector<vector<int64_t>> actual = generate_serial_tilings(s, 0, 0, 0, {}, false, true);

        EXPECT_EQ(expected, actual);

        s.back() = 5;
        expected.back().back() = 5;
        actual = generate_serial_tilings(s, 0, 0, 0, {}, false, true);
        EXPECT_EQ(expected, actual);

        s.back() = 7;
        expected.back().back() = 7;
        actual = generate_serial_tilings(s, 0, 0, 0, {}, false, true);
        EXPECT_EQ(expected, actual);

        // If 'allow_inner_ones' is false, don't split
        actual = generate_serial_tilings(s, 0, 0, 0, {}, false, false);
        expected.clear();
        EXPECT_EQ(expected, actual);
    }

    {
        vector<int64_t> s;
        s.push_back(8);

        vector<vector<int64_t>> expected;
        expected.push_back({8});
        expected.push_back({4});
        expected.push_back({2});

        vector<vector<int64_t>> actual = generate_serial_tilings(s, 0, 0, 0, {}, false, true);

        EXPECT_EQ(expected, actual);
    }

    {
        vector<int64_t> s;
        s.push_back(8);

        vector<vector<int64_t>> expected;
        // If 'filter_small_outer_extents' is true, don't split small extents
        vector<vector<int64_t>> actual = generate_serial_tilings(s, 0, 0, 0, {}, true, true);

        EXPECT_EQ(expected, actual);
    }

    {
        vector<int64_t> s;
        s.push_back(8);

        vector<vector<int64_t>> expected;
        expected.push_back({8});
        expected.push_back({4});
        expected.push_back({2});

        // If 'filter_small_outer_extents' is true but we're not considering the
        // vectorized_loop_index, do split
        vector<vector<int64_t>> actual = generate_serial_tilings(s, 0, 0, 1, {}, true, true);

        EXPECT_EQ(expected, actual);
    }

    // Test that generate_gpu_tilings does not exit when it encounters a one tiling
    // option with too many threads
    {
        vector<vector<int64_t>> stage_sizes;
        stage_sizes.push_back({16, 16, 32});

        vector<vector<int>> pure_dims;
        pure_dims.push_back({0, 1, 2});

        vector<int64_t> max_s;
        max_s.push_back(16);
        max_s.push_back(16);
        max_s.push_back(2);

        vector<int> vectorized_indices;
        vectorized_indices.push_back(0);

        bool serial_inner = true;

        vector<vector<int64_t>> expected;
        expected.push_back({16, 2, 4});
        expected.push_back({16, 4, 4});
        expected.push_back({16, 8, 4});
        expected.push_back({16, 16, 4});

        auto actual = generate_gpu_tilings(stage_sizes, pure_dims, max_s, (int)(stage_sizes[0].size() - 1), vectorized_indices, serial_inner);

        EXPECT_EQ(expected, actual);
    }
}

int main(int argc, char **argv) {
    test_serial_tilings();
    printf("All tests passed.\n");
    return 0;
}
