// A tensor core operand staged in a fragment is loaded with the layout of the
// matrix it is filled from, and PTX requires the multiply to name the same
// layout as the wmma.load that produced each of its operands. A fragment's own
// storage may be in the other layout from its source (here, the default
// storage of a Func that reads a transposed input), which must not leak into
// the multiply. Compiling for CUDA embeds the PTX in the host assembly, so no
// device is needed to check this.

#include "Halide.h"
#include "halide_test_dirs.h"
#include <fstream>
#include <set>
#include <sstream>
#include <stdio.h>

using namespace Halide;

namespace {

// The layouts named by every instance of an instruction in the assembly: the
// one or two words ("row" or "col") following the prefix and an optional
// ".aligned" qualifier.
std::set<std::string> layouts(const std::string &assembly, const std::string &prefix, int words) {
    std::set<std::string> result;
    for (size_t at = assembly.find(prefix); at != std::string::npos;
         at = assembly.find(prefix, at + 1)) {
        size_t start = at + prefix.size();
        if (assembly.compare(start, 8, "aligned.") == 0) {
            start += 8;
        }
        result.insert(assembly.substr(start, 4 * words - 1));
    }
    return result;
}

// A 32x32x32 matrix multiply in 16x16x16 tiles, with both operands staged in
// fragments. Each input is stored either the way its fragment is (A with K
// dense, B with N dense) or transposed.
bool test(bool a_transposed, bool b_transposed) {
    const int M = 32, N = 32, K = 32, tile = 16;

    ImageParam A(Float(16), 2, "A"), B(Float(16), 2, "B");
    Var x("x"), y("y"), kk("kk"), yy("yy"), xx("xx");
    RDom k(0, K, "k");
    Func prod("prod"), out("out"), Am("Am"), Bm("Bm");

    Am(kk, yy) = a_transposed ? A(yy, kk) : A(kk, yy);
    Bm(xx, kk) = b_transposed ? B(kk, xx) : B(xx, kk);
    prod(x, y) = 0.f;
    prod(x, y) += cast<float>(Am(k, y)) * cast<float>(Bm(x, k));
    out(x, y) = prod(x, y);

    Var xi("xi"), yi("yi"), mmxi("mmxi"), mmyi("mmyi"), rxi("rxi"), ryi("ryi");
    Var kko("kko"), kki("kki"), yyo("yyo"), yyi("yyi"), xxo("xxo"), xxi("xxi");
    RVar ko("ko"), ki("ki");

    out.bound(x, 0, N)
        .bound(y, 0, M)
        .tile(x, y, xi, yi, N, M)
        .tile(xi, yi, mmxi, mmyi, tile, tile)
        .gpu_blocks(x, y)
        .reorder(mmxi, mmyi, xi, yi, x, y)
        .unroll(xi)
        .unroll(yi)
        .vectorize(mmxi)
        .vectorize(mmyi);

    prod.compute_at(out, x)
        .store_in(MemoryType::Tile)
        .tile(x, y, rxi, ryi, tile, tile)
        .vectorize(rxi)
        .vectorize(ryi)
        .unroll(x)
        .unroll(y);

    prod.update()
        .split(k, ko, ki, tile)
        .tile(x, y, rxi, ryi, tile, tile)
        .reorder(ki, rxi, ryi, x, y, ko)
        .unroll(x)
        .unroll(y)
        .atomic()
        .vectorize(ki)
        .vectorize(rxi)
        .vectorize(ryi);

    for (Func f : {Am, Bm}) {
        // Am is (k, row), Bm is (column, k): either way the fragment is the
        // tile of the first two dimensions, and both are staged per step of k.
        Var a = f.args()[0], b = f.args()[1], ao("ao"), ai("ai"), bo("bo"), bi("bi");
        f.compute_at(prod, ko)
            .store_in(MemoryType::Tile)
            .split(a, ao, ai, tile)
            .split(b, bo, bi, tile)
            .reorder(ai, bi, ao, bo)
            .unroll(ao)
            .unroll(bo)
            .vectorize(ai)
            .vectorize(bi);
    }

    Target t = get_host_target()
                   .with_feature(Target::CUDA)
                   .with_feature(Target::CUDACapability70);
    std::string path = Internal::get_test_tmp_dir() + "wmma_fragment_layout.s";
    out.compile_to_assembly(path, {A, B}, "out", t);
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string assembly = ss.str();

    const std::string a = a_transposed ? "col" : "row", b = b_transposed ? "col" : "row";
    const std::set<std::string>
        load_a = layouts(assembly, "wmma.load.a.sync.", 1),
        load_b = layouts(assembly, "wmma.load.b.sync.", 1),
        mma = layouts(assembly, "wmma.mma.sync.", 2);
    if (load_a != std::set<std::string>{a} || load_b != std::set<std::string>{b} ||
        mma != std::set<std::string>{a + "." + b}) {
        auto print = [](const char *what, const std::set<std::string> &s) {
            printf(" %s:", what);
            for (const std::string &l : s) {
                printf(" %s", l.c_str());
            }
        };
        printf("With A%s and B%s, expected wmma.load.a.%s, wmma.load.b.%s and "
               "wmma.mma.%s.%s, but got",
               a_transposed ? " transposed" : "", b_transposed ? " transposed" : "",
               a.c_str(), b.c_str(), a.c_str(), b.c_str());
        print("load.a", load_a);
        print("load.b", load_b);
        print("mma", mma);
        printf("\n");
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    for (bool a_transposed : {false, true}) {
        for (bool b_transposed : {false, true}) {
            if (!test(a_transposed, b_transposed)) {
                printf("Failed!\n");
                return 1;
            }
        }
    }
    printf("Success!\n");
    return 0;
}
