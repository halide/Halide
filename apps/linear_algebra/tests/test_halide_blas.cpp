#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <cblas.h>
#include <halide_blas.h>

#define RUN_TEST(method)                                                \
    std::cout << std::setw(30) << ("Testing " #method ": ") << std::flush; \
    if (test_##method(N)) {                                             \
        std::cout << "PASSED\n";                                        \
    }                                                                   \

#define L1_VECTOR_TEST(method, code)            \
    bool test_##method(int N) {                 \
        Scalar alpha = random_scalar();         \
        Vector ex(random_vector(N));            \
        Vector ey(random_vector(N));            \
        Vector ax(ex), ay(ey);                  \
                                                \
        {                                       \
            Scalar *x = &(ex[0]);               \
            Scalar *y = &(ey[0]);               \
            cblas_##code;                       \
        }                                       \
                                                \
        {                                       \
            Scalar *x = &(ax[0]);               \
            Scalar *y = &(ay[0]);               \
            hblas_##code;                       \
        }                                       \
                                                \
        return compareVectors(N, ey, ay);       \
    }

#define L1_SCALAR_TEST(method, code)            \
    bool test_##method(int N) {                 \
        Scalar alpha = random_scalar();         \
        Vector ex(random_vector(N));            \
        Vector ey(random_vector(N));            \
        Vector ax(ex), ay(ey);                  \
        Scalar er, ar;                          \
                                                \
        {                                       \
            Scalar *x = &(ex[0]);               \
            Scalar *y = &(ey[0]);               \
            er = cblas_##code;                  \
        }                                       \
                                                \
        {                                       \
            Scalar *x = &(ax[0]);               \
            Scalar *y = &(ay[0]);               \
            ar = hblas_##code;                  \
        }                                       \
                                                \
        return compareScalars(er, ar);          \
    }

#define L2_TEST(method, cblas_code, hblas_code) \
    bool test_##method(int N) {                 \
        Scalar alpha = random_scalar();         \
        Scalar beta = random_scalar();          \
        Vector ex(random_vector(N));            \
        Vector ey(random_vector(N));            \
        Matrix eA(random_matrix(N));            \
        Vector ax(ex), ay(ey);                  \
        Matrix aA(eA);                          \
                                                \
        {                                       \
            Scalar *x = &(ex[0]);               \
            Scalar *y = &(ey[0]);               \
            Scalar *A = &(eA[0]);               \
            cblas_code;                         \
        }                                       \
                                                \
        {                                       \
            Scalar *x = &(ax[0]);               \
            Scalar *y = &(ay[0]);               \
            Scalar *A = &(aA[0]);               \
            hblas_code;                         \
        }                                       \
                                                \
        return compareVectors(N, ey, ay);       \
    }

#define L3_TEST(method, cblas_code, hblas_code) \
    bool test_##method(int N) {                 \
        Scalar alpha = random_scalar();         \
        Scalar beta = random_scalar();          \
        Matrix eA(random_matrix(N));            \
        Matrix eB(random_matrix(N));            \
        Matrix eC(random_matrix(N));            \
        Matrix aA(eA), aB(eB), aC(eC);          \
                                                \
        {                                       \
            Scalar *A = &(eA[0]);               \
            Scalar *B = &(eB[0]);               \
            Scalar *C = &(eC[0]);               \
            cblas_code;                         \
        }                                       \
                                                \
        {                                       \
            Scalar *A = &(aA[0]);               \
            Scalar *B = &(aB[0]);               \
            Scalar *C = &(aC[0]);               \
            hblas_code;                         \
        }                                       \
                                                \
        return compareMatrices(N, eC, aC);      \
    }


template<class T>
struct BLASTestBase {
    typedef T Scalar;
    typedef std::vector<T> Vector;
    typedef std::vector<T> Matrix;

    std::random_device rand_dev;
    std::default_random_engine rand_eng;

    BLASTestBase() : rand_eng(rand_dev()) {}

    Scalar random_scalar() {
        std::uniform_real_distribution<T> uniform_dist(0.0, 1.0);
        return uniform_dist(rand_eng);
    }

    Vector random_vector(int N) {
        Vector buff(N);
        for (int i=0; i<N; ++i) {
            buff[i] = random_scalar();
        }
        return buff;
    }

    Matrix random_matrix(int N) {
        Matrix buff(N * N);
        for (int i=0; i<N*N; ++i) {
            buff[i] = random_scalar();
        }
        return buff;
    }

    bool compareScalars(Scalar x, Scalar y, Scalar epsilon = 4 * std::numeric_limits<Scalar>::epsilon()) {
        if (x == y) {
            return true;
        } else {
            const Scalar min_normal = std::numeric_limits<Scalar>::min();

            Scalar ax = std::abs(x);
            Scalar ay = std::abs(y);
            Scalar diff = std::abs(x - y);

            bool equal = false;
            if (x == 0.0 || y == 0.0 || diff < min_normal) {
                equal = diff < (epsilon * min_normal);
            } else {
                equal = diff / (ax + ay) < epsilon;
            }

            if (!equal) {
                std::cerr << "FAIL! expected = " << x << ", actual = " << y << "\n";
            }

            return equal;
        }
    }

    bool compareVectors(int N, const Vector &x, const Vector &y,
                        Scalar epsilon = 16 * std::numeric_limits<Scalar>::epsilon()) {
        bool equal = true;
        for (int i = 0; i < N; ++i) {
            if (!compareScalars(x[i], y[i], epsilon)) {
                std::cerr << "Vectors differ at index: " << i << "\n";
                equal = false;
                break;
            }
        }
        return equal;
    }

    bool compareMatrices(int N, const Matrix &A, const Matrix &B,
                         Scalar epsilon = 16 * std::numeric_limits<Scalar>::epsilon()) {
        bool equal = true;
        for (int i = 0; i < N*N; ++i) {
            if (!compareScalars(A[i], B[i], epsilon)) {
                std::cerr << "Matrices differ at coords: (" << i%N << ", " << i/N << ")\n";
                equal = false;
                break;
            }
        }
        return equal;
    }
};

struct BLASFloatTests : public BLASTestBase<float> {
    void run_tests(int N) {
        RUN_TEST(scopy);
        RUN_TEST(sscal);
        RUN_TEST(saxpy);
        RUN_TEST(sdot);
        RUN_TEST(sasum);
        RUN_TEST(sgemv_notrans);
        RUN_TEST(sgemv_trans);
        RUN_TEST(sger);
        RUN_TEST(sgemm_notrans);
        RUN_TEST(sgemm_transA);
        RUN_TEST(sgemm_transB);
        RUN_TEST(sgemm_transAB);
    }

    L1_VECTOR_TEST(scopy, scopy(N, x, 1, y, 1))
    L1_VECTOR_TEST(sscal, sscal(N, alpha, y, 1))
    L1_VECTOR_TEST(saxpy, saxpy(N, alpha, x, 1, y, 1))

    L1_SCALAR_TEST(sdot, sdot(N, x, 1, y, 1))
    L1_SCALAR_TEST(sasum, sasum(N, x, 1))

    L2_TEST(sgemv_notrans,
            cblas_sgemv(CblasColMajor, CblasNoTrans, N, N, alpha, A, N, x, 1, beta, y, 1),
            hblas_sgemv(HblasColMajor, HblasNoTrans, N, N, alpha, A, N, x, 1, beta, y, 1));
    L2_TEST(sgemv_trans,
            cblas_sgemv(CblasColMajor, CblasTrans, N, N, alpha, A, N, x, 1, beta, y, 1),
            hblas_sgemv(HblasColMajor, HblasTrans, N, N, alpha, A, N, x, 1, beta, y, 1));
    L2_TEST(sger,
            cblas_sger(CblasColMajor, N, N, alpha, x, 1, y, 1, A, N),
            hblas_sger(HblasColMajor, N, N, alpha, x, 1, y, 1, A, N));

    L3_TEST(sgemm_notrans,
            cblas_sgemm(CblasColMajor, CblasNoTrans, CblasNoTrans, N, N, N, alpha, A, N, B, N, beta, C, N),
            hblas_sgemm(HblasColMajor, HblasNoTrans, HblasNoTrans, N, N, N, alpha, A, N, B, N, beta, C, N));
    L3_TEST(sgemm_transA,
            cblas_sgemm(CblasColMajor, CblasTrans, CblasNoTrans, N, N, N, alpha, A, N, B, N, beta, C, N),
            hblas_sgemm(HblasColMajor, HblasTrans, HblasNoTrans, N, N, N, alpha, A, N, B, N, beta, C, N));
    L3_TEST(sgemm_transB,
            cblas_sgemm(CblasColMajor, CblasNoTrans, CblasTrans, N, N, N, alpha, A, N, B, N, beta, C, N),
            hblas_sgemm(HblasColMajor, HblasNoTrans, HblasTrans, N, N, N, alpha, A, N, B, N, beta, C, N));
    L3_TEST(sgemm_transAB,
            cblas_sgemm(CblasColMajor, CblasTrans, CblasTrans, N, N, N, alpha, A, N, B, N, beta, C, N),
            hblas_sgemm(HblasColMajor, HblasTrans, HblasTrans, N, N, N, alpha, A, N, B, N, beta, C, N));
};

struct BLASDoubleTests : public BLASTestBase<double> {
    void run_tests(int N) {
        RUN_TEST(dcopy);
        RUN_TEST(dscal);
        RUN_TEST(daxpy);
        RUN_TEST(ddot);
        RUN_TEST(dasum);
        RUN_TEST(dgemv_notrans);
        RUN_TEST(dgemv_trans);
        RUN_TEST(dger);
        RUN_TEST(dgemm_notrans);
        RUN_TEST(dgemm_transA);
        RUN_TEST(dgemm_transB);
        RUN_TEST(dgemm_transAB);
    }

    L1_VECTOR_TEST(dcopy, dcopy(N, x, 1, y, 1))
    L1_VECTOR_TEST(dscal, dscal(N, alpha, y, 1))
    L1_VECTOR_TEST(daxpy, daxpy(N, alpha, x, 1, y, 1))

    L1_SCALAR_TEST(ddot, ddot(N, x, 1, y, 1))
    L1_SCALAR_TEST(dasum, dasum(N, x, 1))

    L2_TEST(dgemv_notrans,
            cblas_dgemv(CblasColMajor, CblasNoTrans, N, N, alpha, A, N, x, 1, beta, y, 1),
            hblas_dgemv(HblasColMajor, HblasNoTrans, N, N, alpha, A, N, x, 1, beta, y, 1));
    L2_TEST(dgemv_trans,
            cblas_dgemv(CblasColMajor, CblasTrans, N, N, alpha, A, N, x, 1, beta, y, 1),
            hblas_dgemv(HblasColMajor, HblasTrans, N, N, alpha, A, N, x, 1, beta, y, 1));
    L2_TEST(dger,
            cblas_dger(CblasColMajor, N, N, alpha, x, 1, y, 1, A, N),
            hblas_dger(HblasColMajor, N, N, alpha, x, 1, y, 1, A, N));

    L3_TEST(dgemm_notrans,
            cblas_dgemm(CblasColMajor, CblasNoTrans, CblasNoTrans, N, N, N, alpha, A, N, B, N, beta, C, N),
            hblas_dgemm(HblasColMajor, HblasNoTrans, HblasNoTrans, N, N, N, alpha, A, N, B, N, beta, C, N));
    L3_TEST(dgemm_transA,
            cblas_dgemm(CblasColMajor, CblasTrans, CblasNoTrans, N, N, N, alpha, A, N, B, N, beta, C, N),
            hblas_dgemm(HblasColMajor, HblasTrans, HblasNoTrans, N, N, N, alpha, A, N, B, N, beta, C, N));
    L3_TEST(dgemm_transB,
            cblas_dgemm(CblasColMajor, CblasNoTrans, CblasTrans, N, N, N, alpha, A, N, B, N, beta, C, N),
            hblas_dgemm(HblasColMajor, HblasNoTrans, HblasTrans, N, N, N, alpha, A, N, B, N, beta, C, N));
    L3_TEST(dgemm_transAB,
            cblas_dgemm(CblasColMajor, CblasTrans, CblasTrans, N, N, N, alpha, A, N, B, N, beta, C, N),
            hblas_dgemm(HblasColMajor, HblasTrans, HblasTrans, N, N, N, alpha, A, N, B, N, beta, C, N));
};

int main(int argc, char *argv[]) {
    BLASFloatTests  s;
    BLASDoubleTests d;


    if (argc > 1) {
        for (int i = 1; i < argc; ++i) {
            int size = std::stoi(argv[i]);
            std::cout << "Testing halide_blas with N = " << size << ":\n";
            s.run_tests(size);
            d.run_tests(size);
        }
    } else {
        int size = 64 * 7;
        std::cout << "Testing halide_blas with N = " << size << ":\n";
        s.run_tests(size);
        d.run_tests(size);
    }
}
