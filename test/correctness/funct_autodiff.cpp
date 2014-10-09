#include <stdio.h>
#include <Halide.h>

using namespace Halide;

#if __cplusplus > 199711L
#define CPP11
#endif

// Class which tracks the value of a function of 2 variables and its partial derivatives.
class DiffExpr {
public:
    Expr f;
    Expr dx, dy;

    DiffExpr() {}
    DiffExpr(Expr f, Expr dx, Expr dy) : f(f), dx(dx), dy(dy) {}
    DiffExpr(float f) : f(f), dx(0.0f), dy(0.0f) {}

    // To work with Halide, we need a conversion to/from Tuple.
    explicit DiffExpr(Tuple t) : f(t[0]), dx(t[1]), dy(t[2]) {}
    operator Tuple() const { return Tuple(f, dx, dy); }
};

DiffExpr operator + (DiffExpr l, DiffExpr r) { return DiffExpr(l.f + r.f, l.dx + r.dx, l.dy + r.dy); }
DiffExpr operator - (DiffExpr l, DiffExpr r) { return DiffExpr(l.f - r.f, l.dx - r.dx, l.dy - r.dy); }
// Define operator * using the product rule for the derivatives.
DiffExpr operator * (DiffExpr l, DiffExpr r) { return DiffExpr(l.f*r.f,
                                                               l.f*r.dx + r.f*l.dx,
                                                               l.f*r.dy + r.f*l.dy); }
// Quotient rule for the derivatives.
DiffExpr operator / (DiffExpr l, DiffExpr r) { return DiffExpr(l.f/r.f,
                                                               (r.f*l.dx - l.f*r.dx)/(r.f*r.f),
                                                               (r.f*l.dy - l.f*r.dy)/(r.f*r.f)); }

DiffExpr sin(DiffExpr x) { return DiffExpr(sin(x.f), cos(x.f)*x.dx, cos(x.f)*x.dy); }
DiffExpr cos(DiffExpr x) { return DiffExpr(cos(x.f), -sin(x.f)*x.dx, -sin(x.f)*x.dy); }
DiffExpr sqrt(DiffExpr x) { return DiffExpr(sqrt(x.f), 0.5f*x.dx/sqrt(x.f), 0.5f*x.dy/sqrt(x.f)); }

typedef FuncT<DiffExpr> DiffFunc;

// Declare the test func here as a template. This allows for computing
// the derivative numerically (T = Expr) or via auto differentiation (T = DiffExpr).
template <typename T>
T test_func(T x, T y) {
    const float pi = 3.141592f;
    return cos(sqrt(x*x + y*y)/(2*pi));
}

int main(int argc, char **argv) {

    Var x("x"), y("y");

    // Define DiffExprs for our variables (x -> dx = 1, y -> dy = 1).
    DiffExpr xdx(x, 1.0f, 0.0f);
    DiffExpr ydy(y, 0.0f, 1.0f);

    // Define some interesting function.
    DiffFunc f("f");
    f(x, y) = test_func(xdx, ydy);

    // Compute the magnitude of the derivative of f.
    Func g("g");
    g(x, y) = sqrt(f(x, y).dx*f(x, y).dx + f(x, y).dy*f(x, y).dy);

    Func ref("ref");
    // Approximate the magnitude of the derivative of f via finite differences.
    float h = 1e-3f;
    Expr df_dx = (test_func<Expr>(x + h, y) - test_func<Expr>(x - h, y))/(2*h);
    Expr df_dy = (test_func<Expr>(x, y + h) - test_func<Expr>(x, y - h))/(2*h);
    ref(x, y) = sqrt(df_dx*df_dx + df_dy*df_dy);

    // Test the correctness of the above by numerically computing the
    // derivative of f and comparing it to the auto differentiation
    // result.
    int width = 50;
    int height = 50;
    Image<float> G = g.realize(width, height);
    Image<float> refG = ref.realize(width, height);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (std::abs(G(x, y) - refG(x, y)) > std::max(G(x, y), refG(x, y))*1e-3f + 1e-3f) {
                printf("Error at %d, %d: %g != %g\n", x, y, G(x, y), refG(x, y));
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
