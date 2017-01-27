#ifndef _TESTING_H_
#define _TESTING_H_

#include "Halide.h"
#include <iostream>
#include <exception>
#include <functional>
#include <cmath>

namespace Testing {

    // fill 3-dimension buffer
    template <typename T>
        void fill(Halide::Buffer<T> &buf, std::function<T(int x, int y, int c)> f)
        {
            buf.for_each_element([&](int x, int y, int c) { 
                    buf(x, y, c) = f(x, y, c);
                    });
        }

    template <typename T>
        bool neq(T a, T b, T tol)
        {
            return std::abs(a-b) > tol;
        }

    // check 3-dimension buffer
    template<typename T>
        bool check_result(const Halide::Buffer<T> &buf, std::function<T(int x, int y, int c)> f, T tol=0)
        {
            class err : std::exception {
                public: static void vector(const std::vector<T> &v) {
                    for (size_t i=0; i<v.size(); i++) {
                        if (i > 0) std::cerr << ",";
                        std::cerr << v[i]+0; // need to add 0 to get output -- compiler bug??
                    }
                }
            };
            try {
                buf.for_each_element([&](int x, int y) {
                    std::vector<T> expected;
                    std::vector<T> result;
                    for (int c=0; c < buf.channels(); c++) {
                        expected.push_back(f(x, y, c));
                        result.push_back(buf(x,y,c));
                    }
                    for (int c=0; c< buf.channels(); c++) {
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
            }
            catch (err) {
                return false;
            }
            return true;
        }

    // check 2-dimension buffer
    template<typename T>
        bool check_result(const Halide::Buffer<T> &buf, std::function<T(int x, int y)> f, T tol=0)
        {
            class err : std::exception {};
            try {
                buf.for_each_element([&](int x, int y) {
                    const T expected = f(x, y);
                    const T result = buf(x, y);
                    if (neq(result, expected, tol)) {
                        std::cerr << "Error: result (";
                        std::cerr << result+0;
                        std::cerr << ") should be (";
                        std::cerr << expected+0;
                        std::cerr << ") at x=" << x << " y=" << y << std::endl;
                        throw err();
                    }
                });
            }
            catch (err) {
                return false;
            }
            return true;
        }

}

#endif // _TESTING_H_

