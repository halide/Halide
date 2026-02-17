#include <Halide.h>

#include <algorithm>
#include <cassert>
#include <iostream>
#include <random>
#include <string>

using namespace Halide;
using namespace Halide::Internal;

using std::optional;
using std::vector;

namespace {

vector<int> shuffle_without_divided(const vector<int> &a, const vector<int> &b, const vector<int> &indices) {
    int src_lanes = static_cast<int>(a.size());
    vector<int> dst(indices.size(), 0xdeadbeef);
    for (size_t i = 0; i < indices.size(); ++i) {
        int idx = indices[i];
        if (idx < 0) {
            continue;
        }
        if (idx < src_lanes) {
            dst[i] = a[idx];
        } else {
            int idx_b = idx - src_lanes;
            internal_assert(idx_b < static_cast<int>(b.size()));
            dst[i] = b[idx_b];
        }
    }
    return dst;
}

struct STLShuffleCodeGen {
    int get_vector_num_elements(const vector<int> &v) {
        return static_cast<int>(v.size());
    }

    vector<int> slice_vector(const vector<int> &v, int start, int lanes) {
        auto result = vector<int>(v.begin() + start, v.begin() + std::min(start + lanes, static_cast<int>(v.size())));
        result.resize(lanes);
        return result;
    }

    vector<int> concat_vectors(const vector<vector<int>> &vecs) {
        vector<int> out;
        for (const auto &v : vecs) {
            out.insert(out.end(), v.begin(), v.end());
        }
        return out;
    }

    vector<int> shuffle_scalable_vectors_general(const vector<int> &a, const vector<int> &b, const vector<int> &indices) {
        internal_assert(a.size() == indices.size());

        auto result = shuffle_without_divided(a, b, indices);

        debug(1) << "slice a: " << PrintSpan{a} << ", "
                 << "slice b: " << PrintSpan{b} << ", "
                 << "indices: " << PrintSpan{indices} << "\n"
                 << "\t=> slice output: " << PrintSpan{result} << "\n";

        return result;
    }
};

void generate_data(int src_lanes, int dst_lanes,
                   vector<int> &a, vector<int> &b, vector<int> &indices) {
    a.resize(src_lanes);
    b.resize(src_lanes);
    for (int i = 0; i < src_lanes; ++i) {
        a[i] = i * 10;
        b[i] = (i + src_lanes) * 10;
    }
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution dist(0, src_lanes * 2 - 1);
    indices.resize(dst_lanes);
    for (int i = 0; i < dst_lanes; ++i) {
        indices[i] = dist(gen);
    }

    debug(1) << "input a: " << PrintSpan{a} << "\n"
             << "input b: " << PrintSpan{b} << "\n"
             << "indices: " << PrintSpan{indices} << "\n\n";
}

void assert_vectors_equal(const vector<int> &expected, const vector<int> &actual) {
    internal_assert(expected.size() == actual.size())
        << "Vector sizes are different\n"
        << "expected: " << PrintSpan{expected} << "\n"
        << "  actual: " << PrintSpan{actual} << "\n";

    for (size_t i = 0; i < expected.size(); ++i) {
        internal_assert(expected[i] == actual[i])
            << "Mismatch: expected[" << i << "] = " << expected[i] << ", actual[" << i << "] = " << actual[i] << "\n"
            << "expected: " << PrintSpan{expected} << "\n"
            << "  actual: " << PrintSpan{actual} << "\n";
    }
}

void run_single_test(int src_lanes, int dst_lanes, int vl) {
    vector<int> a, b, indices;
    generate_data(src_lanes, dst_lanes, a, b, indices);

    auto expected = shuffle_without_divided(a, b, indices);

    STLShuffleCodeGen ops;
    DecomposeVectorShuffle shuffler(ops, a, b, src_lanes, vl);
    auto actual = shuffler.run(indices);

    assert_vectors_equal(expected, actual);
}

void run_test(int src_lanes, int dst_lanes, int vl, int repeat) {
    debug(2) << "Running " << repeat << " tests for\n"
             << "  src_lanes: " << src_lanes
             << ", dst_lanes: " << dst_lanes
             << ", vl: " << vl << "\n";

    for (int t = 0; t < repeat; ++t) {
        run_single_test(src_lanes, dst_lanes, vl);
    }
}

}  // namespace

int main(int argc, char *argv[]) {
    int repeat = 100;

    if (argc >= 3) {
        int src_lanes = std::stoi(argv[1]);
        int dst_lanes = std::stoi(argv[2]);
        int vl = (argc >= 4) ? std::stoi(argv[3]) : 4;
        repeat = (argc >= 5) ? std::stoi(argv[4]) : repeat;
        internal_assert(popcount64(vl) == 1 && vl > 1) << "vl must be a power of 2";
        run_test(src_lanes, dst_lanes, vl, repeat);
    } else {
        run_test(8, 8, 4, repeat);
        run_test(19, 9, 4, repeat);
        run_test(5, 3, 8, repeat);
    }

    printf("Success!\n");
    return 0;
}
