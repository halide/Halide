#ifndef UTIL_H
#define UTIL_H

#include <vector>

/* This file contains various utility functions for Halide. */

namespace HalideInternal {

    /* Build small vectors of up to 6 elements. If we used C++11 and
     * had vector initializers, this would not be necessary, but we
     * don't want to rely on C++11 support. */
    template<typename T>
    std::vector<T> vec(T a) {
        std::vector<T> v(1);
        v[0] = a;
        return v;
    }

    template<typename T>
    std::vector<T> vec(T a, T b) {
        std::vector<T> v(2);
        v[0] = a;
        v[1] = b;
        return v;
    }

    template<typename T>
    std::vector<T> vec(T a, T b, T c) {
        std::vector<T> v(3);
        v[0] = a;
        v[1] = b;
        v[2] = c;
        return v;
    }

    template<typename T>
    std::vector<T> vec(T a, T b, T c, T d) {
        std::vector<T> v(4);
        v[0] = a;        
        v[1] = b;
        v[2] = c;
        v[3] = d;
        return v;
    }

    template<typename T>
    std::vector<T> vec(T a, T b, T c, T d, T e) {
        std::vector<T> v(5);
        v[0] = a;        
        v[1] = b;
        v[2] = c;
        v[3] = d;
        v[4] = e;
        return v;
    }

    template<typename T>
    std::vector<T> vec(T a, T b, T c, T d, T e, T f) {
        std::vector<T> v(6);
        v[0] = a;        
        v[1] = b;
        v[2] = c;
        v[3] = d;
        v[4] = e;
        v[5] = f;
        return v;
    }
}

#endif
