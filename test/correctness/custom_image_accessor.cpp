#include "Halide.h"
#include <cmath>

using namespace Halide;

// First a very simple example. We'll make it possible to access an
// image with a custom 3D coordinate type.
struct Coord {
    int x, y, z;
};


namespace Halide {

template<typename T, int D>
T image_accessor(const Image<T, D> &im, Coord c) {
    return im(c.x, c.y, c.z);
}

template<typename T, int D>
T &image_accessor(Image<T, D> &im, Coord c) {
    return im(c.x, c.y, c.z);
}

}

// Next we'll use a more complex variadic example. We'll extend
// Halide::Tools::Image<float, D> so that using operator() with floats
// does multi-linear interpolation into it.

// First we define a fancy multi-linear interpolator that uses
// template recursion to touch 2^D samples.
template<int D, int C, typename ...Args>
struct MultiLinearSampler {
    float operator()(const Image<float, D> &im, float *float_args, Args... int_args) {
        float f = *float_args;
        int i = (int)(std::floor(f));
        f -= i;
        MultiLinearSampler<D, C - 1, int, Args...> s;
        float a = s(im, float_args + 1, int_args..., i);
        float b = s(im, float_args + 1, int_args..., i + 1);
        return a + f * (b - a);
    }
};

template<int D, typename ...Args>
struct MultiLinearSampler<D, 0, Args...> {
    float operator()(const Image<float, D> &im, float *float_args, Args... int_args) {
        return im(int_args...);
    }
};

// Then we need a helper to test if a parameter pack is entirely float-convertible
template<typename ...Args>
struct AllFloatConvertible {
    static const bool value = false;
};

template<>
struct AllFloatConvertible<> {
    static const bool value = true;
};

template<typename T, typename ...Args>
struct AllFloatConvertible<T, Args...> {
    static const bool value =
        std::is_convertible<T, float>::value &&
        AllFloatConvertible<Args...>::value;
};

namespace Halide {

// Now define an accessor (for float images only) that does multilinear sampling. 
template<int D, typename ...Args>
typename std::enable_if<AllFloatConvertible<Args...>::value, float>::type
image_accessor(const Image<float, D> &im, Args... args) {
    float coords[] = {float(args)...};
    return MultiLinearSampler<D, sizeof...(args)>()(im, coords);
}

}

int main(int argc, char **argv) {
    Image<float> im(10, 10, 10);

    im(3, 2, 5) = 0.0f;

    im(Coord{3, 2, 5}) = 45.0f;

    if (im(3, 2, 5) != 45.0f) {
        printf("Assigning using Coord didn't work\n");
        return -1;
    }
    if (im(Coord{3, 2, 5}) != 45.0f) {
        printf("Loading using Coord didn't work\n");
    }

    im.for_each_element([&](int x, int y, int c) {
            im(x, y, c) = x*100 + y*10 + c;
        });

    float correct = 1.25f * 100 + 7 * 10 + 1.15f;
    float actual = im(1.25f, 7, 1.15);
    if (fabs(correct - actual) > 0.001f) {
        printf("Got %f instead of %f\n", actual, correct);
        return -1;
    }

    return 0;
}
