#include <stdio.h>
#include <Halide.h>

using namespace Halide;

#if __cplusplus > 199711L
#define CPP11
#endif

// Define a matrix class where the elements are Halide expressions.
template <int M, int N>
class Matrix {
    Tuple m;

public:
    Matrix() : m(std::vector<Expr>(M * N)) {}

    // To work with Halide, we need a conversion to/from Tuple.
    explicit Matrix(Tuple m) : m(m) {
        assert(m.size() == M*N);
    }
    operator Tuple() const { return m; }

    Expr & operator() (int i, int j) {
        assert(0 <= i && i < M);
        assert(0 <= j && j < N);
        return m[i*N + j];
    }

    // Assume vectors are column vectors.
    Expr & operator() (int i) {
#ifdef CPP11
        // If we have C++11, using this operator on a matrix that
        // isn't a row or column vector will be a compile time error.
        static_assert(M == 1 || N == 1, "Matrix is not a vector.");
#else
        assert(M == 1 || N == 1);
#endif
        assert(0 <= i && i < m.size());
        return m[i];
    }
};

// Multiplication for matrices a, b. a must be MxN, and b must be NxK.
template<int M, int N, int K>
Matrix<M, K> operator * (Matrix<M, N> a, Matrix<N, K> b) {
    Matrix<M, K> c;
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < K; j++) {
            c(i, j) = 0.0f;
            for (int k = 0; k < N; k++) {
                c(i, j) += a(i, k)*b(k, j);
            }
        }
    }
    return c;
}

// Scalar multiplication for matrices.
template <int M, int N>
Matrix<M, N> operator * (Matrix<M, N> a, Expr b) {
    Matrix<M, N> c;
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < N; j++) {
            c(i, j) = a(i, j)*b;
        }
    }
    return c;
}

template <int M, int N>
Matrix<M, N> operator * (Expr a, Matrix<M, N> b) {
    Matrix<M, N> c;
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < N; j++) {
            c(i, j) = a*b(i, j);
        }
    }
    return c;
}

// Color is a column vector of 3 components.
typedef Matrix<3, 1> Color;
typedef FuncT<Color> ColorFunc;

// Make a color vector.
Color make_color(Expr r, Expr g, Expr b) {
    Color ret;
    ret(0) = r;
    ret(1) = g;
    ret(2) = b;
    return ret;
}

int main(int argc, char **argv) {

    Var x("x"), y("y");
    // Define a function returning colors. This operation is type
    // safe, the RHS of the assignment must be a Color, f(x, y) = x;
    // or f(x, y) = Tuple(x, y); are an error.
    ColorFunc f("f");
    f(x, y) = make_color(x, y, x + y);

    // Scalar multiplication of a vector.
    ColorFunc g("g");
    g(x, y) = f(x, y) * 3.0f;

    // Matrix-vector multiplication.
    Matrix<3, 3> yuv_rgb;
    yuv_rgb(0, 0) =  0.299f; yuv_rgb(0, 1) =  0.587f; yuv_rgb(0, 2) =  0.114f;
    yuv_rgb(1, 0) = -0.147f; yuv_rgb(1, 1) = -0.289f; yuv_rgb(1, 2) =  0.436f;
    yuv_rgb(2, 0) =  0.615f; yuv_rgb(2, 1) = -0.515f; yuv_rgb(2, 2) = -0.100f;

    ColorFunc h("h");
    // Use the overloaded operator * to perform matrix-vector multiplication.
    // Note that the legality of this operation is enforced by the compiler;
    // g(x, y) * yuv_rgb would be a compile time error.
    h(x, y) = yuv_rgb * g(x, y);

    f.compute_root();
    g.compute_root();
    h.compute_root();

    // Test the correctness of the above.
    int width = 20;
    int height = 20;
    Realization R = h.realize(width, height);
    assert(R.size() == 3);
    Image<float> Y = R[0];
    Image<float> U = R[1];
    Image<float> V = R[2];
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float r_xy = x;
            float g_xy = y;
            float b_xy = x + y;

            r_xy *= 3.0f;
            g_xy *= 3.0f;
            b_xy *= 3.0f;

            float y_xy =  0.299f*r_xy + 0.587f*g_xy + 0.114f*b_xy;
            float u_xy = -0.147f*r_xy - 0.289f*g_xy + 0.436f*b_xy;
            float v_xy =  0.615f*r_xy - 0.515f*g_xy - 0.100f*b_xy;

            if (std::abs(y_xy - Y(x, y)) > 1e-6f ||
                std::abs(u_xy - U(x, y)) > 1e-6f ||
                std::abs(v_xy - V(x, y)) > 1e-6f) {
                printf("Error at %d, %d: (%f, %f, %f) != (%f, %f, %f)\n",
                       x, y,
                       y_xy, u_xy, v_xy,
                       Y(x, y), U(x, y), V(x, y));
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
