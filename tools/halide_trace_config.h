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
    Label(const std::string &text, const Point &pos = {0, 0}, int fade_in_frames = 0) : text(text), pos(pos), fade_in_frames(fade_in_frames) {}

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
    // Label(s) to be rendered with the Func. The Label's position
    // is an offset from the Func's position, so (0, 0) means render
    // at the top-left of the Func itself.
    std::vector<Label> labels;
    bool blank_on_end_realization = false;
    uint32_t uninitialized_memory_color = 0xff000000;

    static std::string tag_start_text() {
        return std::string("htv_func_config:");
    }

    static bool match(const std::string &trace_tag) {
        return trace_tag.find(tag_start_text()) == 0;
    }

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
            << "  blank: " << blank_on_end_realization << "\n"
            << "  uninit: " << uninitialized_memory_color << "\n";
    }

    friend std::ostream &operator<<(std::ostream &os, const FuncConfig &config) {
        // The 'format' here is intentionally simple:
        // a space-separated iostream text string,
        // in an assumed order rather than freeform.
        os
            << tag_start_text() << " "
            << config.zoom << " "
            << config.load_cost << " "
            << config.store_cost << " "
            << config.pos << " "
            << config.strides << " "
            << config.color_dim << " "
            << config.min << " "
            << config.max << " "
            << config.labels << " "
            << config.blank_on_end_realization << " "
            << config.uninitialized_memory_color;
        return os;
    }

    friend std::istream &operator>>(std::istream &is, FuncConfig &config) {
        std::string start_text;
        is
            >> start_text
            >> config.zoom
            >> config.load_cost
            >> config.store_cost
            >> config.pos
            >> config.strides
            >> config.color_dim
            >> config.min
            >> config.max
            >> config.labels
            >> config.blank_on_end_realization
            >> config.uninitialized_memory_color;
        if (start_text != tag_start_text()) {
            is.setstate(std::ios::failbit);
        }
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
            error("FuncConfig trace_tag parsing error");
        }
    }
};

// Configuration for top-level visualization config settings.
// If more than one of these is encountered, the last one wins.
struct GlobalConfig {
    // The size of the output frames.
    Point frame_size = { 1920, 1080 };

    // How quickly should the yellow and blue highlights decay
    // over time. This is a two-stage exponential decay with a knee in
    // it. decay_factor_during_compute controls the rate at which they
    // decay while a value is in the process of being computed,
    // and decay_factor_after_compute controls the rate at which
    // they decay over time after the corresponding value has finished
    // being computed. 1 means never decay, 2 means halve in opacity
    // every frame, and 256 or larger means instant decay. The default
    // values produce a highlight that holds while the value is being computed,
    // and then decays slowly.
    int decay_factor_during_compute = 1;
    int decay_factor_after_compute = 2;

    // How many frames to output after the end of the trace.
    int hold_frames = 250;

    // How many Halide computations should be covered by each frame.
    int timestep = 10000;

    static std::string tag_start_text() {
        return std::string("htv_global_config:");
    }

    static bool match(const std::string &trace_tag) {
        return trace_tag.find(tag_start_text()) == 0;
    }

    void dump(std::ostream &os) const {
        os << "Global:\n"
            << "  frame_size: " << frame_size << "\n"
            << "  decay_factor_during_compute: " << decay_factor_during_compute << "\n"
            << "  decay_factor_after_compute: " << decay_factor_after_compute << "\n"
            << "  hold_frames: " << hold_frames << "\n"
            << "  timestep: " << timestep << "\n";
    }

    friend std::ostream &operator<<(std::ostream &os, const GlobalConfig &config) {
        // The 'format' here is intentionally simple:
        // a space-separated iostream text string,
        // in an assumed order rather than freeform.
        os
            << tag_start_text() << " "
            << config.frame_size << " "
            << config.decay_factor_during_compute << " "
            << config.decay_factor_after_compute << " "
            << config.hold_frames << " "
            << config.timestep;
        return os;
    }

    friend std::istream &operator>>(std::istream &is, GlobalConfig &config) {
        std::string start_text;
        is
            >> start_text
            >> config.frame_size
            >> config.decay_factor_during_compute
            >> config.decay_factor_after_compute
            >> config.hold_frames
            >> config.timestep;
        if (start_text != tag_start_text()) {
            is.setstate(std::ios::failbit);
        }
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

    GlobalConfig() = default;

    explicit GlobalConfig(const std::string &trace_tag, ErrorFunc error = default_error) {
        std::istringstream is(trace_tag);
        is >> *this;
        if (is.fail() || is.get() != EOF) {
            error("GlobalConfig trace_tag parsing error");
        }
    }
};


}  // namespace Trace
}  // namespace Halide

#endif  // HALIDE_TRACE_CONFIG_H
