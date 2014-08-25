#include <stdio.h>

#ifdef WITH_EIGEN

#include <Halide.h>

#include <math.h>
#include <algorithm>

using namespace Halide;

template<class M>
bool same_as_matrix(const Buffer& buff, const Eigen::MatrixBase<M>& mat) {
    typedef typename M::Scalar  ScalarType;
    typedef Eigen::Matrix<ScalarType, M::RowsAtCompileTime, M::ColsAtCompileTime>
        MatrixType;

    ScalarType *data = reinterpret_cast<ScalarType*>(buff.host_ptr());
    Eigen::Map<MatrixType> A(data, mat.rows(), mat.cols());
    return A == mat;
}

template<int n>
bool test_matrix_operations() {
    bool success = true;
    
    typedef Eigen::Matrix<float, n, n> EigenMatrix;

    EigenMatrix A, B;
    EigenMatrix u, v;

    A = EigenMatrix::Random();
    B = EigenMatrix::Random();
    u = EigenMatrix::Random();
    v = EigenMatrix::Random();

    Matrix AB = Matrix(A) * Matrix(B);
    success &= same_as_matrix(AB.function().realize(), A*B);

    Matrix Au = Matrix(A) * Matrix(u);
    success &= same_as_matrix(Au.function().realize(), A*u);

    B = A.transpose();
    Matrix At = Matrix(A).transpose();
    success &= same_as_matrix(At.function().realize(), B);

    B = A.inverse();
    Matrix Ainv = Matrix(A).inverse();
    success &= same_as_matrix(Ainv.function().realize(), B);

    float det = evaluate<float>(Matrix(A).determinant());
    success &= det = A.determinant();

    return success;
}

bool test_matrix_operations(const int n) {
    bool success = true;

    typedef Eigen::MatrixXf EigenMatrix;

    Eigen::MatrixXf A, B;
    Eigen::MatrixXf u, v;

    A = EigenMatrix::Random(n, n);
    B = EigenMatrix::Random(n, n);
    u = EigenMatrix::Random(n, 1);
    v = EigenMatrix::Random(n, 1);

    Matrix AB = Matrix(A) * Matrix(B);
    success &= same_as_matrix(AB.function().realize(), A*B);

    Matrix Au = Matrix(A) * Matrix(u);
    success &= same_as_matrix(Au.function().realize(), A*u);

    B = A.transpose();
    Matrix At = Matrix(A).transpose();
    success &= same_as_matrix(At.function().realize(), B);

    Matrix result = 2 * Matrix(A) * Matrix(u) + Matrix(B) * Matrix(v) / 3;
    success &= same_as_matrix(result.function().realize(), 2*A*u + B*v/3);

    return success;
}

int main(int argc, char **argv) {
    bool success = true;

    for (int n = 5; n <= 10; ++n ) {
        success &= test_matrix_operations(n);
    }
}

#else

int main() {
    printf("Not running test matrix_ops, since Eigen is not available.\n");
}

#endif

