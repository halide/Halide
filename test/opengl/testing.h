#ifndef _TESTING_H_
#define _TESTING_H_

#include "Halide.h"
#include <iostream>
#include <functional>
#include <cmath>

namespace Testing {

    // fill 3-dimension buffer
    template <typename T>
        void fill(Halide::Buffer<T> &buf, std::function<T(int x, int y, int c)> f)
        {
            for (int y = buf.top(); y <= buf.bottom(); y++) {
                for (int x = buf.left(); x <= buf.right(); x++) {
                    for (int c = 0; c < buf.channels(); c++) {
                        buf(x, y, c) = f(x, y, c);
                    }
                }
            }
        }


    // check 3-dimension buffer
    template<typename T>
        bool check_result(const Halide::Buffer<T> &buf, std::function<T(int x, int y, int c)> f, T tol=0)
        {
            auto neq = [&](T a, T b) { return std::abs(a-b) > tol; };

            for (int y = buf.top(); y <= buf.bottom(); ++y) {
                for (int x = buf.left(); x <= buf.right(); ++x) {
                    if (buf.channels() == 3) {
                        const auto expected0 = f(x, y, 0);
                        const auto expected1 = f(x, y, 1);
                        const auto expected2 = f(x, y, 2);
                        const auto result0 = buf(x, y, 0);
                        const auto result1 = buf(x, y, 1);
                        const auto result2 = buf(x, y, 2);
                        if (neq(result0, expected0) || neq(result1, expected1) || neq(result2,  expected2)) {
                            // need to include +0 to overcome compiler bug(?) that doesn't
                            // properly call the ostream operator
                            auto errpixel = [](T a, T b, T c) { std::cerr << "(" << a+0 << "," << b+0 << "," << c+0 << ")"; };

                            std::cerr << "Error: result pixel ";
                            errpixel(result0, result1, result2);
                            std::cerr << " should be ";
                            errpixel(expected0, expected1, expected2);
                            std::cerr << " at x=" << x << " y=" << y;
                            if (tol != 0) std::cerr << " [tolerance=" << tol << "]";
                            std::cout << std::endl;
                            return false;
                        }
                    } else {
                        for (int c = 0; c != buf.extent(2); ++c) {
                            const auto expected = f(x, y, c);
                            const auto result = buf(x, y, c);
                            if (neq(result, expected)) {
                                std::cerr << "Error: result value " << result;
                                std::cerr << " should be " << expected;
                                std::cerr << " at x=" << x << " y=" << y << " c=" << c;
                                if (tol != 0) std::cerr << " [tolerance=" << tol << "]";
                                std::cout << std::endl;
                                return false;
                            }
                        }
                    }
                }
            }
            return true;
        }

    // check 2-dimension buffer
    template<typename T>
        bool check_result(const Halide::Buffer<T> &buf, std::function<T(int x, int y)> f, T tol=0)
        {
            auto neq = [&](T a, T b) { return std::abs(a-b) > tol; };

            for (int y = buf.top(); y <= buf.bottom(); ++y) {
                for (int x = buf.left(); x <= buf.right(); ++x) {
                    const auto expected = f(x, y);
                    const auto result = buf(x, y);
                    if (neq(result, expected)) {
                        std::cerr << "Error: result value " << result;
                        std::cerr << " should be " << expected;
                        std::cerr << " at x=" << x << " y=" << y;
                        if (tol != 0) std::cerr << " [tolerance=" << tol << "]";
                        std::cout << std::endl;
                        return false;
                    }
                }
            }
            return true;
        }

}

#endif // _TESTING_H_

