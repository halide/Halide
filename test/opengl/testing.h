#ifndef _TESTING_H_
#define _TESTING_H_

#include "Halide.h"
#include <cmath>
#include <exception>
#include <functional>
#include <iostream>

namespace Testing {

template <typename T>
bool neq(T a, T b, T tol) {
    return std::abs(a - b) > tol;
}

// Check 3-dimension buffer
template <typename T>
bool check_result(const Halide::Buffer<T> &buf, T tol, std::function<T(int x, int y, int c)> f) {
    class err : std::exception {
    public:
        static void vector(const std::vector<T> &v) {
            for (size_t i = 0; i < v.size(); i++) {
                if (i > 0) {
                    std::cerr << ",";
                }
                std::cerr << +v[i];  // use unary + to promote uint8_t from char to numeric
            }
        }
    };
    try {
        buf.for_each_element([&](int x, int y) {
            std::vector<T> expected;
            std::vector<T> result;
            for (int c = 0; c < buf.channels(); c++) {
                expected.push_back(f(x, y, c));
                result.push_back(buf(x, y, c));
            }
            for (int c = 0; c < buf.channels(); c++) {
                if (neq(result[c], expected[c], tol)) {
                    std::cerr << "Error: result (";
                    err::vector(result);
                    std::cerr << ") should be (";
                    err::vector(expected);
                    std::cerr << ") at x=" << x << " y=" << y << std::endl;
                    throw err();
                }
            }
        });
    } catch (err) {
        return false;
    }
    return true;
}

// Check 2-dimension buffer
template <typename T>
bool check_result(const Halide::Buffer<T> &buf, T tol, std::function<T(int x, int y)> f) {
    class err : std::exception {};
    try {
        buf.for_each_element([&](int x, int y) {
            const T expected = f(x, y);
            const T result = buf(x, y);
            if (neq(result, expected, tol)) {
                std::cerr << "Error: result (";
                std::cerr << +result;
                std::cerr << ") should be (";
                std::cerr << +expected;
                std::cerr << ") at x=" << x << " y=" << y << std::endl;
                throw err();
            }
        });
    } catch (err) {
        return false;
    }
    return true;
}

// Shorthand to check with tolerance=0
template <typename T, typename Func>
bool check_result(const Halide::Buffer<T> &buf, Func f) {
    return check_result<T>(buf, 0, f);
}
}

#endif  // _TESTING_H_
