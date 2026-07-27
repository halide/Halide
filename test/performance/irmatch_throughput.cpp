#include "Halide.h"
#include "halide_benchmark.h"

#include <cstdio>
#include <vector>

// A matcher-bound throughput benchmark for the term-rewriting engine in
// src/IRMatch.h. Most rules deliberately fail to match, which is the realistic
// hot path (the simplifier tries many rules per node and few fire).

using namespace Halide;
using namespace Halide::Internal;
using namespace Halide::Tools;

namespace {

// Run a large battery of Sub-canonicalization rules, mirroring the structure
// of src/Simplify_Sub.cpp. Returns the number of rules that fired (kept live
// so nothing is optimized away).
HALIDE_ALWAYS_INLINE int try_sub_rules(const Expr &e) {
    // Putting this at the top level trips a GCC ADL bug!
    using namespace Halide::Internal::IRMatcher;

    auto rewrite = IRMatcher::rewriter(e, e.type());

    Wild<0> x;
    Wild<1> y;
    Wild<2> z;
    WildConst<0> c0;
    WildConst<1> c1;

    return rewrite(x - x, 0) ||
           rewrite(x - 0, x) ||
           rewrite(c0 - c1, fold(c0 - c1)) ||
           rewrite((x + y) - x, y) ||
           rewrite((x + y) - y, x) ||
           rewrite((x - y) - x, -y) ||
           rewrite((x + c0) - c1, x + fold(c0 - c1)) ||
           rewrite((c0 - x) - c1, fold(c0 - c1) - x) ||
           rewrite(x - (x + y), -y) ||
           rewrite(x - (y + x), -y) ||
           rewrite((x + y) - (x + z), y - z) ||
           rewrite((x + y) - (z + x), y - z) ||
           rewrite((y + x) - (x + z), y - z) ||
           rewrite((y + x) - (z + x), y - z) ||
           rewrite(min(x, y) - x, min(y - x, 0)) ||
           rewrite(min(x, y) - y, min(x - y, 0)) ||
           rewrite(max(x, y) - x, max(y - x, 0)) ||
           rewrite(max(x, y) - y, max(x - y, 0)) ||
           rewrite(x - min(x, y), max(x - y, 0)) ||
           rewrite(x - max(x, y), min(x - y, 0)) ||
           rewrite((x + c0) - (y + c1), (x - y) + fold(c0 - c1)) ||
           rewrite((x * c0) - (y * c0), (x - y) * c0) ||
           rewrite(c0 - (c1 - x), (c0 - c1) + x) ||
           rewrite(c0 - (x + c1), fold(c0 - c1) - x) ||
           rewrite((x - y) - (x - z), z - y) ||
           rewrite((x - y) - (z - y), x - z);
}

std::vector<Expr> make_corpus() {
    std::vector<Expr> corpus;
    for (Type t : {Int(32), Int(16), UInt(32), Float(32)}) {
        Expr x = Variable::make(t, "x");
        Expr y = Variable::make(t, "y");
        Expr z = Variable::make(t, "z");
        Expr c0 = make_const(t, 3);
        Expr c1 = make_const(t, 5);
        // A mix: some hit late rules, some hit early rules, some fail entirely.
        corpus.push_back(x - x);                       // fires rule 1
        corpus.push_back((x + c0) - (y + c1));         // fires a late rule
        corpus.push_back((x + y) - (z + x));           // fires a mid rule
        corpus.push_back(max(x, y) - z);               // fails everything
        corpus.push_back((x * c0) - (y * c0));         // fires a late rule
        corpus.push_back(min(x, y) - x);               // fires a mid rule
        corpus.push_back((x - y) - (z - y));           // fires the last rule
        corpus.push_back(Variable::make(t, "w") - z);  // fails everything
    }
    return corpus;
}

}  // namespace

int main(int argc, char **argv) {
    std::vector<Expr> corpus = make_corpus();

    // Prevent the optimizer from discarding the work.
    volatile int sink = 0;

    // Warmup + sanity.
    for (const Expr &e : corpus) {
        sink += try_sub_rules(e);
    }

    double t = benchmark(20, 200, [&]() {
        int fired = 0;
        for (const Expr &e : corpus) {
            fired += try_sub_rules(e);
        }
        sink += fired;
    });

    printf("irmatch throughput: %.2f ns per rule-battery (corpus of %d, sink %d)\n",
           (t / corpus.size()) * 1e9, (int)corpus.size(), (int)sink);
    printf("Success!\n");
    return 0;
}
