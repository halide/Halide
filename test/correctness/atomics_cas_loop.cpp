#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>
#include <fstream>
#include <sstream>

using namespace Halide;

// An atomic update that can't be done as a single atomic read-modify-write is
// done as a compare-and-swap loop. The new value must be computed from the
// value the cmpxchg compares against, so the loop must not contain any other
// (non-atomic) load of the location being updated. This test compiles a few
// such updates to LLVM assembly and checks that no basic block containing a
// cmpxchg also contains a plain load.

namespace {

// Returns the number of basic blocks containing a cmpxchg, or -1 if any of
// them also contains a non-atomic load.
int check_cas_loops(const std::string &name, const std::string &path) {
    std::ifstream in(path);
    std::string line;
    std::vector<std::string> block;
    int cas_blocks = 0;
    bool ok = true;
    auto finish_block = [&]() {
        bool has_cmpxchg = false;
        for (const auto &l : block) {
            has_cmpxchg |= l.find(" cmpxchg ") != std::string::npos;
        }
        if (has_cmpxchg) {
            cas_blocks++;
            for (const auto &l : block) {
                if (l.find(" = load ") != std::string::npos &&
                    l.find(" = load atomic ") == std::string::npos) {
                    printf("%s: non-atomic load in a compare-and-swap loop:\n", name.c_str());
                    for (const auto &b : block) {
                        printf("  %s\n", b.c_str());
                    }
                    ok = false;
                    break;
                }
            }
        }
        block.clear();
    };
    while (std::getline(in, line)) {
        // Instructions are indented; anything else (a label, a function
        // definition, a closing brace) ends the current basic block.
        if (line.empty() || line[0] != ' ') {
            finish_block();
        }
        block.push_back(line);
    }
    finish_block();
    return ok ? cas_blocks : -1;
}

}  // namespace

int main(int argc, char **argv) {
    const Target target = get_host_target()
                              .with_feature(Target::NoAsserts)
                              .with_feature(Target::NoRuntime)
                              .with_feature(Target::NoBoundsQuery);
    const std::string dir = Internal::get_test_tmp_dir();

    const int hist_size = 7;
    bool ok = true;
    for (int i = 0; i < 5; i++) {
        Func hist("hist");
        Var x("x");
        RDom r(0, 1000, "r");
        Expr idx = r % hist_size;
        std::string name;
        int vector_width = 1;
        switch (i) {
        case 0:
            name = "min_i32";
            hist(x) = 0;
            hist(idx) = min(hist(idx) + 1, 100);
            break;
        case 1:
            name = "max_f32";
            hist(x) = 0.0f;
            hist(idx) = max(hist(idx), cast<float>(r));
            break;
        case 2:
            name = "mul_u32";
            hist(x) = cast<uint32_t>(1);
            hist(idx) = hist(idx) * cast<uint32_t>(3);
            break;
        case 3:
            name = "min_i32_vectorized";
            hist(x) = 0;
            hist(idx) = min(hist(idx) + 1, 100);
            vector_width = 8;
            break;
        case 4:
            name = "min_f16";
            hist(x) = cast<float16_t>(0);
            hist(idx) = min(hist(idx) + cast<float16_t>(1), cast<float16_t>(100));
            break;
        }

        if (vector_width > 1) {
            RVar ro("ro"), ri("ri");
            hist.update()
                .atomic(true /*override_associativity_test*/)
                .split(r, ro, ri, vector_width)
                .parallel(ro)
                .vectorize(ri);
        } else {
            hist.update()
                .atomic(true /*override_associativity_test*/)
                .parallel(r);
        }

        const std::string ll = dir + "atomics_cas_loop_" + name + ".ll";
        hist.compile_to_llvm_assembly(ll, {}, "atomics_cas_loop_" + name, target);
        Internal::assert_file_exists(ll);

        int cas_blocks = check_cas_loops(name, ll);
        if (cas_blocks == 0) {
            printf("%s: no compare-and-swap loop found in %s\n", name.c_str(), ll.c_str());
            ok = false;
        } else if (cas_blocks < 0) {
            ok = false;
        }
    }

    if (!ok) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}
