#ifndef HALIDE_TRACE_CONFIG_H
#define HALIDE_TRACE_CONFIG_H

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "HalideRuntime.h"

namespace Halide {
namespace Trace {

using ErrorFunc = std::function<void(const std::string &)>;

inline void default_error(const std::string &err) {
    std::cerr << "Error: " << err << "\n";
    exit(1);
}

inline std::string replace_all(const std::string &str, const std::string &find, const std::string &replace) {
    size_t pos = 0;
    std::string result = str;
    while ((pos = result.find(find, pos)) != std::string::npos) {
        result.replace(pos, find.length(), replace);
        pos += replace.length();
    }
    return result;
}

inline std::string escape_spaces(const std::string &str) {
    // Note: if the source string already contains '\x20', we'll
    // end up unescaping that back into a space. That's acceptable.
    return replace_all(str, " ", "\\x20");
}

inline std::string unescape_spaces(const std::string &str) {
    // Note: if the source string already contains '\x20', we'll
    // end up unescaping that back into a space. That's acceptable.
    return replace_all(str, "\\x20", " ");
}

template<typename T>
std::ostream &operator<<(std::ostream &os, const std::vector<T> &v) {
    os << v.size() << " ";
    for (const T &t : v) {
        os << t << " ";
    }
    return os;
}

template<typename T>
std::istream &operator>>(std::istream &is, std::vector<T> &v) {
    v.clear();
    size_t size;
    is >> size;
    for (size_t i = 0; i < size; ++i) {
        T tmp;
        is >> tmp;
        v.push_back(tmp);
    }
    return is;
}

struct Point {
    int x = 0, y = 0;

    Point() = default;
    Point(int x, int y) : x(x), y(y) {}

    friend std::ostream &operator<<(std::ostream &os, const Point &pt) {
        os << pt.x << " " << pt.y;
        return os;
    }

    friend std::istream &operator>>(std::istream &is, Point &pt) {
        is >> pt.x >> pt.y;
        return is;
    }
};

// A struct specifying a text label that will appear on the screen at some point.
struct Label {
    std::string text;
    Point pos;
    int fade_in_frames = 0;

    Label() = default;
    Label(const std::string &text, const Point &pos, int fade_in_frames = 0) : text(text), pos(pos), fade_in_frames(fade_in_frames) {}

    friend std::ostream &operator<<(std::ostream &os, const Label &label) {
        os << escape_spaces(label.text) << " " << label.pos << " " << label.fade_in_frames;
        return os;
    }

    friend std::istream &operator>>(std::istream &is, Label &label) {
        is >> label.text >> label.pos >> label.fade_in_frames;
        label.text = unescape_spaces(label.text);
        return is;
    }
};

// Configuration for how a func should be rendered in HalideTraceViz
struct FuncConfig {
    float zoom = 1.f;
    int load_cost = 0;
    int store_cost = 1;
    Point pos;
    std::vector<Point> strides = { {1, 0}, {0, 1} };
    int color_dim = -1;
    float min = 0.f, max = 1.f;
    std::vector<Label> labels;
    bool auto_label = true;  // if there are no labels, add one matching the func name
    bool blank_on_end_realization = false;
    uint32_t uninitialized_memory_color = 0xff000000;

    void dump(std::ostream &os, const std::string &name) const {
        os << std::boolalpha
            << "Func: " << name << "\n"
            << "  zoom: " << zoom << "\n"
            << "  load cost: " << load_cost << "\n"
            << "  store cost: " << store_cost << "\n"
            << "  pos: " << pos << "\n"
            << "  strides: " << strides << "\n"
            << "  color_dim: " << color_dim << "\n"
            << "  min: " << min << " max: " << max << "\n"
            << "  labels: " << labels << "\n"
            << "  auto_label: " << auto_label << "\n"
            << "  blank: " << blank_on_end_realization << "\n"
            << "  uninit: " << uninitialized_memory_color << "\n";
    }

    friend std::ostream &operator<<(std::ostream &os, const FuncConfig &config) {
        // The 'format' here is intentionally simple:
        // a space-separated iostream text string,
        // in an assumed order rather than freeform.
        os
            << config.zoom << " "
            << config.load_cost << " "
            << config.store_cost << " "
            << config.pos << " "
            << config.strides << " "
            << config.color_dim << " "
            << config.min << " "
            << config.max << " "
            << config.labels << " "
            << config.auto_label << " "
            << config.blank_on_end_realization << " "
            << config.uninitialized_memory_color;
        return os;
    }

    friend std::istream &operator>>(std::istream &is, FuncConfig &config) {
        is
            >> config.zoom
            >> config.load_cost
            >> config.store_cost
            >> config.pos
            >> config.strides
            >> config.color_dim
            >> config.min
            >> config.max
            >> config.labels
            >> config.auto_label
            >> config.blank_on_end_realization
            >> config.uninitialized_memory_color;
        return is;
    }

    std::string to_trace_tag() const {
        // The 'format' here is intentionally simple:
        // a space-separated iostream text string,
        // in an assumed order rather than freeform.
        std::ostringstream os;
        os << *this;
        return os.str();
    }

    FuncConfig() = default;

    explicit FuncConfig(const std::string &trace_tag, ErrorFunc error = default_error) {
        std::istringstream is(trace_tag);
        is >> *this;
        if (is.fail() || is.get() != EOF) {
            error("trace_tag parsing error");
        }
    }
};


}  // namespace Trace
}  // namespace Halide

#endif  // HALIDE_TRACE_CONFIG_H
