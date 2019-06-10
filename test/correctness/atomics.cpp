#include <type_traits>

#include "Halide.h"

using namespace Halide;

enum class Backend {
    CPU,
    OpenCL,
    CUDA
};

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
void test_parallel_hist(const Backend &backend) {
    int img_size = 10000;
    int hist_size = 7;

    Func im, hist;
    Var x;
    RDom r(0, img_size);

    im(x) = (x*x) % hist_size;

    hist(x) = cast<T>(0);
    hist(im(r)) += cast<T>(1);

    hist.compute_root();
    switch(backend) {
        case Backend::CPU: {
            hist.update().atomic().parallel(r);
        } break;
        case Backend::OpenCL: {
            RVar ro, ri;
            hist.update().atomic().split(r, ro, ri, 32)
                .gpu_blocks(ro, DeviceAPI::OpenCL)
                .gpu_threads(ri, DeviceAPI::OpenCL);
        } break;
        case Backend::CUDA: {
            RVar ro, ri;
            hist.update().atomic().split(r, ro, ri, 32)
                .gpu_blocks(ro, DeviceAPI::CUDA)
                .gpu_threads(ri, DeviceAPI::CUDA);
        } break;
    }

    Buffer<T> correct(hist_size);
    correct.fill(T(0));
    for (int i = 0; i < img_size; i++) {
        int idx = (i*i) % hist_size;
        correct(idx) = correct(idx) + T(1);
    }

    // Run 100 times to make sure race condition do happen
    for (int iter = 0; iter < 100; iter++) {
        Buffer<T> out = hist.realize(hist_size);
        for (int i = 0; i < hist_size; i++) {
            check(__LINE__, out(i), correct(i));
        }
    }
}

template <typename T>
void test_parallel_cas_update(const Backend &backend) {
    int img_size = 1000;
    int hist_size = 13;

    Func im, hist;
    Var x;
    RDom r(0, img_size);

    im(x) = (x*x) % hist_size;

    hist(x) = cast<T>(0);
    // Can't do this with atomic rmw, need to generate a CAS loop
    hist(im(r)) = min(hist(im(r)) + cast<T>(1), cast<T>(100));

    hist.compute_root();
    switch(backend) {
        case Backend::CPU: {
            hist.update().atomic().parallel(r);
        } break;
        case Backend::OpenCL: {
            RVar ro, ri;
            hist.update().atomic().split(r, ro, ri, 32)
                .gpu_blocks(ro, DeviceAPI::OpenCL)
                .gpu_threads(ri, DeviceAPI::OpenCL);
        } break;
        case Backend::CUDA: {
            RVar ro, ri;
            hist.update().atomic().split(r, ro, ri, 32)
                .gpu_blocks(ro, DeviceAPI::CUDA)
                .gpu_threads(ri, DeviceAPI::CUDA);
        } break;
    }

    Buffer<T> correct(hist_size);
    correct.fill(T(0));
    for (int i = 0; i < img_size; i++) {
        int idx = (i*i) % hist_size;
        T x = correct(idx) + T(1);
        correct(idx) = x < T(100) ? x : T(100);
    }

    // Run 1000 times to make sure race condition do happen
    for (int iter = 0; iter < 1000; iter++) {
        Buffer<T> out = hist.realize(hist_size);
        for (int i = 0; i < hist_size; i++) {
            check(__LINE__, out(i), correct(i));
        }
    }
}

template <typename T>
void test_all(const Backend &backend) {
    test_parallel_hist<T>(backend);
    test_parallel_cas_update<T>(backend);
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    test_all<uint8_t>(Backend::CPU);
    test_all<int8_t>(Backend::CPU);
    test_all<uint16_t>(Backend::CPU);
    test_all<int16_t>(Backend::CPU);
    if (target.has_feature(Target::F16C)) {
        test_all<float16_t>(Backend::CPU);
    }
    test_all<uint32_t>(Backend::CPU);
    test_all<int32_t>(Backend::CPU);
    test_all<float>(Backend::CPU);
    test_all<uint64_t>(Backend::CPU);
    test_all<int64_t>(Backend::CPU);
    test_all<double>(Backend::CPU);
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
        test_all<uint32_t>(Backend::CUDA);
        test_all<int32_t>(Backend::CUDA);
        test_all<float>(Backend::CUDA);
        test_all<uint64_t>(Backend::CUDA);
        test_all<int64_t>(Backend::CUDA);
        test_all<double>(Backend::CUDA);
    }
    return 0;
}
