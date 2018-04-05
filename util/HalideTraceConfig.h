#ifndef HALIDE_TRACE_CONFIG_H
#define HALIDE_TRACE_CONFIG_H

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "HalideRuntime.h"

#ifdef _MSC_VER
#define HALIDE_NEVER_INLINE __declspec(noinline)
#else
#define HALIDE_NEVER_INLINE __attribute__((noinline))
#endif

namespace Halide {
namespace Trace {

template<typename T>
std::ostream &operator<<(std::ostream &stream, const std::vector<T> &v) {
    stream << "[ ";
    bool need_comma = false;
    for (const T &t : v) {
        if (need_comma) {
            stream << ", ";
        }
        stream << t;
        need_comma = true;
    }
    stream << " ]";
    return stream;
}

HALIDE_NEVER_INLINE int parse_int(const std::string &str,
                                  std::function<void(const std::string &)> error = nullptr) {
    std::istringstream iss(str);
    int i;
    iss >> i;
    if (iss.fail() || iss.get() != EOF) {
        if (error) {
            error("Unable to parse '" + str + "' as an int");
        }
    }
    return i;
}

HALIDE_NEVER_INLINE float parse_float(const std::string &str,
                                      std::function<void(const std::string &)> error = nullptr) {
    std::istringstream iss(str);
    float f;
    iss >> f;
    if (iss.fail() || iss.get() != EOF) {
        error("Unable to parse '" + str + "' as a float");
    }
    return f;
}

// A struct specifying a text label that will appear on the screen at some point.
struct Label {
    std::string text;
    int x, y, n;

    friend std::ostream &operator<<(std::ostream &stream, const Label &label) {
        stream << "text=\"" << label.text << " @ " << label.x << " " << label.y << " n=" << label.n;
        return stream;
    }
};

struct Point {
    int x, y;

    friend std::ostream &operator<<(std::ostream &stream, const Point &pt) {
        stream << "(" << pt.x << "," << pt.y << ")";
        return stream;
    }
};

// Configuration for how a func should be rendered in HalideTraceViz
struct Config {
    std::string name;
    float zoom = 1.f;
    int load_cost = 0;
    int store_cost = 1;
    int dims = 2;
    int x, y = 0;
    std::vector<Point> strides = { {1, 0}, {0, 1} };
    int color_dim = -1;
    float min = 0.f, max = 1.f;
    std::vector<Label> labels;
    bool blank_on_end_realization = false;
    uint32_t uninitialized_memory_color = 0xff000000;

    friend std::ostream &operator<<(std::ostream &stream, const Config &config) {
        stream <<
                "Func " << config.name << ":\n" <<
                " min: " << config.min << " max: " << config.max << "\n" <<
                " color_dim: " << config.color_dim << "\n" <<
                " blank: " << config.blank_on_end_realization << "\n" <<
                " dims: " << config.dims << "\n" <<
                " zoom: " << config.zoom << "\n" <<
                " load cost: " << config.load_cost << "\n" <<
                " store cost: " << config.store_cost << "\n" <<
                " x: " << config.x << " y: " << config.y << "\n" <<
                " strides: " << config.strides << "\n" <<
                " labels: " << config.labels << "\n";
        return stream;
    }

    // NO_INLINE
};


}  // namespace Trace
}  // namespace Halide

#endif  // HALIDE_TRACE_CONFIG_H
