#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

using namespace Halide;

namespace {

// A gemv over 8 rows whose weights are interleaved 4 rows at a time in 4-byte
// groups (as in ggml's q4_0_4x4 and KleidiAI's dotprod kernels): byte j of
// group g of row r lives at w(16 * g + 4 * (r % 4) + j, r / 4). Each group of
// 4 activation bytes is shared by all 4 rows of a row group, so the 16-byte
// weight vector of a group should be dotted against a 4-byte activation group
// broadcast to all four lanes. Staging 16 activation bytes in a register and
// unrolling 4 groups should then produce the by-element form
// sdot/udot vD.4s, vN.16b, vM.4b[lane] with one lane per group.
struct Pipeline {
    ImageParam w, a;
    Param<int> groups{"groups"};
    Func f{"f"};

    Pipeline(Type t, bool activation_first)
        : w(t, 2, "w"), a(t, 1, "a") {
        Type acc = t.is_int() ? Int(32) : UInt(32);
        Var row("row");
        RDom r(0, 4, 0, groups * 4);
        Expr wt = cast(acc, w(16 * r.y + 4 * (row % 4) + r.x, row / 4));
        Expr act = cast(acc, a(4 * r.y + r.x));
        f(row) = cast(acc, 0);
        f(row) += activation_first ? act * wt : wt * act;

        RVar gyo("gyo"), gyi("gyi");
        f.bound(row, 0, 8);
        f.update()
            .split(r.y, gyo, gyi, 4)
            .reorder(r.x, row, gyi, gyo)
            .atomic()
            .vectorize(r.x)
            .vectorize(row, 4)
            .unroll(row)
            .unroll(gyi);
        a.in(f).compute_at(f, gyo).bound_extent(_0, 16).vectorize(_0);
    }

    std::vector<Argument> args() const {
        return {w, a, groups};
    }
};

int check_asm(Type t, bool activation_first) {
    Pipeline p(t, activation_first);
    const char *op = t.is_int() ? "sdot" : "udot";
    std::string name = std::string(op) + (activation_first ? "_activation_first" : "_weight_first");
    std::string file = Internal::get_test_tmp_dir() + "arm_dot_product_by_element_" + name + ".s";
    Target target("arm-64-linux-arm_dot_prod-no_runtime-no_asserts-no_bounds_query");
    p.f.compile_to_assembly(file, p.args(), name, target);

    std::ifstream in(file);
    std::stringstream asm_text;
    asm_text << in.rdbuf();
    std::string s = asm_text.str();

    std::regex by_element(std::string(op) + R"(\s+v\d+\.4s,\s*v\d+\.16b,\s*v\d+\.4b\[([0-3])\])");
    std::set<std::string> lanes;
    for (auto it = std::sregex_iterator(s.begin(), s.end(), by_element); it != std::sregex_iterator(); ++it) {
        lanes.insert((*it)[1]);
    }
    if (lanes.size() != 4) {
        printf("%s: expected by-element %s on all four lanes, found %d lanes. Assembly:\n%s\n",
               name.c_str(), op, (int)lanes.size(), s.c_str());
        return 1;
    }
    return 0;
}

template<typename T, typename Acc>
int check_values(bool activation_first) {
    Pipeline p(type_of<T>(), activation_first);
    const int groups = 3;  // 12 activation groups, so 3 iterations of 4.
    const int k = 4 * groups * 4;

    Buffer<T> w(4 * k, 2), a(k);
    w.for_each_value([](T &v) { v = (T)(rand() & 0xff); });
    a.for_each_value([](T &v) { v = (T)(rand() & 0xff); });

    p.w.set(w);
    p.a.set(a);
    p.groups.set(groups);
    Buffer<Acc> out = p.f.realize({8});

    for (int row = 0; row < 8; row++) {
        Acc correct = 0;
        for (int g = 0; g < 4 * groups; g++) {
            for (int j = 0; j < 4; j++) {
                correct += (Acc)w(16 * g + 4 * (row % 4) + j, row / 4) * (Acc)a(4 * g + j);
            }
        }
        if (out(row) != correct) {
            printf("out(%d) = %lld instead of %lld\n", row, (long long)out(row), (long long)correct);
            return 1;
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    for (bool activation_first : {false, true}) {
        if (check_asm(Int(8), activation_first) ||
            check_asm(UInt(8), activation_first)) {
            return 1;
        }
    }

    Target host = get_jit_target_from_environment();
    if (host.arch == Target::ARM && host.bits == 64 && host.has_feature(Target::ARMDotProd)) {
        for (bool activation_first : {false, true}) {
            if (check_values<int8_t, int32_t>(activation_first) ||
                check_values<uint8_t, uint32_t>(activation_first)) {
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
