#include <stdio.h>

#ifdef WITH_EIGEN

#include <Halide.h>

#include <algorithm>
#include <iostream>

using namespace Halide;

bool approx_equal(float x, float y) {
    static const float p = Eigen::NumTraits<float>::dummy_precision();
    const float mag = p * std::abs(x) * std::abs(y);
    bool equal = std::abs(x - y) < mag;

    if (!equal) {
        std::cout << "Realized:\n" << x << std::endl;
        std::cout << "Expected:\n" << y << std::endl;
    }

    return equal;
}

template<class M>
bool same_as_matrix(const Buffer& buff, const Eigen::MatrixBase<M>& mat) {
    typedef typename M::Scalar  ScalarType;
    typedef Eigen::Matrix<ScalarType, M::RowsAtCompileTime, M::ColsAtCompileTime>
        MatrixType;

    ScalarType *data = reinterpret_cast<ScalarType*>(buff.host_ptr());
    Eigen::Map<MatrixType> A(data, mat.rows(), mat.cols());

    bool equal = A.isApprox(mat);

    if (!equal) {
        std::cout << "Realized:\n" << A << std::endl;
        std::cout << "Expected:\n" << mat << std::endl;
    }

    return equal;
}

template<int n>
bool test_matrix_operations() {
    bool success = true;

    printf("Testing matrix operations in %d-dimensions.\n", n);

    typedef Eigen::Matrix<float, n, n> EigenMatrix;

    EigenMatrix A, B;
    EigenMatrix u, v;

    A = EigenMatrix::Random();
    B = EigenMatrix::Random();
    u = EigenMatrix::Random();
    v = EigenMatrix::Random();

    printf("mat-mat multiply..");
    Matrix AB = Matrix(A) * Matrix(B);
    success &= same_as_matrix(AB.realize(), A*B);
    if (success) printf("success!\n");
    else { printf("fail\n"); return success; }

    printf("mat-vec multiply..");
    Matrix Au = Matrix(A) * Matrix(u);
    success &= same_as_matrix(Au.realize(), A*u);
    if (success) printf("success!\n");
    else { printf("fail\n"); return success; }

    printf("transpose..");
    B = A.transpose();
    Matrix At = Matrix(A).transpose();
    success &= same_as_matrix(At.realize(), B);
    if (success) printf("success!\n");
    else { printf("fail\n"); return success; }

    printf("inverse..");
    B = A.inverse();
    Matrix Ainv = Matrix(A).inverse();
    success &= same_as_matrix(Ainv.realize(), B);
    if (success) printf("success!\n");
    else { printf("fail\n"); return success; }

    printf("determinant..");
    float det = evaluate<float>(Matrix(A).determinant());
    success &= approx_equal(det, A.determinant());
    if (success) printf("success!\n");
    else { printf("fail\n"); return success; }

    printf("gemm..");
    Matrix result = 2 * Matrix(A) * Matrix(u) + Matrix(B) * Matrix(v) / 3.f;
    success &= same_as_matrix(result.realize(), 2.f*A*u + B*v/3.f);
    if (success) printf("success!\n");
    else { printf("fail\n"); return success; }

    return success;
}

bool test_matrix_operations(const int n) {
    bool success = true;

    printf("Testing matrix operations in %d-dimensions.\n", n);

    typedef Eigen::MatrixXf EigenMatrix;

    Eigen::MatrixXf A, B;
    Eigen::MatrixXf u, v;

    A = EigenMatrix::Random(n, n);
    B = EigenMatrix::Random(n, n);
    u = EigenMatrix::Random(n, 1);
    v = EigenMatrix::Random(n, 1);

    printf("mat-mat multiply..");
    Matrix AB = Matrix(A) * Matrix(B);
    Func f = AB.function();
    success &= same_as_matrix(AB.realize(), A*B);
    if (success) printf("success!\n");
    else { printf("fail\n"); return success; }

    printf("mat-vec multiply..");
    Matrix Au = Matrix(A) * Matrix(u);
    success &= same_as_matrix(Au.realize(), A*u);
    if (success) printf("success!\n");
    else { printf("fail\n"); return success; }

    printf("transpose..");
    B = A.transpose();
    Matrix At = Matrix(A).transpose();
    success &= same_as_matrix(At.realize(), B);
    if (success) printf("success!\n");
    else { printf("fail\n"); return success; }

    printf("gemm..");
    Matrix result = 2 * Matrix(A) * Matrix(u) + Matrix(B) * Matrix(v) / 3;
    success &= same_as_matrix(result.realize(), 2*A*u + B*v/3);
    if (success) printf("success!\n");
    else { printf("fail\n"); return success; }

    return success;
}

int main(int argc, char **argv) {
    bool success = true;

    success &= test_matrix_operations<1>();
    success &= test_matrix_operations<2>();
    success &= test_matrix_operations<3>();
    success &= test_matrix_operations<4>();

    for (int n = 5; n <= 10; ++n ) {
        success &= test_matrix_operations(n);
    }

    if (success) {
      printf("Success!\n");
    } else {
      printf("Failure\n");
    }

    return 0;
}

#else

int main() {
    printf("Not running test matrix_ops, since Eigen is not available.\n");
}

#endif
