#include "DecomposeVectorShuffle.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <random>
#include <string>

namespace Halide {
namespace Internal {
namespace {

using namespace std;
// #define DVS_DEBUG

void print_v(const std::string &preamble, const vector<int> &v, const std::string &epilogue = "\n") {
#ifdef DVS_DEBUG
    cout << preamble << "[";
    for (const auto &e : v) {
        cout << e << ", ";
    }
    cout << "]" << epilogue;
#endif
};

//-----------------------------
// Test for DecomposeVectorShuffle using std::vector<int> as VecTy
//-----------------------------
struct STLVectorShuffler : public DecomposeVectorShuffle<STLVectorShuffler, std::vector<int>> {

    STLVectorShuffler(const vector<int> &src_a, const vector<int> &src_b, const vector<int> &indices, int vl)
        : DecomposeVectorShuffle(src_a, src_b, indices, vl) {
    }

    int get_vec_length(vector<int> &v) {
        return static_cast<int>(v.size());
    }

    vector<int> align_up_vector(vector<int> &v, int align) {
        size_t org_len = v.size();
        v.resize(align_up(org_len, align), 0);
        return v;
    }

    vector<int> slice_vec(const vector<int> &v, int start, size_t lanes) {
        assert(start + lanes <= v.size());
        return vector<int>(v.begin() + start, v.begin() + start + lanes);
    }

    vector<int> concat_vecs(const vector<vector<int>> &vecs) {
        vector<int> out;
        for (const auto &v : vecs) {
            out.insert(out.end(), v.begin(), v.end());
        }
        return out;
    }

    vector<int> shuffle_vl_aligned(const vector<int> &a, const optional<vector<int>> &b, const vector<int> &indices, int vl) {
        if (b.has_value()) {
            assert(a.size() == b->size());
        }
        assert(a.size() == indices.size());
        assert(indices.size() % vl == 0);

        auto result = shuffle_without_divided(a, b.value_or(vector<int>{}), indices);

        print_v("slice a:", a, ",  ");
        print_v("slice b:", b.value_or(vector<int>{}), ",  ");
        print_v("indices:", indices);
        print_v("  => slice output:", result);
        return result;
    }

    // Naive implementation of shuffle
    vector<int> shuffle_without_divided(const vector<int> &a, const vector<int> &b, const vector<int> &indices) {
        int src_lanes = static_cast<int>(a.size());
        vector<int> dst(indices.size(), 0xdeadbeaf);
        for (size_t i = 0; i < indices.size(); ++i) {
            int idx = indices[i];
            if (idx < 0) {
                continue;
            } else if (idx < src_lanes) {
                dst[i] = a[idx];
            } else {
                int idx_b = idx - src_lanes;
                assert(idx_b < static_cast<int>(b.size()));
                dst[i] = b[idx_b];
            }
        }
        return dst;
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
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dist(0, src_lanes * 2 - 1);
    indices.resize(dst_lanes);
    for (int i = 0; i < dst_lanes; ++i) {
        indices[i] = dist(gen);
    }

    print_v("input a: ", a);
    print_v("input b: ", b);
    print_v("indices: ", indices, "\n\n");
}

bool compare_vectors(const vector<int> &ref, const vector<int> &tar) {
    print_v("\noutput: ", tar, "\n\n");

    if (ref.size() != tar.size()) {
        cerr << "Vector sizes are different\n";
        return false;
    }
    for (size_t i = 0; i < ref.size(); ++i) {
        if (ref[i] != tar[i]) {
            cerr << "Mismatch at index " << i
                 << ": expected " << ref[i] << ", got " << tar[i] << "\n";
            return false;
        }
    }
    return true;
}

void run_single_test(int src_lanes, int dst_lanes, int vl) {
    vector<int> a, b, indices;
    generate_data(src_lanes, dst_lanes, a, b, indices);

    STLVectorShuffler shuffler(a, b, indices, vl);
    auto ref = shuffler.shuffle_without_divided(a, b, indices);
    auto tar = shuffler.shuffle();
    assert(compare_vectors(ref, tar));
}

void run_test(int src_lanes, int dst_lanes, int vl, int repeat) {
#ifdef DVS_DEBUG
    cout << "\nRunning " << repeat << " tests for\n src_lanes: " << src_lanes
         << ", dst_lanes: " << dst_lanes
         << ", vl: " << vl << "\n";
#endif

    for (int t = 0; t < repeat; ++t) {
        run_single_test(src_lanes, dst_lanes, vl);
    }
}

}  // namespace

void decompose_vector_shuffle_test() {
    int repeat = 100;
    run_test(8, 8, 4, repeat);
    run_test(19, 9, 4, repeat);
    run_test(5, 3, 8, repeat);
    cout << "test_decompose_vector_shuffle passed\n";
}

}  // namespace Internal
}  // namespace Halide

// #define CLI_TEST_DECOMPOSE_TO_NATIVE_SHUFFLES
#ifdef CLI_TEST_DECOMPOSE_TO_NATIVE_SHUFFLES
int main(int argc, char *argv[]) {
    int src_lanes = 19;
    int dst_lanes = 9;
    int vl = 4;
    int repeat = 100;

    if (argc >= 3) {
        src_lanes = stoi(argv[1]);
        dst_lanes = stoi(argv[2]);
    }
    if (argc >= 4) {
        vl = stoi(argv[3]);
        assert(__popcount(vl) == 1 && vl > 1);  // power of 2 only
    }

    Halide::Internal::run_test(src_lanes, dst_lanes, vl, 100);
    cout << "All tests passed\n";
    return 0;
}

#endif  // CLI_TEST_DECOMPOSE_TO_NATIVE_SHUFFLES
