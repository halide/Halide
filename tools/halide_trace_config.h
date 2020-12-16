#ifndef HALIDE_TRACE_CONFIG_H
#define HALIDE_TRACE_CONFIG_H

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
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

inline std::ostream &operator<<(std::ostream &os, const halide_type_t &t) {
    os << (int)t.code << " " << (int)t.bits << " " << t.lanes;
    return os;
}

inline std::istream &operator>>(std::istream &is, halide_type_t &t) {
    // type.code is an enum; type.bits is a uint8 and might be read as char.
    int type_code, type_bits;
    is >> type_code >> type_bits >> t.lanes;
    t.code = (halide_type_code_t)type_code;
    t.bits = (uint8_t)type_bits;
    return is;
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
    Point(int x, int y)
        : x(x), y(y) {
    }

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
    float h_scale = 1.0f;

    Label() = default;
    Label(const std::string &text, const Point &pos = {0, 0}, int fade_in_frames = 0, float h_scale = 1.f)
        : text(text), pos(pos), fade_in_frames(fade_in_frames), h_scale(h_scale) {
    }

    friend std::ostream &operator<<(std::ostream &os, const Label &label) {
        os << escape_spaces(label.text) << " " << label.pos << " " << label.fade_in_frames << " " << label.h_scale;
        return os;
    }

    friend std::istream &operator>>(std::istream &is, Label &label) {
        is >> label.text >> label.pos >> label.fade_in_frames >> label.h_scale;
        label.text = unescape_spaces(label.text);
        return is;
    }
};

// Configuration for how a func should be rendered in HalideTraceViz.
struct FuncConfig {
    // Note that every field in this struct is initialized to a value which
    // means "no value specified"; this is allows us to merge configs
    // from several sources (auto-layout, embedded trace-tags, and command-line)
    // in a way that we can selectively add or override some-but-not-all configuration
    // values (e.g., use auto-layout's positioning, but customizing labels
    // and changing rgb vs gray rendering). In all cases, if a field is
    // the initial "no value specified" value at rendering time, HalideTraceViz
    // will choose a reasonable value for that field.

    // Each value of a Func will draw as a zoom x zoom
    // box in the output. Fractional values are allowed.
    //
    // Valid values: 0.0 < zoom <= HUGEVAL or so
    float zoom = -1.f;

    // Each load from a Func costs the given number of ticks.
    // Legal values are 0.0 < zoom <= 1000 or so
    //
    // Valid values: load_cost >= 0
    int load_cost = -1;

    // Each store to a Func costs the given number of ticks.
    //
    // Valid values: store_cost >= 0
    int store_cost = -1;

    // The position on the screen corresponding to the Func's 0, 0 coordinate.
    //
    // Valid values: pos.x and pos.y > std::numeric_limits<int>::lowest()
    Point pos = {std::numeric_limits<int>::lowest(), std::numeric_limits<int>::lowest()};

    // Specifies the matrix that maps the coordinates of the
    // Func to screen pixels. Specified column major. For example,
    // { {1, 0}, {0, 1}, {0, 0} } specifies that the Func has three
    // dimensions where the first one maps to screen-space x
    // coordinates, the second one maps to screen-space y coordinates,
    // and the third one does not affect screen-space coordinates.
    //
    // Valid values: strize.size() > 0
    std::vector<Point> strides;

    // Specify the dimension to use for rendering the color channels of the Func.
    //
    // Valid values: color_dim == -1 -> render as grayscale
    //               color_dim >= 0  -> render as RGB using that dimension as color channel
    int color_dim = -2;

    // The minimum value taken on by a Func; maps to black.
    // TODO: this doesn't give enough range to allow for the full range of int64 or uint64. Do we care?
    //
    // Valid values: min-of-type <= min <= max-of-type
    double min = std::numeric_limits<double>::quiet_NaN();

    // The maximum value taken on by a Func; maps to white.
    // TODO: this doesn't give enough range to allow for the full range of int64 or uint64. Do we care?
    //
    // Valid values: min-of-type <= min <= max-of-type
    double max = std::numeric_limits<double>::quiet_NaN();

    // Label(s) to be rendered with the Func. The Label's position
    // is an offset from the Func's position, so (0, 0) means render
    // at the top-left of the Func itself.
    //
    // Valid values: Any
    std::vector<Label> labels;

    // If blank_on_end_realization > 0, the output occupied by a Func will be set to
    // black on its end-realization event; if blank_on_end_realization == 0, the
    // Func's values will be left on the screen.
    //
    // Valid values: 0 or 1.
    int blank_on_end_realization = -1;

    // Specifies the on-screen color corresponding to uninitialized memory,
    // in 0x00BBGGRR format. 0x00010101 is a "magic" value that will actually
    // fill with a checkerboard pattern.
    //
    // Valid values: Any uint32 with 0x00 in the upper 8 bits.
    uint32_t uninitialized_memory_color = 0xFFFFFFFF;

    // For each field in 'from':
    // -- if it has a well-defined value, replace the corresponding field in 'this'
    // -- if it does not have a well-defined value, leave untouched the corresponding field in 'this'
    void merge_from(const FuncConfig &from) {
        if (from.zoom >= 0.f) {
            this->zoom = from.zoom;
        }
        if (from.load_cost >= 0) {
            this->load_cost = from.load_cost;
        }
        if (from.store_cost > 0) {
            this->store_cost = from.store_cost;
        }
        if (from.pos.x > std::numeric_limits<int>::lowest()) {
            this->pos.x = from.pos.x;
        }
        if (from.pos.y > std::numeric_limits<int>::lowest()) {
            this->pos.y = from.pos.y;
        }
        if (!from.strides.empty()) {
            this->strides = from.strides;
        }
        if (from.color_dim >= -1) {
            this->color_dim = from.color_dim;
        }
        if (!std::isnan(from.min)) {
            this->min = from.min;
        }
        if (!std::isnan(from.max)) {
            this->max = from.max;
        }
        if (!from.labels.empty()) {
            this->labels = from.labels;
        }
        if (from.blank_on_end_realization >= 0) {
            this->blank_on_end_realization = from.blank_on_end_realization;
        }
        if (!(from.uninitialized_memory_color & 0xff000000)) {
            this->uninitialized_memory_color = from.uninitialized_memory_color;
        }
    }

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
        // Conforming C++ implementations are allowed to fail when reading
        // 'nan', 'inf', etc for floating-point values, so read these as
        // text and reality-check them ourselves.
        std::string min_text, max_text;
        is >>
            start_text >>
            config.zoom >>
            config.load_cost >>
            config.store_cost >>
            config.pos >>
            config.strides >>
            config.color_dim >>
            min_text >>
            max_text >>
            config.labels >>
            config.blank_on_end_realization >>
            config.uninitialized_memory_color;

        const auto parse_double = [](const std::string &s) -> double {
            double d;
            std::istringstream iss(s);
            iss >> d;
            if (iss.fail() || iss.get() != EOF) {
                // If it fails, just use nan for the value.
                // (Could upgrade to guess at +-Inf if we ever care.)
                d = std::numeric_limits<double>::quiet_NaN();
            }
            return d;
        };
        config.min = parse_double(min_text);
        config.max = parse_double(max_text);

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

    explicit FuncConfig(const std::string &trace_tag, const ErrorFunc &error = default_error) {
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
    Point frame_size = {1920, 1080};

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

    // If true, automatically layout every realized func we see, in left-to-right,
    // top-to-bottom order as they are first touched.
    bool auto_layout = false;

    // If doing auto-layout, divide the frame into this many rows and columns,
    // filling in each cell in left-to-right, top-to-bottom order. If either
    // value is -1, calculate a cell size based on the number of boxes touched.
    Point auto_layout_grid = {-1, -1};

    // If doing auto-layout, the padding to use between each cell.
    Point auto_layout_pad = {32, 32};

    // Specifies the default on-screen color corresponding to uninitialized memory,
    // in 0x00BBGGRR format. 0x00010101 is a "magic" value that will actually
    // fill with a checkerboard pattern. This will be used for any Func that doesn't
    // override it in its FuncConfig.
    //
    // Valid values: Any uint32 with 0x00 in the upper 8 bits.
    uint32_t default_uninitialized_memory_color = 0xFFFFFFFF;

    static std::string tag_start_text() {
        return std::string("htv_global_config:");
    }

    static bool match(const std::string &trace_tag) {
        return trace_tag.find(tag_start_text()) == 0;
    }

    void dump(std::ostream &os) const {
        os << std::boolalpha
           << "Global:\n"
           << "  frame_size: " << frame_size << "\n"
           << "  decay_factor_during_compute: " << decay_factor_during_compute << "\n"
           << "  decay_factor_after_compute: " << decay_factor_after_compute << "\n"
           << "  hold_frames: " << hold_frames << "\n"
           << "  timestep: " << timestep << "\n"
           << "  auto_layout: " << auto_layout << "\n"
           << "  auto_layout_grid: " << auto_layout_grid << "\n"
           << "  auto_layout_pad: " << auto_layout_grid << "\n"
           << "  default_uninitialized_memory_color: " << default_uninitialized_memory_color << "\n";
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
            << config.timestep << " "
            << config.auto_layout << " "
            << config.auto_layout_grid << " "
            << config.auto_layout_pad << " "
            << config.default_uninitialized_memory_color;
        return os;
    }

    friend std::istream &operator>>(std::istream &is, GlobalConfig &config) {
        std::string start_text;
        is >>
            start_text >>
            config.frame_size >>
            config.decay_factor_during_compute >>
            config.decay_factor_after_compute >>
            config.hold_frames >>
            config.timestep >>
            config.auto_layout >>
            config.auto_layout_grid >>
            config.auto_layout_pad >>
            config.default_uninitialized_memory_color;
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

    explicit GlobalConfig(const std::string &trace_tag, const ErrorFunc &error = default_error) {
        std::istringstream is(trace_tag);
        is >> *this;
        if (is.fail() || is.get() != EOF) {
            error("GlobalConfig trace_tag parsing error");
        }
    }
};

// We don't use halide_dimension_t here because we don't want stride.
struct Range {
    int min = 0, extent = 0;

    Range() = default;
    Range(int min, int extent)
        : min(min), extent(extent) {
    }

    friend std::ostream &operator<<(std::ostream &os, const Range &dim) {
        os << dim.min << " " << dim.extent;
        return os;
    }

    friend std::istream &operator>>(std::istream &is, Range &dim) {
        is >> dim.min >> dim.extent;
        return is;
    }
};

// TODO name is terrible
struct FuncTypeAndDim {
    std::vector<halide_type_t> types;
    std::vector<Range> dims;

    static std::string tag_start_text() {
        return std::string("func_type_and_dim:");
    }

    static bool match(const std::string &trace_tag) {
        return trace_tag.find(tag_start_text()) == 0;
    }

    void dump(std::ostream &os, const std::string &name) const {
        static const char *const type_name[4] = {"int", "uint", "float", "handle"};
        os << "FuncTypeAndDim: " << name << "\n";
        os << "  types:";
        for (const auto &type : types) {
            os << " " << type_name[type.code & 3] << (int)type.bits;
            if (type.lanes > 1) {
                os << "x" << type.lanes;
            }
        }
        os << "\n";
        os << "  dims: " << dims << "\n";
    }

    friend std::ostream &operator<<(std::ostream &os, const FuncTypeAndDim &types_and_ranges) {
        os << tag_start_text()
           << " " << types_and_ranges.types
           << " " << types_and_ranges.dims;
        return os;
    }

    friend std::istream &operator>>(std::istream &is, FuncTypeAndDim &types_and_ranges) {
        std::string start_text;
        is >> start_text >> types_and_ranges.types >> types_and_ranges.dims;
        if (start_text != tag_start_text()) {
            is.setstate(std::ios::failbit);
        }
        return is;
    }

    std::string to_trace_tag() const {
        std::ostringstream os;
        os << *this;
        return os.str();
    }

    FuncTypeAndDim() = default;

    explicit FuncTypeAndDim(const std::string &trace_tag, const ErrorFunc &error = default_error) {
        std::istringstream is(trace_tag);
        is >> *this;
        if (is.fail() || is.get() != EOF) {
            error("FuncTypeAndDim trace_tag parsing error");
        }
    }
};

}  // namespace Trace
}  // namespace Halide

#endif  // HALIDE_TRACE_CONFIG_H
