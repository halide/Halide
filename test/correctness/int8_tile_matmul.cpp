#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>
#include <string>

// Matrix products of 8-bit integers accumulated in 32 bits, scheduled as 2x2
// output tiles with an 8-wide vectorized reduction. Some targets compute
// exactly that with one instruction (e.g. smmla/ummla/usmmla on ARM with
// FEAT_I8MM). Check a range of schedules, signednesses and sizes against a
// reference, and that the instruction is used where the target has it.

using namespace Halide;

namespace {

std::mt19937 rng(0);

template<typename T>
Buffer<T> make_input(int w, int h) {
    Buffer<T> buf(w, h);
    std::uniform_int_distribution<int> dist(std::numeric_limits<T>::min(), std::numeric_limits<T>::max());
    buf.for_each_value([&](T &v) { v = (T)dist(rng); });
    // Make sure the extremes of the product appear.
    buf(0, 0) = std::numeric_limits<T>::min();
    buf(1, 0) = std::numeric_limits<T>::max();
    return buf;
}

bool expect_instructions() {
    Target t = get_jit_target_from_environment();
    return t.arch == Target::ARM && t.bits == 64 &&
           t.with_implied_features().has_feature(Target::ARMI8MM) &&
           !t.has_feature(Target::NoNEON) && !t.has_feature(Target::SVE2);
}

// Count the instructions in the assembly whose mnemonic is op.
int count_instructions(const std::string &assembly, const std::string &op) {
    std::istringstream lines(assembly);
    std::string line;
    int count = 0;
    while (std::getline(lines, line)) {
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line.compare(start, op.size(), op) != 0) {
            continue;
        }
        size_t end = start + op.size();
        if (end == line.size() || line[end] == '.' || line[end] == ' ' || line[end] == '\t') {
            count++;
        }
    }
    return count;
}

enum class Schedule {
    // A 2x2 tile accumulated in registers, with the tile's columns
    // innermost.
    Tile,
    // The same, with the tile's rows innermost.
    TransposedTile,
    // A 4x4 register block made of unrolled 2x2 tiles.
    Block4x4,
    // A 2x2 tile accumulated directly in the output. The tile isn't
    // contiguous in the output, so this one may not use the instruction.
    Direct,
};

// C(j, i) = sum_k A(k, i) * B(k, j), with A M x K and B N x K, both stored
// with k innermost.
template<typename TA, typename TB, typename TC>
int test(const std::string &name, Schedule schedule, int M, int N, int K, const std::string &op) {
    Buffer<TA> a_buf = make_input<TA>(K, M);
    Buffer<TB> b_buf = make_input<TB>(K, N);
    ImageParam A(type_of<TA>(), 2, "A"), B(type_of<TB>(), 2, "B");
    A.set(a_buf);
    B.set(b_buf);

    Var i("i"), j("j"), io("io"), jo("jo"), ii("ii"), ji("ji");
    RDom r(0, K, "r");
    RVar ko("ko"), ki("ki");
    Func C("C"), acc("acc");

    auto product = [&](const Expr &row, const Expr &col) {
        return cast<TC>(A(r, row)) * cast<TC>(B(r, col));
    };

    switch (schedule) {
    case Schedule::Tile:
    case Schedule::TransposedTile: {
        acc(j, i) = cast<TC>(0);
        acc(j, i) += product(i, j);
        C(j, i) = acc(j, i);
        C.tile(j, i, jo, io, ji, ii, 2, 2).vectorize(ji).vectorize(ii);
        acc.compute_at(C, jo).store_in(MemoryType::Register).vectorize(j).vectorize(i);
        Stage s = acc.update().split(r, ko, ki, 8);
        if (schedule == Schedule::Tile) {
            s.reorder(ki, j, i, ko);
        } else {
            s.reorder(ki, i, j, ko);
        }
        s.atomic().vectorize(ki).vectorize(j).vectorize(i);
        break;
    }
    case Schedule::Block4x4: {
        // Element (jb, ib) of 2x2 tile (jt, it). Storing the tiles
        // separately keeps each one contiguous.
        Var jb("jb"), ib("ib"), jt("jt"), it("it");
        acc(jb, ib, jt, it) = cast<TC>(0);
        acc(jb, ib, jt, it) += product(2 * it + ib, 2 * jt + jb);
        C(j, i) = acc(j % 2, i % 2, j / 2, i / 2);
        C.tile(j, i, jo, io, ji, ii, 4, 4).vectorize(ji, 2).unroll(ji).unroll(ii);
        acc.compute_at(C, jo)
            .store_in(MemoryType::Register)
            .vectorize(jb)
            .vectorize(ib)
            .unroll(jt)
            .unroll(it);
        acc.update()
            .split(r, ko, ki, 8)
            .reorder(ki, jb, ib, jt, it, ko)
            .atomic()
            .vectorize(ki)
            .vectorize(jb)
            .vectorize(ib)
            .unroll(jt)
            .unroll(it);
        break;
    }
    case Schedule::Direct:
        C(j, i) = cast<TC>(0);
        C(j, i) += product(i, j);
        C.tile(j, i, jo, io, ji, ii, 2, 2).vectorize(ji).vectorize(ii);
        C.update()
            .split(r, ko, ki, 8)
            .tile(j, i, jo, io, ji, ii, 2, 2)
            .reorder(ki, ji, ii, ko, jo, io)
            .atomic()
            .vectorize(ki)
            .vectorize(ji)
            .vectorize(ii);
        break;
    }

    Buffer<TC> out = C.realize({N, M});
    for (int y = 0; y < M; y++) {
        for (int x = 0; x < N; x++) {
            int64_t correct = 0;
            for (int k = 0; k < K; k++) {
                correct += (int64_t)a_buf(k, y) * (int64_t)b_buf(k, x);
            }
            if (out(x, y) != (TC)correct) {
                printf("%s: C(%d, %d) = %lld instead of %lld\n",
                       name.c_str(), x, y, (long long)out(x, y), (long long)(TC)correct);
                return 1;
            }
        }
    }

    if (expect_instructions() && schedule != Schedule::Direct) {
        std::string file = Internal::get_test_tmp_dir() + "int8_tile_matmul_" + name + ".s";
        C.compile_to_assembly(file, {A, B}, get_jit_target_from_environment());
        std::ifstream in(file);
        std::stringstream contents;
        contents << in.rdbuf();
        int count = count_instructions(contents.str(), op);
        // Each of the four unrolled tiles of a 4x4 block needs one.
        int expected = schedule == Schedule::Block4x4 ? 4 : 1;
        if (count < expected) {
            printf("%s: expected at least %d %s in the generated assembly, found %d\n",
                   name.c_str(), expected, op.c_str(), count);
            return 1;
        }
    }

    return 0;
}

template<typename TA, typename TB, typename TC>
int test_all_schedules(const std::string &name, const std::string &op) {
    int failures = 0;
    failures += test<TA, TB, TC>(name + "_tile", Schedule::Tile, 8, 6, 32, op);
    failures += test<TA, TB, TC>(name + "_transposed_tile", Schedule::TransposedTile, 6, 8, 32, op);
    // Odd sizes and a reduction that isn't a multiple of 8.
    failures += test<TA, TB, TC>(name + "_tile_tails", Schedule::Tile, 7, 5, 37, op);
    failures += test<TA, TB, TC>(name + "_transposed_tile_tails", Schedule::TransposedTile, 5, 7, 37, op);
    failures += test<TA, TB, TC>(name + "_block4x4", Schedule::Block4x4, 8, 12, 64, op);
    failures += test<TA, TB, TC>(name + "_direct", Schedule::Direct, 8, 6, 32, op);
    return failures;
}

}  // namespace

int main(int argc, char **argv) {
    int failures = 0;
    failures += test_all_schedules<int8_t, int8_t, int32_t>("s8_s8", "smmla");
    failures += test_all_schedules<uint8_t, uint8_t, int32_t>("u8_u8", "ummla");
    failures += test_all_schedules<uint8_t, uint8_t, uint32_t>("u8_u8_u32", "ummla");
    failures += test_all_schedules<uint8_t, int8_t, int32_t>("u8_s8", "usmmla");
    failures += test_all_schedules<int8_t, uint8_t, int32_t>("s8_u8", "usmmla");

    if (failures) {
        printf("%d tests failed\n", failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
}
