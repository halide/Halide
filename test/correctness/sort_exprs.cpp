#include "Halide.h"
using namespace Halide;

// Order a pair of Exprs, treating undefined Exprs as infinity
void sort2(Expr &a, Expr &b) {
    if (!a.defined()) {
        std::swap(a, b);
    } else if (!b.defined()) {
        return;
    } else {
        Expr tmp = min(a, b);
        b = max(a, b);
        a = tmp;
    }
}

// Bitonic sort a vector of Exprs
std::vector<Expr> bitonic_sort_inner(std::vector<Expr> v, bool flipped) {
    size_t size = v.size();
    size_t half_size = size / 2;

    if (!half_size) return v;

    std::vector<Expr>::iterator middle = v.begin() + half_size;
    std::vector<Expr> a, b;
    a.insert(a.begin(), v.begin(), middle);
    b.insert(b.begin(), middle, v.end());

    // Sort each half
    a = bitonic_sort_inner(a, true);
    b = bitonic_sort_inner(b, false);
    assert(a.size() == half_size);
    assert(b.size() == half_size);

    // Concat the results
    a.insert(a.end(), b.begin(), b.end());

    // Bitonic merge
    for (size_t stride = half_size; stride > 0; stride /= 2) {
        for (size_t i = 0; i < size; i++) {
            if (i % (2 * stride) < stride) {
                if (!flipped) {
                    sort2(a[i], a[i + stride]);
                } else {
                    sort2(a[i + stride], a[i]);
                }
            }
        }
    }

    return a;
}

std::vector<Expr> bitonic_sort(std::vector<Expr> v) {
    // Bulk up the vector to a power of two using infinities
    while (v.size() & (v.size() - 1)) {
        v.push_back(Expr());
    }

    v = bitonic_sort_inner(v, false);

    while (!v.back().defined()) {
        v.pop_back();
    }
    return v;
}

Expr median(std::vector<Expr> v) {
    v = bitonic_sort(v);
    return v[v.size() / 2];
}

int main(int argc, char **argv) {
    Func f;
    Var x;
    f(x) = sin(x);
    f.compute_root();

    const int N = 9;

    std::vector<Expr> exprs;
    for (int i = 0; i < N; i++) {
        exprs.push_back(f(i));
    }
    exprs = bitonic_sort(exprs);

    std::cout << exprs.size() << "\n";

    // Use update definitions to write them to another Func in sorted
    // order for inspection. Note that doing this doesn't explicitly
    // share work between each element - it'll generate the huge
    // min/max expression to extract each sorted element. llvm should
    // lift out common subexpressions though.
    Func g;
    g(x) = undef<float>();
    for (int i = 0; i < N; i++) {
        g(i) = exprs[i];
    }

    Buffer<float> result = g.realize({N});

    for (int i = 0; i < N; i++) {
        printf("%f ", result(i));
    }
    printf("\n");

    for (int i = 0; i < N - 1; i++) {
        if (result(i) >= result(i + 1)) {
            printf("Results were not in order\n");
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
