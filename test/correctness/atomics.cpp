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
void test_parallel_hist() {
    int img_size = 10000;
    int hist_size = 7;

    Func im, hist;
    Var x;
    RDom r(0, img_size);

    im(x) = (x*x) % hist_size;

    hist(x) = cast<T>(0);
    hist(im(r)) += cast<T>(1);

    hist.compute_root();
    hist.update().atomic().parallel(r);

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

int main(int argc, char **argv) {
    test_parallel_hist<uint8_t>();
    test_parallel_hist<uint16_t>();
    test_parallel_hist<uint32_t>();
    test_parallel_hist<int8_t>();
    test_parallel_hist<int16_t>();
    test_parallel_hist<int32_t>();
    test_parallel_hist<float16_t>();
    test_parallel_hist<float>();
    test_parallel_hist<double>();
    return 0;
}
