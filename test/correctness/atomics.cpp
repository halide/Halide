#include <type_traits>

#include "Halide.h"

using namespace Halide;

template<typename T,
         typename std::enable_if<std::is_floating_point<T>::value>::type* = nullptr>
inline void check(int line_number, T x, T target, T threshold = T(1e-6)) {
    _halide_user_assert(std::fabs((x) - (target)) < threshold)
        << "Line " << line_number << ": Expected " << (target) << " instead of " << (x) << "\n";
}

inline void check(int line_number, float16_t x, float16_t target) {
    return check(line_number, (double)x, (double)target, 5e-3);
}

template<typename T,
         typename std::enable_if<std::is_integral<T>::value, int>::type* = nullptr>
inline void check(int line_number, T x, T target) {
    _halide_user_assert(x == target)
        << "Line " << line_number << ": Expected " << (target) << " instead of " << (x) << "\n";
}

template <typename T>
void test_parallel_hist(const Target &target) {
    int img_size = 10000;
    int hist_size = 7;

    Func im, hist;
    Var x;
    RDom r(0, img_size);

    im(x) = (x*x) % hist_size;

    hist(x) = cast<T>(0);
    hist(im(r)) += cast<T>(1);

    hist.compute_root();
    if (!target.has_gpu_feature()) {
        hist.update().atomic().parallel(r);
    } else {
        RVar ro, ri;
        hist.update().atomic().split(r, ro, ri, 32).gpu_blocks(ro).gpu_threads(ri);
    }

    Buffer<T> correct(hist_size);
    correct.fill(T(0));
    for (int i = 0; i < img_size; i++) {
        int idx = (i*i) % hist_size;
        correct(idx) = correct(idx) + T(1);
    }

    // Run 100 times to make sure race condition do happen
    for (int iter = 0; iter < 100; iter++) {
        Buffer<T> out = hist.realize(hist_size, target);
        for (int i = 0; i < hist_size; i++) {
            check(__LINE__, out(i), correct(i));
        }
    }
}

template <typename T>
void test_parallel_cas_update(const Target &target) {
    int img_size = 1000;
    int hist_size = 13;

    Func im, hist;
    Var x;
    RDom r(0, img_size);

    im(x) = (x*x) % hist_size;

    hist(x) = cast<T>(0);
    // Can't do this with atomic rmw, need to generate a CAS loop
    hist(im(r)) = max(hist(im(r)) + cast<T>(1), cast<T>(100));

    hist.compute_root();
    if (!target.has_gpu_feature()) {
        hist.update().atomic().parallel(r);
    } else {
        RVar ro, ri;
        hist.update().atomic().split(r, ro, ri, 32).gpu_blocks(ro).gpu_threads(ri);
    }

    Buffer<T> correct(hist_size);
    correct.fill(T(0));
    for (int i = 0; i < img_size; i++) {
        int idx = (i*i) % hist_size;
        T x = correct(idx) + T(1);
        correct(idx) = x > T(100) ? x : T(100);
    }

    // Run 1000 times to make sure race condition do happen
    for (int iter = 0; iter < 1000; iter++) {
        Buffer<T> out = hist.realize(hist_size, target);
        for (int i = 0; i < hist_size; i++) {
            check(__LINE__, out(i), correct(i));
        }
    }
}

template <typename T>
void test_all(const Target &target) {
    test_parallel_hist<T>(target);
    // test_parallel_cas_update<T>(target);
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_feature(Target::OpenCL)) {
        // Skip 8-bit & 16-bit tests for OpenCL
        test_all<uint8_t>(target);
        test_all<int8_t>(target);
        test_all<uint16_t>(target);
        test_all<int16_t>(target);
        test_all<float16_t>(target);
    }
    test_all<uint32_t>(target);
    test_all<int32_t>(target);
    test_all<float>(target);
    if (target.has_feature(Target::CLAtomics64)) {
        test_all<uint64_t>(target);
        test_all<int64_t>(target);
        test_all<double>(target);
    }
    return 0;
}
