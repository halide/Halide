#include <type_traits>

#include "Halide.h"

using namespace Halide;

enum class Backend {
    CPU,
    CPUVectorize,
    OpenCL,
    CUDA,
    CUDAVectorize
};

template<typename T,
         typename std::enable_if<std::is_floating_point<T>::value>::type * = nullptr>
inline void check(int line_number, T x, T target, T threshold = T(1e-6)) {
    _halide_user_assert(std::fabs((x) - (target)) < threshold)
        << "Line " << line_number
        << ": Expected " << (target)
        << " instead of " << (x) << "\n";
}

inline void check(int line_number, float16_t x, float16_t target) {
    return check(line_number, (double)x, (double)target, 5e-3);
}

inline void check(int line_number, bfloat16_t x, bfloat16_t target) {
    return check(line_number, (double)x, (double)target, 5e-3);
}

template<typename T,
         typename std::enable_if<std::is_integral<T>::value, int>::type * = nullptr>
inline void check(int line_number, T x, T target) {
    _halide_user_assert(x == target)
        << "Line " << line_number
        << ": Expected " << (int64_t)(target)
        << " instead of " << (int64_t)(x) << "\n";
}

template<typename T>
void test_parallel_hist(const Backend &backend) {
    int img_size = 10000;
    int hist_size = 7;

    Func im, hist;
    Var x;
    RDom r(0, img_size);

    im(x) = (x * x) % hist_size;

    hist(x) = cast<T>(0);
    hist(im(r)) += cast<T>(1);

    Type t = cast<T>(0).type();
    bool is_float_16 = t.is_float() && t.bits() == 16;

    hist.compute_root();
    switch (backend) {
    case Backend::CPU: {
        if (is_float_16) {
            // Associativity prover doesn't support float16.
            // Set override_associativity_test to true to remove the check.
            hist.update()
                .atomic(true /*override_associativity_test*/)
                .parallel(r);
        } else {
            hist.update()
                .atomic()
                .parallel(r);
        }
    } break;
    case Backend::CPUVectorize: {
        RVar ro, ri;
        if (is_float_16) {
            // Associativity prover doesn't support float16.
            // Set override_associativity_test to true to remove the check.
            hist.update()
                .atomic(true /*override_associativity_test*/)
                .split(r, ro, ri, 8)
                .parallel(ro)
                .vectorize(ri);
        } else {
            hist.update()
                .atomic()
                .split(r, ro, ri, 8)
                .parallel(ro)
                .vectorize(ri);
        }
    } break;
    case Backend::OpenCL: {
        RVar ro, ri;
        if (is_float_16) {
            // Associativity prover doesn't support float16.
            // Set override_associativity_test to true to remove the check.
            hist.update()
                .atomic(true /*override_associativity_test*/)
                .split(r, ro, ri, 32)
                .gpu_blocks(ro, DeviceAPI::OpenCL)
                .gpu_threads(ri, DeviceAPI::OpenCL);
        } else {
            hist.update()
                .atomic()
                .split(r, ro, ri, 32)
                .gpu_blocks(ro, DeviceAPI::OpenCL)
                .gpu_threads(ri, DeviceAPI::OpenCL);
        }
    } break;
    case Backend::CUDA: {
        if (is_float_16) {
            RVar ro, ri;
            // Associativity prover doesn't support float16.
            // Set override_associativity_test to true to remove the check.
            hist.update()
                .atomic(true /*override_associativity_test*/)
                .split(r, ro, ri, 32)
                .gpu_blocks(ro, DeviceAPI::CUDA)
                .gpu_threads(ri, DeviceAPI::CUDA);
        } else {
            RVar ro, ri;
            hist.update()
                .atomic()
                .split(r, ro, ri, 32)
                .gpu_blocks(ro, DeviceAPI::CUDA)
                .gpu_threads(ri, DeviceAPI::CUDA);
        }
    } break;
    case Backend::CUDAVectorize: {
        RVar ro, ri;
        RVar rio, rii;
        hist.update()
            .atomic()
            .split(r, ro, ri, 32)
            .split(ri, rio, rii, 4)
            .gpu_blocks(ro, DeviceAPI::CUDA)
            .gpu_threads(rio, DeviceAPI::CUDA)
            .vectorize(rii);
    } break;
    default: {
        _halide_user_assert(false) << "Unsupported backend.\n";
    } break;
    }

    Buffer<T> correct(hist_size);
    correct.fill(T(0));
    for (int i = 0; i < img_size; i++) {
        int idx = (i * i) % hist_size;
        correct(idx) = correct(idx) + T(1);
    }

    // Run 10 times to make sure race condition do happen
    for (int iter = 0; iter < 10; iter++) {
        Buffer<T> out = hist.realize({hist_size});
        for (int i = 0; i < hist_size; i++) {
            check(__LINE__, out(i), correct(i));
        }
    }
}

template<typename T>
void test_parallel_cas_update(const Backend &backend) {
    int img_size = 1000;
    int hist_size = 13;

    Func im, hist;
    Var x;
    RDom r(0, img_size);

    im(x) = (x * x) % hist_size;

    hist(x) = cast<T>(0);
    // Can't do this with atomic rmw, need to generate a CAS loop
    hist(im(r)) = min(hist(im(r)) + cast<T>(1), cast<T>(100));

    hist.compute_root();
    switch (backend) {
    case Backend::CPU: {
        // Halide cannot prove that this is associative.
        // Set override_associativity_test to true to remove the check.
        hist.update()
            .atomic(true /*override_associativity_test*/)
            .parallel(r);
    } break;
    case Backend::CPUVectorize: {
        RVar ro, ri;
        // Halide cannot prove that this is associative.
        // Set override_associativity_test to true to remove the check.
        hist.update()
            .atomic(true /*override_associativity_test*/)
            .split(r, ro, ri, 8)
            .parallel(ro)
            .vectorize(ri);
    } break;
    case Backend::OpenCL: {
        RVar ro, ri;
        // Halide cannot prove that this is associative.
        // Set override_associativity_test to true to remove the check.
        hist.update()
            .atomic(true /*override_associativity_test*/)
            .split(r, ro, ri, 32)
            .gpu_blocks(ro, DeviceAPI::OpenCL)
            .gpu_threads(ri, DeviceAPI::OpenCL);
    } break;
    case Backend::CUDA: {
        RVar ro, ri;
        // Halide cannot prove that this is associative.
        // Set override_associativity_test to true to remove the check.
        hist.update()
            .atomic(true /*override_associativity_test*/)
            .split(r, ro, ri, 32)
            .gpu_blocks(ro, DeviceAPI::CUDA)
            .gpu_threads(ri, DeviceAPI::CUDA);
    } break;
    case Backend::CUDAVectorize: {
        RVar ro, ri;
        RVar rio, rii;
        hist.update()
            .atomic(true /*override_assciativity_test*/)
            .split(r, ro, ri, 32)
            .split(ri, rio, rii, 4)
            .gpu_blocks(ro, DeviceAPI::CUDA)
            .gpu_threads(rio, DeviceAPI::CUDA)
            .vectorize(rii);
    } break;
    default: {
        _halide_user_assert(false) << "Unsupported backend.\n";
    } break;
    }

    Buffer<T> correct(hist_size);
    correct.fill(T(0));
    for (int i = 0; i < img_size; i++) {
        int idx = (i * i) % hist_size;
        T x = correct(idx) + T(1);
        correct(idx) = x < T(100) ? x : T(100);
    }

    // Run 10 times to make sure race condition do happen
    for (int iter = 0; iter < 10; iter++) {
        Buffer<T> out = hist.realize({hist_size});
        for (int i = 0; i < hist_size; i++) {
            check(__LINE__, out(i), correct(i));
        }
    }
}

template<typename T>
void test_parallel_hist_tuple(const Backend &backend) {
    int img_size = 10000;
    int hist_size = 7;

    Func im, hist;
    Var x;
    RDom r(0, img_size);

    im(x) = (x * x) % hist_size;

    hist(x) = Tuple(cast<T>(0), cast<T>(0));
    hist(im(r)) += Tuple(cast<T>(1), cast<T>(2));

    Type t = cast<T>(0).type();
    bool is_float_16 = t.is_float() && t.bits() == 16;

    hist.compute_root();
    if (is_float_16) {
        // Associativity prover doesn't support float16,
        // Set override_associativity_test to true to remove the check.
        hist.update()
            .atomic(true /*override_associativity_test*/)
            .parallel(r);
    } else {
        hist.update()
            .atomic()
            .parallel(r);
    }

    Buffer<T> correct0(hist_size);
    Buffer<T> correct1(hist_size);
    correct0.fill(T(0));
    correct1.fill(T(0));
    for (int i = 0; i < img_size; i++) {
        int idx = (i * i) % hist_size;
        correct0(idx) = correct0(idx) + T(1);
        correct1(idx) = correct1(idx) + T(2);
    }

    // Run 10 times to make sure race condition do happen
    for (int iter = 0; iter < 10; iter++) {
        Realization out = hist.realize({hist_size});
        Buffer<T> out0 = out[0];
        Buffer<T> out1 = out[1];
        for (int i = 0; i < hist_size; i++) {
            check(__LINE__, out0(i), correct0(i));
            check(__LINE__, out1(i), correct1(i));
        }
    }
}

template<typename T>
void test_predicated_hist(const Backend &backend) {
    int img_size = 1000;
    int hist_size = 13;

    Func im, hist;
    Var x;
    RDom r(0, img_size);
    r.where(r % 2 == 0);

    im(x) = (x * x) % hist_size;

    hist(x) = cast<T>(0);
    hist(im(r)) += cast<T>(1);                                  // atomic add
    hist(im(r)) -= cast<T>(1);                                  // atomic add
    hist(im(r)) = min(hist(im(r)) + cast<T>(1), cast<T>(100));  // cas loop

    RDom r2(0, img_size);
    // This predicate means that the update definitions below can't actually be
    // atomic, because the if isn't included in the atomic block.
    r2.where(hist(im(r2)) > cast<T>(0) && hist(im(r2)) < cast<T>(90));
    hist(im(r2)) -= cast<T>(1);
    hist(im(r2)) = min(hist(im(r2)) + cast<T>(1), cast<T>(100));

    hist.update(3).unscheduled();
    hist.update(4).unscheduled();

    hist.compute_root();
    for (int update_id = 0; update_id < 3; update_id++) {
        switch (backend) {
        case Backend::CPU: {
            // Can't prove associativity.
            // Set override_associativity_test to true to remove the check.
            hist.update(update_id)
                .atomic(true /*override_associativity_test*/)
                .parallel(r);
        } break;
        case Backend::CPUVectorize: {
            // Doesn't support predicated store yet.
            _halide_user_assert(false) << "Unsupported backend.\n";
        } break;
        case Backend::OpenCL: {
            // Can't prove associativity.
            // Set override_associativity_test to true to remove the check.
            RVar ro, ri;
            hist.update(update_id)
                .atomic(true /*override_associativity_test*/)
                .split(r, ro, ri, 32)
                .gpu_blocks(ro, DeviceAPI::OpenCL)
                .gpu_threads(ri, DeviceAPI::OpenCL);
        } break;
        case Backend::CUDA: {
            // Can't prove associativity.
            // Set override_associativity_test to true to remove the check.
            RVar ro, ri;
            hist.update(update_id)
                .atomic(true /*override_associativity_test*/)
                .split(r, ro, ri, 32)
                .gpu_blocks(ro, DeviceAPI::CUDA)
                .gpu_threads(ri, DeviceAPI::CUDA);
        } break;
        case Backend::CUDAVectorize: {
            RVar ro, ri;
            RVar rio, rii;
            hist.update(update_id)
                .atomic(true /*override_assciativity_test*/)
                .split(r, ro, ri, 32)
                .split(ri, rio, rii, 4)
                .gpu_blocks(ro, DeviceAPI::CUDA)
                .gpu_threads(rio, DeviceAPI::CUDA)
                .vectorize(rii);
        } break;
        default: {
            _halide_user_assert(false) << "Unsupported backend.\n";
        } break;
        }
    }

    Buffer<T> correct(hist_size);
    correct.fill(T(0));
    for (int i = 0; i < img_size; i++) {
        if (i % 2 != 0) {
            continue;
        }
        int idx = (i * i) % hist_size;
        correct(idx) = correct(idx) + T(1);
        correct(idx) = correct(idx) - T(1);
        T x = correct(idx) + T(1);
        correct(idx) = x < T(100) ? x : T(100);
    }
    for (int i = 0; i < img_size; i++) {
        int idx = (i * i) % hist_size;
        if (correct(idx) <= T(0) || correct(idx) >= T(90)) {
            continue;
        }
        correct(idx) = correct(idx) - T(1);
    }
    for (int i = 0; i < img_size; i++) {
        int idx = (i * i) % hist_size;
        if (correct(idx) <= T(0) || correct(idx) >= T(90)) {
            continue;
        }
        T x = correct(idx) + T(1);
        correct(idx) = x < T(100) ? x : T(100);
    }

    // Run 10 times to make sure race condition do happen
    for (int iter = 0; iter < 10; iter++) {
        Buffer<T> out = hist.realize({hist_size});
        for (int i = 0; i < hist_size; i++) {
            check(__LINE__, out(i), correct(i));
        }
    }
}

template<typename T>
void test_parallel_hist_tuple2(const Backend &backend) {
    int img_size = 10000;
    int hist_size = 7;

    Func im, hist;
    Var x;
    RDom r(0, img_size);

    im(x) = (x * x) % hist_size;

    hist(x) = Tuple(cast<T>(0), cast<T>(0));
    // Swap the tuple when updating.
    hist(im(r)) = Tuple(hist(im(r))[1] + cast<T>(1),
                        hist(im(r))[0] + cast<T>(2));

    im.compute_root();
    hist.compute_root();
    switch (backend) {
    case Backend::CPU: {
        // Halide cannot prove that this is associative.
        // Set override_associativity_test to true to remove the check.
        hist.update()
            .atomic(true /*override_associativity_test*/)
            .parallel(r);
    } break;
    default: {
        // All other backends do not support mutex locking.
        _halide_user_assert(false) << "Unsupported backend.\n";
    }
    }

    Buffer<T> correct0(hist_size);
    Buffer<T> correct1(hist_size);
    correct0.fill(T(0));
    correct1.fill(T(0));
    for (int i = 0; i < img_size; i++) {
        int idx = (i * i) % hist_size;
        T new_c0 = correct1(idx) + T(1);
        T new_c1 = correct0(idx) + T(2);
        correct0(idx) = new_c0;
        correct1(idx) = new_c1;
    }

    // Run 10 times to make sure race condition do happen
    for (int iter = 0; iter < 10; iter++) {
        Realization out = hist.realize({hist_size});
        Buffer<T> out0 = out[0];
        Buffer<T> out1 = out[1];
        for (int i = 0; i < hist_size; i++) {
            check(__LINE__, out0(i), correct0(i));
            check(__LINE__, out1(i), correct1(i));
        }
    }
}

template<typename T>
void test_tuple_reduction(const Backend &backend) {
    int img_size = 10000;

    Func im, arg_max;
    Var x;
    RDom r(0, img_size);

    im(x) = cast<T>(120.f * abs(sin(cast<float>(x))));
    // Make sure there is only one winner for argmax
    im(1234) = cast<T>(125);

    arg_max() = {0, im(0)};
    Expr old_index = arg_max()[0];
    Expr old_max = arg_max()[1];
    Expr new_index = select(old_max < im(r), r, old_index);
    Expr new_max = max(im(r), old_max);
    arg_max() = {new_index, new_max};

    arg_max.compute_root();
    switch (backend) {
    case Backend::CPU: {
        // This is in fact not an associative reduction if
        // there is more than one winner.
        arg_max.update()
            .atomic(true /*override_associativity_test*/)
            .parallel(r);
    } break;
    default: {
        // All other backends do not support mutex locking.
        _halide_user_assert(false) << "Unsupported backend.\n";
    }
    }

    // Run 10 times to make sure race condition do happen
    for (int iter = 0; iter < 10; iter++) {
        Realization out = arg_max.realize();
        Buffer<int> out0 = out[0];
        Buffer<T> out1 = out[1];
        check(__LINE__, out0(0), 1234);
        check(__LINE__, out1(0), T(125));
    }
}

template<typename T>
void test_nested_atomics(const Backend &backend) {
    int img_size = 10000;

    Func im, arg_max;
    Var x;
    RDom r(0, img_size);

    im(x) = cast<T>(120.f * abs(sin(cast<float>(x))));
    // Make sure there is only one winner for argmax
    im(1234) = cast<T>(125);

    arg_max() = {0, im(0)};
    Expr old_index = arg_max()[0];
    Expr old_max = arg_max()[1];
    Expr new_index = select(old_max < im(r), r, old_index);
    Expr new_max = max(im(r), old_max);
    arg_max() = {new_index, new_max};

    im.compute_inline().atomic().update().atomic();
    arg_max.compute_root();
    switch (backend) {
    case Backend::CPU: {
        // This is in fact not an associative reduction if
        // there is more than one winner.
        arg_max.update()
            .atomic(true /*override_associativity_test*/)
            .parallel(r);
    } break;
    default: {
        // All other backends do not support mutex locking.
        _halide_user_assert(false) << "Unsupported backend.\n";
    }
    }

    // Run 10 times to make sure race condition do happen
    for (int iter = 0; iter < 10; iter++) {
        Realization out = arg_max.realize();
        Buffer<int> out0 = out[0];
        Buffer<T> out1 = out[1];
        check(__LINE__, out0(0), 1234);
        check(__LINE__, out1(0), T(125));
    }
}

template<typename T>
void test_hist_compute_at(const Backend &backend) {
    int img_size = 1000;
    int hist_size = 53;

    Func im, hist, final;
    Var x, y;
    RDom r(0, img_size);

    im(x) = (x * x) % hist_size;

    hist(x) = cast<T>(0);
    hist(im(r)) += cast<T>(1);

    final(x, y) = hist((x + y) % hist_size);

    Type t = cast<T>(0).type();
    bool is_float_16 = t.is_float() && t.bits() == 16;

    final.compute_root().parallel(y);
    hist.compute_at(final, y);
    switch (backend) {
    case Backend::CPU: {
        if (is_float_16) {
            // Associativity prover doesn't support float16.
            // Set override_associativity_test to true to remove the check.
            hist.update()
                .atomic(true /*override_associativity_test*/)
                .parallel(r);
        } else {
            hist.update()
                .atomic()
                .parallel(r);
        }
    } break;
    case Backend::CPUVectorize: {
        RVar ro, ri;
        if (is_float_16) {
            // Associativity prover doesn't support float16.
            // Set override_associativity_test to true to remove the check.
            hist.update()
                .atomic(true /*override_associativity_test*/)
                .split(r, ro, ri, 8)
                .parallel(ro)
                .vectorize(ri);
        } else {
            hist.update()
                .atomic()
                .split(r, ro, ri, 8)
                .parallel(ro)
                .vectorize(ri);
        }
    } break;
    case Backend::OpenCL: {
        RVar ro, ri;
        if (is_float_16) {
            // Associativity prover doesn't support float16.
            // Set override_associativity_test to true to remove the check.
            hist.update().atomic(true /*override_associativity_test*/).split(r, ro, ri, 32).gpu_blocks(ro, DeviceAPI::OpenCL).gpu_threads(ri, DeviceAPI::OpenCL);
        } else {
            hist.update().atomic().split(r, ro, ri, 32).gpu_blocks(ro, DeviceAPI::OpenCL).gpu_threads(ri, DeviceAPI::OpenCL);
        }
    } break;
    case Backend::CUDA: {
        RVar ro, ri;
        hist.update().atomic().split(r, ro, ri, 32).gpu_blocks(ro, DeviceAPI::CUDA).gpu_threads(ri, DeviceAPI::CUDA);
    } break;
    case Backend::CUDAVectorize: {
        RVar ro, ri;
        RVar rio, rii;
        hist.update().atomic().split(r, ro, ri, 32).split(ri, rio, rii, 4).gpu_blocks(ro, DeviceAPI::CUDA).gpu_threads(rio, DeviceAPI::CUDA).vectorize(rii);
    } break;
    default: {
        _halide_user_assert(false) << "Unsupported backend.\n";
    } break;
    }

    Buffer<T> correct_hist(hist_size);
    Buffer<T> correct_final(10, 10);
    correct_hist.fill(T(0));
    correct_final.fill(T(0));
    for (int i = 0; i < img_size; i++) {
        int idx = (i * i) % hist_size;
        correct_hist(idx) = correct_hist(idx) + T(1);
    }
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            correct_final(i, j) = correct_hist((i + j) % hist_size);
        }
    }

    // Run 10 times to make sure race condition do happen
    for (int iter = 0; iter < 10; iter++) {
        Buffer<T> out = final.realize({10, 10});
        for (int i = 0; i < 10; i++) {
            for (int j = 0; j < 10; j++) {
                check(__LINE__, out(i, j), correct_final(i, j));
            }
        }
    }
}

template<typename T>
void test_hist_tuple_compute_at(const Backend &backend) {
    int img_size = 1000;
    int hist_size = 7;

    Func im, hist, final;
    Var x, y;
    RDom r(0, img_size);

    im(x) = (x * x) % hist_size;

    hist(x) = Tuple(cast<T>(0), cast<T>(0));
    // Swap the tuple when updating.
    hist(im(r)) = Tuple(hist(im(r))[1] + cast<T>(1),
                        hist(im(r))[0] + cast<T>(2));

    final(x, y) = hist((x + y) % hist_size);

    final.compute_root().parallel(y);
    hist.compute_at(final, y);
    switch (backend) {
    case Backend::CPU: {
        // Halide cannot prove that this is associative.
        // Set override_associativity_test to true to remove the check.
        hist.update().atomic(true /*override_associativity_test*/).parallel(r);
    } break;
    default: {
        // All other backends do not support mutex locking.
        _halide_user_assert(false) << "Unsupported backend.\n";
    }
    }

    Buffer<T> correct_hist0(hist_size);
    Buffer<T> correct_hist1(hist_size);
    correct_hist0.fill(T(0));
    correct_hist1.fill(T(0));
    for (int i = 0; i < img_size; i++) {
        int idx = (i * i) % hist_size;
        T new_c0 = correct_hist1(idx) + T(1);
        T new_c1 = correct_hist0(idx) + T(2);
        correct_hist0(idx) = new_c0;
        correct_hist1(idx) = new_c1;
    }
    Buffer<T> correct_final0(10, 10);
    Buffer<T> correct_final1(10, 10);
    correct_final0.fill(T(0));
    correct_final1.fill(T(0));
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            correct_final0(i, j) = correct_hist0((i + j) % hist_size);
            correct_final1(i, j) = correct_hist1((i + j) % hist_size);
        }
    }

    // Run 10 times to make sure race condition do happen
    for (int iter = 0; iter < 10; iter++) {
        Realization out = final.realize({10, 10});
        Buffer<T> out0 = out[0];
        Buffer<T> out1 = out[1];
        for (int i = 0; i < 10; i++) {
            for (int j = 0; j < 10; j++) {
                check(__LINE__, out0(i, j), correct_final0(i, j));
                check(__LINE__, out1(i, j), correct_final1(i, j));
            }
        }
    }
}

template<typename T>
void test_hist_store_at(const Backend &backend) {
    int img_size = 1000;
    int hist_size = 53;

    Func im, hist, final;
    Var x, y;
    RDom r(0, img_size);

    im(x) = (x * x) % hist_size;

    hist(x) = cast<T>(0);
    hist(im(r)) += cast<T>(1);

    final(x, y) = hist((x + y) % hist_size);

    Type t = cast<T>(0).type();
    bool is_float_16 = t.is_float() && t.bits() == 16;

    final.compute_root().parallel(y);
    hist.store_at(final, y)
        .compute_at(final, x);
    switch (backend) {
    case Backend::CPU: {
        if (is_float_16) {
            // Associativity prover doesn't support float16.
            // Set override_associativity_test to true to remove the check.
            hist.update().atomic(true /*override_associativity_test*/).parallel(r);
        } else {
            hist.update().atomic().parallel(r);
        }
    } break;
    case Backend::CPUVectorize: {
        RVar ro, ri;
        if (is_float_16) {
            // Associativity prover doesn't support float16.
            // Set override_associativity_test to true to remove the check.
            hist.update()
                .atomic(true /*override_associativity_test*/)
                .split(r, ro, ri, 8)
                .parallel(ro)
                .vectorize(ri);
        } else {
            hist.update()
                .atomic()
                .split(r, ro, ri, 8)
                .parallel(ro)
                .vectorize(ri);
        }
    } break;
    default: {
        _halide_user_assert(false) << "Unsupported backend.\n";
    }
    }

    Buffer<T> correct_hist(hist_size);
    Buffer<T> correct_final(10, 10);
    correct_hist.fill(T(0));
    correct_final.fill(T(0));
    for (int i = 0; i < img_size; i++) {
        int idx = (i * i) % hist_size;
        correct_hist(idx) = correct_hist(idx) + T(1);
    }
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            correct_final(i, j) = correct_hist((i + j) % hist_size);
        }
    }

    // Run 10 times to make sure race condition do happen
    for (int iter = 0; iter < 10; iter++) {
        Buffer<T> out = final.realize({10, 10});
        for (int i = 0; i < 10; i++) {
            for (int j = 0; j < 10; j++) {
                check(__LINE__, out(i, j), correct_final(i, j));
            }
        }
    }
}

template<typename T>
void test_hist_rfactor(const Backend &backend) {
    Type t = cast<T>(0).type();
    bool is_float_16 = t.is_float() && t.bits() == 16;
    if (is_float_16) {
        // rfactor doesn't support float16 yet.
        return;
    }

    int img_size = 100;
    int hist_size = 7;

    Func im, hist;
    Var x, y;
    RDom r(0, img_size, 0, img_size);
    im(x, y) = ((x + 1) * (y + 1)) % hist_size;
    hist(x) = cast<T>(0);
    hist(im(r.x, r.y)) += cast<T>(1);

    Func intermediate =
        hist.update()
            .rfactor(r.y, y);
    intermediate.compute_root();
    hist.compute_root();
    switch (backend) {
    case Backend::CPU: {
        intermediate.update().atomic().parallel(r.x);
    } break;
    case Backend::CPUVectorize: {
        RVar ro, ri;
        intermediate.update()
            .atomic()
            .split(r.x, ro, ri, 8)
            .parallel(ro)
            .vectorize(ri);
    } break;
    case Backend::OpenCL: {
        RVar ro, ri;
        intermediate.update()
            .atomic()
            .split(r.x, ro, ri, 8)
            .gpu_blocks(ro, DeviceAPI::OpenCL)
            .gpu_threads(ri, DeviceAPI::OpenCL);
    } break;
    case Backend::CUDA: {
        RVar ro, ri;
        intermediate.update()
            .atomic()
            .split(r.x, ro, ri, 8)
            .gpu_blocks(ro, DeviceAPI::CUDA)
            .gpu_threads(ri, DeviceAPI::CUDA);
    } break;
    case Backend::CUDAVectorize: {
        RVar ro, ri;
        RVar rio, rii;
        intermediate.update()
            .atomic(true)
            .split(r.x, ro, ri, 32)
            .split(ri, rio, rii, 4)
            .gpu_blocks(ro, DeviceAPI::CUDA)
            .gpu_threads(rio, DeviceAPI::CUDA)
            .vectorize(rii);
    } break;
    default: {
        _halide_user_assert(false) << "Unsupported backend.\n";
    } break;
    }

    Buffer<T> correct(hist_size);
    correct.fill(T(0));
    for (int i = 0; i < img_size; i++) {
        for (int j = 0; j < img_size; j++) {
            int idx = ((i + 1) * (j + 1)) % hist_size;
            correct(idx) = correct(idx) + T(1);
        }
    }

    // Run 10 times to make sure race condition do happen
    for (int iter = 0; iter < 10; iter++) {
        Buffer<T> out = hist.realize({hist_size});
        for (int i = 0; i < hist_size; i++) {
            check(__LINE__, out(i), correct(i));
        }
    }
}

template<typename T>
void test_hist_tuple_rfactor(const Backend &backend) {
    Type t = cast<T>(0).type();
    bool is_float_16 = t.is_float() && t.bits() == 16;
    if (is_float_16) {
        // rfactor doesn't support float16 yet.
        return;
    }

    int img_size = 100;
    int hist_size = 7;

    Func im, hist;
    Var x, y;
    RDom r(0, img_size, 0, img_size);
    im(x, y) = ((x + 1) * (y + 1)) % hist_size;
    hist(x) = Tuple(cast<T>(0), cast<T>(0));
    hist(im(r.x, r.y)) += Tuple(cast<T>(1), cast<T>(2));

    Func intermediate =
        hist.update()
            .rfactor({{r.y, y}});
    intermediate.compute_root();
    hist.compute_root();
    switch (backend) {
    case Backend::CPU: {
        intermediate.update().atomic().parallel(r.x);
    } break;
    case Backend::CPUVectorize: {
        RVar ro, ri;
        intermediate.update()
            .atomic()
            .split(r.x, ro, ri, 8)
            .parallel(ro)
            .vectorize(ri);
    } break;
    case Backend::OpenCL: {
        RVar ro, ri;
        intermediate.update()
            .atomic()
            .split(r.x, ro, ri, 8)
            .gpu_blocks(ro, DeviceAPI::OpenCL)
            .gpu_threads(ri, DeviceAPI::OpenCL);
    } break;
    case Backend::CUDA: {
        RVar ro, ri;
        intermediate.update()
            .atomic()
            .split(r.x, ro, ri, 8)
            .gpu_blocks(ro, DeviceAPI::CUDA)
            .gpu_threads(ri, DeviceAPI::CUDA);
    } break;
    case Backend::CUDAVectorize: {
        RVar ro, ri;
        RVar rio, rii;
        hist.update().atomic(true /*override_assciativity_test*/).split(r, ro, ri, 8).split(ri, rio, rii, 4).gpu_blocks(ro, DeviceAPI::CUDA).gpu_threads(rio, DeviceAPI::CUDA).vectorize(rii);
    } break;
    default: {
        _halide_user_assert(false) << "Unsupported backend.\n";
    } break;
    }

    Buffer<T> correct0(hist_size);
    Buffer<T> correct1(hist_size);
    correct0.fill(T(0));
    correct1.fill(T(0));
    for (int i = 0; i < img_size; i++) {
        for (int j = 0; j < img_size; j++) {
            int idx = ((i + 1) * (j + 1)) % hist_size;
            correct0(idx) = correct0(idx) + T(1);
            correct1(idx) = correct1(idx) + T(2);
        }
    }

    // Run 10 times to make sure race condition do happen
    for (int iter = 0; iter < 10; iter++) {
        Realization out = hist.realize({hist_size});
        Buffer<T> out0 = out[0];
        Buffer<T> out1 = out[1];
        for (int i = 0; i < hist_size; i++) {
            check(__LINE__, out0(i), correct0(i));
            check(__LINE__, out1(i), correct1(i));
        }
    }
}

template<typename T>
void test_all(const Backend &backend) {
    test_parallel_hist<T>(backend);
    test_parallel_cas_update<T>(backend);
    if (backend != Backend::CPUVectorize) {
        // Doesn't support vectorized predicated store yet.
        test_predicated_hist<T>(backend);
    }
    test_hist_compute_at<T>(backend);
    if (backend == Backend::CPU || backend == Backend::CPUVectorize) {
        test_hist_store_at<T>(backend);
    }
    test_hist_rfactor<T>(backend);
    if (backend == Backend::CPU) {
        // These require mutex locking which does not support vectorization and GPU
        test_parallel_hist_tuple<T>(backend);
        test_parallel_hist_tuple2<T>(backend);
        test_tuple_reduction<T>(backend);
        test_nested_atomics<T>(backend);
        test_hist_tuple_compute_at<T>(backend);
        test_hist_tuple_rfactor<T>(backend);
    }
}

extern "C" HALIDE_EXPORT_SYMBOL int extern_func(int x) {
    return x + 1;
}
HalideExtern_1(int, extern_func, int);

void test_extern_func(const Backend &backend) {
    int img_size = 10000;
    int hist_size = 7;

    Func im, hist;
    Var x;
    RDom r(0, img_size);

    im(x) = (x * x) % hist_size;

    hist(x) = 0;
    hist(im(r)) = extern_func(hist(im(r)));

    hist.compute_root();
    switch (backend) {
    case Backend::CPU: {
        hist.update().atomic(true /*override_associativity_test*/).parallel(r);
    } break;
    case Backend::CPUVectorize: {
        RVar ro, ri;
        hist.update()
            .atomic(true /*override_associativity_test*/)
            .split(r, ro, ri, 8)
            .parallel(ro)
            .vectorize(ri);
    } break;
    default: {
        _halide_user_assert(false) << "Unsupported backend.\n";
    } break;
    }

    Buffer<int> correct(hist_size);
    correct.fill(0);
    for (int i = 0; i < img_size; i++) {
        int idx = (i * i) % hist_size;
        correct(idx) = correct(idx) + 1;
    }

    // Run 10 times to make sure race condition do happen
    for (int iter = 0; iter < 10; iter++) {
        Buffer<int> out = hist.realize({hist_size});
        for (int i = 0; i < hist_size; i++) {
            check(__LINE__, out(i), correct(i));
        }
    }
}

extern "C" HALIDE_EXPORT_SYMBOL int expensive(int x) {
    float f = 3.0f;
    for (int i = 0; i < (1 << 10); i++) {
        f = sqrtf(sinf(cosf(f)));
    }
    if (f < 0) return 3;
    return x + 1;
}
HalideExtern_1(int, expensive, int);

void test_async(const Backend &backend) {
    int img_size = 10000;
    int hist_size = 7;

    Func producer, consumer;
    Var x;
    RDom r(0, img_size);

    producer(x) = (x * x) % hist_size;

    consumer(x) = 0;
    consumer(producer(r)) = extern_func(consumer(producer(r)));

    consumer.compute_root();
    switch (backend) {
    case Backend::CPU: {
        producer.compute_root().async();
        consumer.update().atomic(true /*override_associativity_test*/).parallel(r);
    } break;
    case Backend::CPUVectorize: {
        producer.compute_root().async();
        RVar ro, ri;
        consumer.update()
            .atomic(true /*override_associativity_test*/)
            .split(r, ro, ri, 8)
            .parallel(ro)
            .vectorize(ri);
    } break;
    default: {
        _halide_user_assert(false) << "Unsupported backend.\n";
    } break;
    }

    Buffer<int> correct(hist_size);
    correct.fill(0);
    for (int i = 0; i < img_size; i++) {
        int idx = (i * i) % hist_size;
        correct(idx) = correct(idx) + 1;
    }

    // Run 10 times to make sure race condition do happen
    for (int iter = 0; iter < 10; iter++) {
        Buffer<int> out = consumer.realize({hist_size});
        for (int i = 0; i < hist_size; i++) {
            check(__LINE__, out(i), correct(i));
        }
    }
}

void test_async_tuple(const Backend &backend) {
    int img_size = 10000;
    int hist_size = 7;

    Func producer0, producer1, consumer0, consumer1;
    Var x;
    RDom r(0, img_size);
    RDom rh(0, hist_size);

    producer0(x) = (x * x) % hist_size;
    producer1(x) = ((x + 1) * (x - 1)) % hist_size;

    consumer0(x) = Tuple(0, 0);
    consumer0(producer0(r)) += Tuple(1, 1);
    consumer0(producer1(r)) += Tuple(1, 1);

    consumer1(x) = Tuple(0, 0);
    consumer1(clamp(consumer0(rh)[0], 0, 2 * img_size)) += Tuple(1, 1);

    consumer0.compute_root().async();
    producer0.compute_root().async().parallel(x);
    producer1.compute_root().async().parallel(x);
    consumer1.compute_root();
    switch (backend) {
    case Backend::CPU: {
        consumer0.update(0)
            .atomic(true /*override_associativity_test*/)
            .parallel(r);
        consumer0.update(1)
            .atomic(true /*override_associativity_test*/)
            .parallel(r);
        consumer1.update()
            .atomic()
            .parallel(rh);
    } break;
    default: {
        _halide_user_assert(false) << "Unsupported backend.\n";
    } break;
    }

    Buffer<int> correct_consumer0(hist_size);
    Buffer<int> correct_consumer1(2 * img_size);
    correct_consumer0.fill(0);
    correct_consumer1.fill(0);
    for (int i = 0; i < img_size; i++) {
        int idx = (i * i) % hist_size;
        correct_consumer0(idx) = correct_consumer0(idx) + 1;
    }
    for (int i = 0; i < img_size; i++) {
        // Halide's modulo behaves differently compared to C's modulo.
        int idx = Halide::Internal::mod_imp(((i + 1) * (i - 1)), hist_size);
        correct_consumer0(idx) = correct_consumer0(idx) + 1;
    }
    for (int i = 0; i < hist_size; i++) {
        int idx = correct_consumer0(i);
        correct_consumer1(idx) = correct_consumer1(idx) + 1;
    }

    // Run 10 times to make sure race condition do not happen
    for (int iter = 0; iter < 10; iter++) {
        Realization out = consumer1.realize({2 * img_size});
        Buffer<int> out0 = out[0];
        Buffer<int> out1 = out[1];
        for (int i = 0; i < 2 * img_size; i++) {
            check(__LINE__, out0(i), correct_consumer1(i));
            check(__LINE__, out1(i), correct_consumer1(i));
        }
    }
}

int main(int argc, char **argv) {
    const Target t = get_jit_target_from_environment();
    if (t.arch == Target::WebAssembly) {
        printf("[SKIP] Skipping test for WebAssembly as it does not support atomics yet.\n");
        return 0;
    }

    if (t.os == Target::Windows && t.has_feature(Target::CUDA)) {
        printf("[SKIP] Skipping test for Windows + CUDA because of unexplained sporadic failures (https://github.com/halide/Halide/issues/7423).\n");
        return 0;
    }

    Target target = get_jit_target_from_environment();
// Most of the schedules used in this test are terrible for large
// thread count machines, due to massive amounts of
// contention. We'll just set the thread count to 4. Unfortunately
// there's no JIT api for this yet.
#ifdef _WIN32
    _putenv_s("HL_NUM_THREADS", "4");
#else
    setenv("HL_NUM_THREADS", "4", 1);
#endif
    test_all<uint8_t>(Backend::CPU);
    test_all<uint8_t>(Backend::CPUVectorize);
    test_all<int8_t>(Backend::CPU);
    test_all<int8_t>(Backend::CPUVectorize);
    test_all<uint16_t>(Backend::CPU);
    test_all<uint16_t>(Backend::CPUVectorize);
    test_all<int16_t>(Backend::CPU);
    test_all<int16_t>(Backend::CPUVectorize);
    if (target.has_feature(Target::F16C)) {
        test_all<float16_t>(Backend::CPU);
        test_all<float16_t>(Backend::CPUVectorize);
    }
    test_all<bfloat16_t>(Backend::CPU);
    test_all<bfloat16_t>(Backend::CPUVectorize);
    test_all<uint32_t>(Backend::CPU);
    test_all<uint32_t>(Backend::CPUVectorize);
    test_all<int32_t>(Backend::CPU);
    test_all<int32_t>(Backend::CPUVectorize);
    test_all<float>(Backend::CPU);
    test_all<float>(Backend::CPUVectorize);
    test_all<uint64_t>(Backend::CPU);
    test_all<uint64_t>(Backend::CPUVectorize);
    test_all<int64_t>(Backend::CPU);
    test_all<int64_t>(Backend::CPUVectorize);
    test_all<double>(Backend::CPU);
    test_all<double>(Backend::CPUVectorize);
    if (target.has_feature(Target::OpenCL)) {
        // No support for 8-bit & 16-bit atomics in OpenCL
        test_all<uint32_t>(Backend::OpenCL);
        test_all<int32_t>(Backend::OpenCL);
        test_all<float>(Backend::OpenCL);
        if (target.has_feature(Target::CLAtomics64)) {
            test_all<uint64_t>(Backend::OpenCL);
            test_all<int64_t>(Backend::OpenCL);
            test_all<double>(Backend::OpenCL);
        }
    }
    if (target.has_feature(Target::CUDA)) {
        // No support for 8-bit & 16-bit atomics in CUDA
        // float16 is possible but not implemented yet.
        test_all<uint32_t>(Backend::CUDA);
        test_all<int32_t>(Backend::CUDA);
        test_all<float>(Backend::CUDA);
        test_all<uint64_t>(Backend::CUDA);
        test_all<int64_t>(Backend::CUDA);
        test_all<double>(Backend::CUDA);

        test_all<uint32_t>(Backend::CUDAVectorize);
        test_all<int32_t>(Backend::CUDAVectorize);
        test_all<float>(Backend::CUDAVectorize);
        test_all<uint64_t>(Backend::CUDAVectorize);
        test_all<int64_t>(Backend::CUDAVectorize);
        test_all<double>(Backend::CUDAVectorize);
    }
    test_extern_func(Backend::CPU);
    test_extern_func(Backend::CPUVectorize);
    test_async(Backend::CPU);
    test_async(Backend::CPUVectorize);
    test_async_tuple(Backend::CPU);

    printf("Success!\n");
    return 0;
}
