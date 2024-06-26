#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#ifdef _MSC_VER
#include <fcntl.h>
#include <io.h>
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#else
#include <unistd.h>
#endif

#include "HalideRuntime.h"
#include "inconsolata.h"

#include "halide_trace_config.h"

using namespace Halide;
using namespace Halide::Trace;

namespace {

// -------------------------------------------------------------

bool verbose = false;

// Log informational output to stderr, but only in verbose mode
struct info {
    std::ostringstream msg;

    template<typename T>
    info &operator<<(const T &x) {
        if (verbose) {
            msg << x;
        }
        return *this;
    }

    ~info() {
        if (verbose) {
            if (msg.str().back() != '\n') {
                msg << "\n";
            }
            std::cerr << msg.str();
        }
    }
};

// Log warnings to stderr
struct warn {
    std::ostringstream msg;

    template<typename T>
    warn &operator<<(const T &x) {
        msg << x;
        return *this;
    }

    ~warn() {
        if (msg.str().back() != '\n') {
            msg << "\n";
        }
        std::cerr << "Warning: " << msg.str();
    }
};

// Log unrecoverable errors to stderr, then exit
struct fail {
    std::ostringstream msg;

    template<typename T>
    fail &operator<<(const T &x) {
        msg << x;
        return *this;
    }

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4722)  // destructor never returns, potential memory leak
#endif
    ~fail() {
        if (msg.str().back() != '\n') {
            msg << "\n";
        }
        std::cerr << msg.str();
        exit(1);
    }
#ifdef _MSC_VER
#pragma warning(pop)
#endif
};

// -------------------------------------------------------------

template<typename T>
T value_as(const halide_type_t &type, const halide_scalar_value_t &value) {
    switch (type.element_of().as_u32()) {
    case halide_type_t(halide_type_int, 8).as_u32():
        return (T)value.u.i8;
    case halide_type_t(halide_type_int, 16).as_u32():
        return (T)value.u.i16;
    case halide_type_t(halide_type_int, 32).as_u32():
        return (T)value.u.i32;
    case halide_type_t(halide_type_int, 64).as_u32():
        return (T)value.u.i64;
    case halide_type_t(halide_type_uint, 1).as_u32():
        return (T)value.u.b;
    case halide_type_t(halide_type_uint, 8).as_u32():
        return (T)value.u.u8;
    case halide_type_t(halide_type_uint, 16).as_u32():
        return (T)value.u.u16;
    case halide_type_t(halide_type_uint, 32).as_u32():
        return (T)value.u.u32;
    case halide_type_t(halide_type_uint, 64).as_u32():
        return (T)value.u.u64;
    case halide_type_t(halide_type_float, 32).as_u32():
        return (T)value.u.f32;
    case halide_type_t(halide_type_float, 64).as_u32():
        return (T)value.u.f64;
    default:
        fail() << "Can't convert packet with type: " << (int)type.code << "bits: " << type.bits;
        return (T)0;
    }
}

template<typename T>
T get_value_as(const halide_trace_packet_t &p, int idx) {
    const uint8_t *val = (const uint8_t *)(p.value()) + idx * p.type.bytes();
    // 'val' may not be aligned: memcpy it to an aligned local
    // so that value_as<>() won't complain under sanitizers.
    halide_scalar_value_t aligned_value;
    // Only copy the number of bytes in the type: the stream isn't guaranteed
    // to be padded to sizeof(halide_scalar_value_t).
    memcpy(&aligned_value, val, p.type.bits / 8);
    return value_as<double>(p.type, aligned_value);
}

struct PacketAndPayload : public halide_trace_packet_t {
    uint8_t payload[4096];

    static bool read_or_die(void *buf, size_t count) {
        char *p = (char *)buf;
        char *p_end = p + count;
        while (p < p_end) {
            int64_t bytes_read = ::read(STDIN_FILENO, p, p_end - p);
            if (bytes_read == 0) {
                return false;  // EOF
            } else if (bytes_read < 0) {
                fail() << "Unable to read packet";
            }
            p += bytes_read;
        }
        assert(p == p_end);
        return true;
    }

    bool read() {
        constexpr size_t header_size = sizeof(halide_trace_packet_t);
        if (!read_or_die(this, header_size)) {
            return false;  // EOF
        }

        const size_t payload_size = this->size - header_size;
        if (payload_size > sizeof(this->payload) || !read_or_die(this->payload, payload_size)) {
            // Shouldn't ever get EOF here
            fail() << "Unable to read packet payload of size " << payload_size;
        }
        return true;
    }
};

// -------------------------------------------------------------

// A struct specifying how a single Func will get visualized.
struct FuncInfo {

    // Info about Funcs type and touched-extent, emitted
    // by the tracing code.
    FuncTypeAndDim type_and_dim;
    bool type_and_dim_valid = false;
    int layout_order = -1;

    // Configuration for how the func should be drawn
    FuncConfig config;
    bool config_valid = false;

    // Information about actual observed values gathered while parsing the trace
    struct Observed {
        std::string qualified_name;
        int first_draw_time = -1, first_packet_idx = -1;
        double min_value = 0.0, max_value = 0.0;
        int min_coord[16];
        int max_coord[16];
        int num_realizations = 0, num_productions = 0;
        uint64_t stores = 0, loads = 0;

        Observed() {
            memset(min_coord, 0, sizeof(min_coord));
            memset(max_coord, 0, sizeof(max_coord));
        }

        void observe_load(const halide_trace_packet_t &p) {
            observe_load_or_store(p);
            loads += p.type.lanes;
        }

        void observe_store(const halide_trace_packet_t &p) {
            observe_load_or_store(p);
            stores += p.type.lanes;
        }

        void observe_load_or_store(const halide_trace_packet_t &p) {
            const int *coords = p.coordinates();
            for (int i = 0; i < std::min(16, p.dimensions / p.type.lanes); i++) {
                for (int lane = 0; lane < p.type.lanes; lane++) {
                    int coord = coords[i * p.type.lanes + lane];
                    if (loads + stores == 0 && lane == 0) {
                        min_coord[i] = coord;
                        max_coord[i] = coord + 1;
                    } else {
                        min_coord[i] = std::min(min_coord[i], coord);
                        max_coord[i] = std::max(max_coord[i], coord + 1);
                    }
                }
            }

            for (int i = 0; i < p.type.lanes; i++) {
                double value = get_value_as<double>(p, i);
                if (stores + loads == 0) {
                    min_value = value;
                    max_value = value;
                } else {
                    min_value = std::min(min_value, value);
                    max_value = std::max(max_value, value);
                }
            }
        }

        void report() {
            std::ostringstream o;
            for (int i = 0; i < 16; i++) {
                if (min_coord[i] == 0 && max_coord[i] == 0) {
                    break;
                }
                if (i > 0) {
                    o << " x ";
                }
                o << "[" << min_coord[i] << ", " << max_coord[i] << ")";
            }
            info()
                << "Func " << qualified_name << ":\n"
                << o.str() << "\n"
                << " range of values: [" << min_value << ", " << max_value << "]\n"
                << " number of realizations: " << num_realizations << "\n"
                << " number of productions: " << num_productions << "\n"
                << " number of loads: " << loads << "\n"
                << " number of stores: " << stores << "\n";
        }

    } stats;
};

struct VizState {
    GlobalConfig globals;
    std::map<std::string, FuncInfo> funcs;
};

// -------------------------------------------------------------

// -------------------------------------------------------------

std::string usage() {
    return
        R"USAGE(
HalideTraceViz accepts Halide-generated binary tracing packets from
stdin, and outputs them as raw 8-bit rgba32 pixel values to
stdout. You should pipe the output of HalideTraceViz into a video
encoder or player.

E.g. to encode a video:
 HL_TARGET=host-trace_all <command to make pipeline> && \
 HL_TRACE_FILE=/dev/stdout <command to run pipeline> | \
 HalideTraceViz -s 1920 1080 -t 10000 <the -f args> | \
 avconv -f rawvideo -pix_fmt bgr32 -s 1920x1080 -i /dev/stdin -c:v h264 output.avi

To just watch the trace instead of encoding a video replace the last
line with something like:
 mplayer -demuxer rawvideo -rawvideo w=1920:h=1080:format=rgba:fps=30 -idle -fixed-vo -

The arguments to HalideTraceViz specify how to lay out and render the
Funcs of interest. It acts like a stateful drawing API. The following
parameters should be set zero or one times:

 --size width height: The size of the output frames. Defaults to
     1920x1080.

 --timestep timestep: How many Halide computations should be covered
     by each frame. Defaults to 10000.

 --decay A B: How quickly should the yellow and blue highlights decay
     over time. This is a two-stage exponential decay with a knee in
     it. A controls the rate at which they decay while a value is in
     the process of being computed, and B controls the rate at which
     they decay over time after the corresponding value has finished
     being computed. 1 means never decay, 2 means halve in opacity
     every frame, and 256 or larger means instant decay. The default
     values for A and B are 1 and 2 respectively, which means that the
     highlight holds while the value is being computed, and then
     decays slowly.

 --hold frames: How many frames to output after the end of the
    trace. Defaults to 250.

The following parameters can be set once per Func. With the exception
of label, they continue to take effect for all subsequently defined
Funcs.

 --min: The minimum value taken on by a Func. Maps to black.

 --max: The maximum value taken on by a Func. Maps to white.

 --rgb dim: Render Funcs as rgb, with the dimension dim indexing the
     color channels.

 --gray: Render Funcs as grayscale.

 --blank: Specify that the output occupied by a Func should be set to
     black on its end-realization event.

 --no-blank: The opposite of --blank. Leaves the Func's values on the
     screen. This is the default

 --zoom factor: Each value of a Func will draw as a factor x factor
     box in the output. Fractional values are allowed.

 --load time: Each load from a Func costs the given number of ticks.

 --store time: Each store to a Func costs the given number of ticks.

 --move x y: Sets the position on the screen corresponding to the
   Func's 0, 0 coordinate.

 --left dx: Moves the currently set position leftward by the given
     amount.

 --right dx: Moves the currently set position rightward by the given
     amount.

 --up dy: Moves the currently set position upward by the given amount.

 --down dy: Moves the currently set position downward by the given
     amount.

 --push: Copies the currently set position onto a stack of positions.

 --pop: Sets the current position to the value most-recently pushed,
   and removes it from the stack.

 --strides ... : Specifies the matrix that maps the coordinates of the
     Func to screen pixels. Specified column major. For example,
     --strides 1 0  0 1  0 0 specifies that the Func has three
     dimensions where the first one maps to screen-space x
     coordinates, the second one maps to screen-space y coordinates,
     and the third one does not affect screen-space coordinates.

 --uninit r g b : Specifies the on-screen color corresponding to
   uninitialized memory. Defaults to black.

 --uninit_default r g b : Specifies the default on-screen color
   corresponding to uninitialized memory, to be used when a func-specific
   --uninit setting is not available.  Defaults to black.

 --func name: Mark a Func to be visualized. Uses the currently set
     values of the parameters above to specify how.

 --label func label n: When the named Func is first touched, the label
     appears with its bottom left corner at the current coordinates
     and fades in over n frames.

 --rlabel func label dx dy n: Like "--label", but relative to the Func's
     position, using dx and dy as an offset.

 --auto_layout: Enables automatic layout of funcs.  The funcs will be
     automatically arranged in a grid, in the order they appear in the
     trace file, with labels and appropriate zoom levels.

 --no-auto_layout: Disables automatic layout of funcs.  This is the default.

 --auto_layout_grid x y: Specify the size of the grid generated by
     --auto_layout mode.  The default is to determine this automatically,
     to roughly maximize use of space on screen.

 --ignore_tags: Indicates that the auto layout feature should ignore config
     tags in the trace data, added by func.add_trace_tag().

 --no-ignore_tags: Indicates that the auto layout feature should obey config
     tags in the trace data, overriding the auto-generated layouts.  This is
     the default.

 --help: Write this usage information to stdout, and exit.

 --verbose: Write additional informational messages to stderr.

 --no-verbose: Disable additional informational messages to stderr.
     This is the default.

)USAGE";
}

// Calculate the maximum 2d rendered size for a given Box and stride, assuming
// a zoom factor of 1. This uses the same recursive approach as fill_realization()
// for simplicity.
void calc_2d_size(const std::vector<Range> &dims, const std::vector<Point> &strides, Range *x, Range *y,
                  int current_dimension = 0, int x_off = 0, int y_off = 0) {
    if (current_dimension == 0) {
        x->min = 2147483647;
        x->extent = -2147483647;
        y->min = 2147483647;
        y->extent = -2147483647;
    }
    if (current_dimension == (int)dims.size()) {
        x->min = std::min(x->min, x_off);
        x->extent = std::max(x->extent, x_off);
        y->min = std::min(y->min, y_off);
        y->extent = std::max(y->extent, y_off);
    } else {
        const auto &m = dims.at(current_dimension);
        const Point &stride = strides.at(current_dimension);
        x_off += stride.x * m.min;
        y_off += stride.y * m.min;
        for (int i = 0; i < m.extent; i++) {
            calc_2d_size(dims, strides, x, y, current_dimension + 1, x_off, y_off);
            x_off += stride.x;
            y_off += stride.y;
        }
    }
    if (current_dimension == 0) {
        x->extent = std::max(1, x->extent - x->min + 1);
        y->extent = std::max(1, y->extent - y->min + 1);
    }
}

// -------------------------------------------------------------

// Given a FuncConfig, check each field for "use some reasonable default"
// value and fill in something reasonable.
void finalize_func_config_values(const GlobalConfig &globals, FuncInfo &fi) {
    // Make a FuncConfig with 'safe' defaults for everything,
    // then merge the existing cfg into it.
    FuncConfig safe;
    safe.zoom = 1.f;
    safe.load_cost = 0;
    safe.store_cost = 1;
    safe.pos = {0, 0};
    safe.strides = {{1, 0}, {0, 1}};
    safe.color_dim = -1;
    safe.min = 0.0;
    safe.max = 1.0;
    safe.labels = {};
    safe.blank_on_end_realization = 0;
    safe.uninitialized_memory_color = globals.default_uninitialized_memory_color;

    if (fi.type_and_dim_valid) {
        // Try to choose better values for min and max based on type.
        // TODO: only considers the first type given; in general,
        // HTV doesn't deal with Tuple-valued Funcs very well.
        const halide_type_t &type = fi.type_and_dim.types.at(0);
        if (type.code == halide_type_uint) {
            safe.max = (double)((1 << type.bits) - 1);
        } else if (type.code == halide_type_int) {
            double d = (double)(1 << (type.bits - 1));
            safe.max = d - 1;
            // safe.min = -d;
            // In practice, assuming a min of zero (rather then -INT_MIN)
            // for signed types produces less-weird results.
            safe.min = 0.0;
        }
    }

    safe.merge_from(fi.config);
    safe.uninitialized_memory_color |= 0xff000000;
    fi.config = safe;
}

// Given a FuncConfig, check each field for "use some reasonable default"
// value and fill in something reasonable.
void finalize_func_config_values(const GlobalConfig &globals, std::map<std::string, FuncInfo> &funcs) {
    for (auto &p : funcs) {
        auto &fi = p.second;

        finalize_func_config_values(globals, fi);
    }
}

void do_auto_layout(const GlobalConfig &globals, const std::string &func_name, FuncInfo &fi) {
    assert(fi.type_and_dim_valid);

    const Point &pad = globals.auto_layout_pad;
    Point cell_size = {
        globals.frame_size.x / globals.auto_layout_grid.x,
        globals.frame_size.y / globals.auto_layout_grid.y};
    info() << "cell_size is " << cell_size << "\n";
    info() << "auto_layout_pad is " << pad << "\n";

    int row = fi.layout_order / globals.auto_layout_grid.x;
    int col = fi.layout_order % globals.auto_layout_grid.x;

    if (fi.config.color_dim < -1) {
        // If color_dim is unspecified and it looks like a 2d RGB Func, make it one
        const auto &dims = fi.type_and_dim.dims;
        if (dims.size() == 3) {
            if ((dims[2].extent == 3 || dims[2].extent == 4)) {
                fi.config.color_dim = 2;
            } else if ((dims[0].extent == 3 || dims[0].extent == 4)) {
                fi.config.color_dim = 0;
                if (fi.config.strides.empty()) {
                    fi.config.strides = {{0, 0}, {1, 0}, {0, 1}};
                }
            }
        } else if (dims.size() == 4) {
            // 4D, maybe a Tensor? Treat as grayscale with x = dim(1), y = dim(2)
            fi.config.strides = {{0, 0}, {1, 0}, {0, 1}, {0, 0}};
        }
    }

    if (fi.config.zoom < 0.f) {
        // Ensure that all of the FuncInfos have strides that match
        // the number of dimensions expected by FuncTypeAndDim, adding
        // zero-stride pairs as needed (this simplifies rendering checks
        // later on)
        if (fi.config.strides.empty()) {
            fi.config.strides = {{1, 0}, {0, 1}};
        }
        while (fi.config.strides.size() < fi.type_and_dim.dims.size()) {
            fi.config.strides.emplace_back(0, 0);
        }

        // Calc the 2d size that this would render at (including stride-stretching) for zoom=1
        Range xr, yr;
        calc_2d_size(fi.type_and_dim.dims, fi.config.strides, &xr, &yr);
        info() << "calc_2d_size for " << func_name << " is " << xr << ", " << yr << "\n";

        // Use that size to calculate the zoom we need -- this chooses
        // a zoom that maximizes the size within the cell.
        float zoom_x = (float)(cell_size.x - pad.x) / (float)xr.extent;
        float zoom_y = (float)(cell_size.y - pad.y) / (float)yr.extent;
        fi.config.zoom = std::min(zoom_x, zoom_y);

        // Try to choose an even-multiple zoom for better display
        // and just less weirdness.
        if (fi.config.zoom > 100.f) {
            // Zooms this large are usually for things like input matrices.
            // Perhaps clamp at something smaller?
            fi.config.zoom = std::floor(fi.config.zoom / 100.f) * 100.f;
        } else if (fi.config.zoom > 10.f) {
            fi.config.zoom = std::floor(fi.config.zoom / 10.f) * 10.f;
        } else if (fi.config.zoom > 1.f) {
            fi.config.zoom = std::floor(fi.config.zoom * 2.f) / 2.f;
        } else if (fi.config.zoom < 1.f) {
            fi.config.zoom = std::ceil(fi.config.zoom * 20.f) / 20.f;
        }
        info() << "zoom for " << func_name << " is " << zoom_x << " " << zoom_y << " -> " << fi.config.zoom << "\n";
    }

    // Put the image at the top-left of the cell. (Should we try to
    // center within the cell?)
    if (fi.config.pos.x < 0 && fi.config.pos.y < 0) {
        fi.config.pos.x = col * cell_size.x + pad.x;
        fi.config.pos.y = row * cell_size.y + pad.y;
    }
    info() << "pos for " << func_name << " is " << fi.config.pos.x << " " << fi.config.pos.y << "\n";

    if (fi.config.labels.empty()) {
        std::string label_suffix = " (" + std::to_string((int)(fi.config.zoom * 100)) + "%)";
        std::string label = func_name + label_suffix;
        const int label_space = cell_size.x - pad.x * 2;
        float h_scale = 1.f;
        int label_width = label.size() * inconsolata_char_width;
        if (label_width > label_space) {
            // "minimum" depends on lots of things but for 1080p output, 70% seems fair
            const float min_readable_h_scale = 0.7f;
            h_scale = std::max(min_readable_h_scale, std::min(1.f, (float)label_space / (float)label_width));
            info() << "h_scale for label (" << label << ") is " << h_scale << "\n";
            // Still too wide? Discard the suffix.
            if (label_width * h_scale > label_space) {
                label = func_name;
                label_width = label.size() * inconsolata_char_width;
            }
            // Still too wide? Try lopping off characters to shorten it rather
            // than squishing it into oblivion. Let's lop off the *beginning*
            // rather than the end, on the assumption that long names are more unique at the end.
            if (label_width * h_scale > label_space) {
                while (label.size() > 1 && (label.size() + 1) * inconsolata_char_width * h_scale > label_space) {
                    label = label.substr(1);
                }
                // prepend "~" to hint it's squished
                label = "~" + label;
                info() << "label squished to (" << label << ")\n";
            }
        }
        fi.config.labels.push_back({label, {0, 0}, 10, h_scale});
    }

    fi.config_valid = true;
}

void do_auto_layout(VizState &state) {
    if (!state.globals.auto_layout) {
        return;
    }

    for (auto &p : state.funcs) {
        const auto &func_name = p.first;
        auto &fi = p.second;
        do_auto_layout(state.globals, func_name, fi);
    }
}

float calc_side_length(int min_cells, int width, int height) {
    const float aspect_ratio = (float)width / (float)height;
    const float p = std::ceil(std::sqrt(min_cells * aspect_ratio));
    const float par = p / aspect_ratio;
    const float s = std::floor(par) * p < min_cells ?
                        height / std::ceil(par) :
                        width / p;
    return s;
}

// Calculate the 'best' cell size such that we can fit at least min_cells
// into the given width x height. Currently this calculates perfectly
// square cells, which is OK but a little wasteful (eg for min_cells=20
// and size 1920x1080, it calculates a grid of 7x4 which wastes 8 cells).
// We could probably do better if we just tried to keep the cells 'nearly'
// square (aspect ratio <= 1.25).
Point best_cell_size(int min_cells, int width, int height) {
    const float sx = calc_side_length(min_cells, width, height);
    const float sy = calc_side_length(min_cells, height, width);
    const int edge = floor(std::max(sx, sy));
    return {edge, edge};
}

// -------------------------------------------------------------

void process_args(int argc, char **argv, VizState *state) {
    GlobalConfig &globals = state->globals;
    std::map<std::string, FuncInfo> &funcs = state->funcs;

    // The struct's default values are what we want
    FuncConfig config;
    std::vector<Point> pos_stack;
    std::set<std::string> labels_seen;

    // If the condition is false, print usage and exit with error.
    const auto expect = [](bool cond, int i) {
        if (!cond) {
            if (i) {
                fail() << "Argument parsing failed at argument " << i << "\n"
                       << usage();
            } else {
                fail() << usage();
            }
        }
    };

    const auto parse_int = [](const char *str) -> int {
        char *endptr = nullptr;
        errno = 0;
        long result = strtol(str, &endptr, 0);
        if (errno == ERANGE || str == endptr) {
            fail() << "Unable to parse '" << str << "' as an int\n"
                   << usage();
        }
        return (int)result;
    };

    const auto parse_float = [](const char *str) -> float {
        char *endptr = nullptr;
        errno = 0;
        float result = strtof(str, &endptr);
        if (errno == ERANGE || str == endptr) {
            fail() << "Unable to parse '" << str << "' as a float\n"
                   << usage();
        }
        return result;
    };

    const auto parse_double = [](const char *str) -> double {
        char *endptr = nullptr;
        errno = 0;
        double result = strtod(str, &endptr);
        if (errno == ERANGE || str == endptr) {
            fail() << "Unable to parse '" << str << "' as a double\n"
                   << usage();
        }
        return result;
    };

    // Parse command line args
    int i = 1;
    while (i < argc) {
        std::string next = argv[i];
        if (next == "--size") {
            expect(i + 2 < argc, i);
            globals.frame_size.x = parse_int(argv[++i]);
            globals.frame_size.y = parse_int(argv[++i]);
        } else if (next == "--func") {
            expect(i + 1 < argc, i);
            const char *func = argv[++i];
            FuncInfo &fi = funcs[func];
            fi.config.merge_from(config);
            fi.config_valid = true;
        } else if (next == "--min") {
            expect(i + 1 < argc, i);
            config.min = parse_double(argv[++i]);
        } else if (next == "--max") {
            expect(i + 1 < argc, i);
            config.max = parse_double(argv[++i]);
        } else if (next == "--move") {
            expect(i + 2 < argc, i);
            config.pos.x = parse_int(argv[++i]);
            config.pos.y = parse_int(argv[++i]);
        } else if (next == "--left") {
            expect(i + 1 < argc, i);
            config.pos.x -= parse_int(argv[++i]);
        } else if (next == "--right") {
            expect(i + 1 < argc, i);
            config.pos.x += parse_int(argv[++i]);
        } else if (next == "--up") {
            expect(i + 1 < argc, i);
            config.pos.y -= parse_int(argv[++i]);
        } else if (next == "--down") {
            expect(i + 1 < argc, i);
            config.pos.y += parse_int(argv[++i]);
        } else if (next == "--push") {
            pos_stack.push_back(config.pos);
        } else if (next == "--pop") {
            expect(!pos_stack.empty(), i);
            config.pos = pos_stack.back();
            pos_stack.pop_back();
        } else if (next == "--rgb") {
            expect(i + 1 < argc, i);
            config.color_dim = parse_int(argv[++i]);
        } else if (next == "--gray") {
            config.color_dim = -1;
        } else if (next == "--blank") {
            config.blank_on_end_realization = 1;
        } else if (next == "--no-blank") {
            config.blank_on_end_realization = 0;
        } else if (next == "--zoom") {
            expect(i + 1 < argc, i);
            config.zoom = parse_float(argv[++i]);
        } else if (next == "--load") {
            expect(i + 1 < argc, i);
            config.load_cost = parse_int(argv[++i]);
        } else if (next == "--store") {
            expect(i + 1 < argc, i);
            config.store_cost = parse_int(argv[++i]);
        } else if (next == "--strides") {
            config.strides.clear();
            while (i + 1 < argc) {
                const char *next_arg = argv[i + 1];
                if (next_arg[0] == '-' &&
                    next_arg[1] == '-') {
                    break;
                }
                expect(i + 2 < argc, i);
                int x = parse_int(argv[++i]);
                int y = parse_int(argv[++i]);
                config.strides.emplace_back(x, y);
            }
        } else if (next == "--label") {
            expect(i + 3 < argc, i);
            char *func = argv[++i];
            char *text = argv[++i];
            int n = parse_int(argv[++i]);
            FuncInfo &fi = funcs[func];
            // A Label's position is relative to its Func's position;
            // the --label flag has always expected an absolute position,
            // so convert it to an offset.
            Point offset = {config.pos.x - fi.config.pos.x, config.pos.y - fi.config.pos.y};
            if (!labels_seen.count(func)) {
                // If there is at least one --label specified for a Func,
                // it overrides the entire previous std::set of labels, rather
                // than simply appending.
                fi.config.labels.clear();
                labels_seen.insert(func);
            }
            fi.config.labels.emplace_back(text, offset, n);
        } else if (next == "--rlabel") {
            expect(i + 5 < argc, i);
            char *func = argv[++i];
            char *text = argv[++i];
            int dx = parse_int(argv[++i]);
            int dy = parse_int(argv[++i]);
            int n = parse_int(argv[++i]);
            FuncInfo &fi = funcs[func];
            Point offset = {dx, dy};
            if (!labels_seen.count(func)) {
                // If there is at least one --label specified for a Func,
                // it overrides the entire previous std::set of labels, rather
                // than simply appending.
                fi.config.labels.clear();
                labels_seen.insert(func);
            }
            fi.config.labels.emplace_back(text, offset, n);
        } else if (next == "--timestep") {
            expect(i + 1 < argc, i);
            globals.timestep = parse_int(argv[++i]);
        } else if (next == "--decay") {
            expect(i + 2 < argc, i);
            globals.decay_factor_during_compute = parse_int(argv[++i]);
            globals.decay_factor_after_compute = parse_int(argv[++i]);
        } else if (next == "--hold") {
            expect(i + 1 < argc, i);
            globals.hold_frames = parse_int(argv[++i]);
        } else if (next == "--uninit") {
            expect(i + 3 < argc, i);
            int r = parse_int(argv[++i]);
            int g = parse_int(argv[++i]);
            int b = parse_int(argv[++i]);
            config.uninitialized_memory_color = ((b & 255) << 16) | ((g & 255) << 8) | (r & 255);
        } else if (next == "--auto_layout") {
            globals.auto_layout = true;
        } else if (next == "--no-auto_layout") {
            globals.auto_layout = false;
        } else if (next == "--auto_layout_grid") {
            expect(i + 2 < argc, i);
            globals.auto_layout_grid.x = parse_int(argv[++i]);
            globals.auto_layout_grid.y = parse_int(argv[++i]);
        } else if (next == "--uninit_default") {
            expect(i + 3 < argc, i);
            int r = parse_int(argv[++i]);
            int g = parse_int(argv[++i]);
            int b = parse_int(argv[++i]);
            globals.default_uninitialized_memory_color = ((b & 255) << 16) | ((g & 255) << 8) | (r & 255);
        } else if (next == "--ignore_tags" || next == "--no-ignore_tags") {
            // Already processed, just continue
        } else if (next == "--verbose" || next == "--no-verbose") {
            // Already processed, just continue
        } else {
            expect(false, i);
        }
        i++;
    }
}

// There are three layers - image data, an animation on top of
// it, and text labels. These layers get composited.
struct Surface {
    const Point frame_size;
    std::vector<uint32_t> image, anim, anim_decay, text_buf, blend;

    // Composite a single pixel of 'over' over a single pixel of 'under', writing the result into dst.
    // Note that under or over might be dst.
    static void composite_one(const uint32_t *under, const uint32_t *over, uint32_t *dst) {
        const uint32_t o = *over;
        const uint8_t alpha = o >> 24;
        // alpha is almost always 0 or 255.
        if (alpha == 0) {
            *dst = *under;
        } else if (alpha == 255) {
            *dst = o;
        } else {
            // TODO: this could be done using 64-bit ops more simply
            const uint8_t *a = (const uint8_t *)under;
            const uint8_t *b = (const uint8_t *)over;
            uint8_t *d = (uint8_t *)dst;
            d[0] = (alpha * b[0] + (255 - alpha) * a[0]) / 255;
            d[1] = (alpha * b[1] + (255 - alpha) * a[1]) / 255;
            d[2] = (alpha * b[2] + (255 - alpha) * a[2]) / 255;
            d[3] = 255 - (((255 - a[3]) * (255 - alpha)) / 255);
        }
    }

    void do_decay(int decay_factor, uint32_t *dst) {
        if (decay_factor != 1) {
            const uint32_t inv_d1 = (1 << 24) / std::max(1, decay_factor);
            for (uint32_t *dst_end = dst + frame_elems(); dst < dst_end; ++dst) {
                uint32_t color = *dst;
                uint32_t rgb = color & 0x00ffffff;
                uint32_t alpha = (color >> 24);
                alpha *= inv_d1;
                alpha &= 0xff000000;
                *dst = alpha | rgb;
            }
        }
    }

    // TODO this doesn't bounds-check against frame_size
    void do_draw_pixel(const float zoom, const int x, const int y, const uint32_t color, uint32_t *dst) {
        const int izoom = (int)std::ceil(zoom);
        const int y_advance = frame_size.x - izoom;
        dst += frame_size.x * y + x;
        for (int dy = 0; dy < izoom; dy++) {
            for (int dx = 0; dx < izoom; dx++) {
                *dst++ = color;
            }
            dst += y_advance;
        }
    }

    // Fill a rectangle in dst with color.
    // opaque RGB(1,1,1) is a "magic" color that means "fill with checkerboard".
    // dst is assumed to point to the start of a frame_size buffer.
    void fill_rect(int left, int top, int width, int height, uint32_t color, uint32_t *dst) {
        const int x_min = std::max(left, 0);
        const int x_end = std::min(left + width, frame_size.x);
        const int y_min = std::max(top, 0);
        const int y_end = std::min(top + height, frame_size.y);
        const int y_stride = frame_size.x - (x_end - x_min);
        dst += y_min * frame_size.x + x_min;
        if (color == 0xff010101) {
            for (int y = y_min; y < y_end; y++) {
                for (int x = x_min; x < x_end; x++) {
                    const int check = ((x / 16) % 2) ^ ((y / 16) % 2);
                    *dst++ = check ? 0xff808080 : 0xffffffff;
                }
                dst += y_stride;
            }
        } else {
            for (int y = y_min; y < y_end; y++) {
                for (int x = x_min; x < x_end; x++) {
                    *dst++ = color;
                }
                dst += y_stride;
            }
        }
    }

    // Set all boxes corresponding to positions in a Func's allocation to
    // the given color. Recursive to handle arbitrary
    // dimensionalities. Used by begin and end realization events.
    void do_fill_realization(uint32_t *dst, uint32_t color,
                             const FuncInfo &fi, const halide_trace_packet_t &p,
                             int current_dimension = 0, int x_off = 0, int y_off = 0) {
        if (2 * current_dimension == p.dimensions) {
            const int x_min = x_off * fi.config.zoom + fi.config.pos.x;
            const int y_min = y_off * fi.config.zoom + fi.config.pos.y;
            const int izoom = (int)std::ceil(fi.config.zoom);
            fill_rect(x_min, y_min, izoom, izoom, color, dst);
        } else {
            const int *coords = p.coordinates();
            const int min = coords[current_dimension * 2 + 0];
            const int extent = coords[current_dimension * 2 + 1];
            // If we don't have enough strides, assume subsequent dimensions have stride (0, 0)
            const Point pt = current_dimension < (int)fi.config.strides.size() ? fi.config.strides.at(current_dimension) : Point{0, 0};
            x_off += pt.x * min;
            y_off += pt.y * min;
            for (int i = 0; i < extent; i++) {
                do_fill_realization(dst, color, fi, p, current_dimension + 1, x_off, y_off);
                x_off += pt.x;
                y_off += pt.y;
            }
        }
    }

public:
    Surface(const Point &fs)
        : frame_size(fs),
          image(frame_elems()),
          anim(frame_elems()),
          anim_decay(frame_elems()),
          text_buf(frame_elems()),
          blend(frame_elems()) {
    }

    Surface(const Surface &) = delete;
    void operator=(const Surface &) = delete;

    size_t frame_elems() const {
        return frame_size.x * frame_size.y;
    }

    const uint32_t *frame_data() const {
        return this->blend.data();
    }

    uint32_t get_image_pixel(const int x, const int y) const {
        return image[frame_size.x * y + x];
    }

    void draw_text(const std::string &text, const Point &pos, uint32_t color, float h_scale = 1.0f) {
        uint32_t *dst = text_buf.data();

        // Drop any alpha component of color
        color &= 0xffffff;

        int c = -1;
        for (int chr : text) {
            ++c;

            // We only handle a subset of ascii
            if (chr < 32 || chr >= 32 + inconsolata_char_count) {
                chr = 32;
            }
            chr -= 32;

            const uint8_t *font_ptr = inconsolata_raw + chr * (inconsolata_char_width * inconsolata_char_height);
            const int h_scale_numerator = std::ceil(std::min(1.f, h_scale) * 256);
            for (int fy = 0; fy < inconsolata_char_height; fy++) {
                for (int fx = 0; fx < inconsolata_char_width; fx++) {
                    int px = pos.x + (((inconsolata_char_width * c + fx) * h_scale_numerator) >> 8);
                    int py = pos.y - inconsolata_char_height + fy + 1;
                    if (px < 0 || px >= frame_size.x ||
                        py < 0 || py >= frame_size.y) {
                        continue;
                    }
                    dst[py * frame_size.x + px] = (font_ptr[fy * inconsolata_char_width + fx] << 24) | color;
                }
            }
        }
    }

    void draw_anim_pixel(const float zoom, int x, int y, uint32_t color) {
        do_draw_pixel(zoom, x, y, color, anim.data());
    }

    void draw_image_pixel(const float zoom, int x, int y, uint32_t color) {
        do_draw_pixel(zoom, x, y, color, image.data());
    }

    void fill_realization(uint32_t color, const FuncInfo &fi, const halide_trace_packet_t &p) {
        do_fill_realization(image.data(), color, fi, p);
    }

    void composite() {
        // Composite text over anim over image
        uint32_t *anim_decay_px = anim_decay.data();
        uint32_t *anim_px = anim.data();
        uint32_t *image_px = image.data();
        uint32_t *text_px = text_buf.data();
        uint32_t *blend_px = blend.data();
        for (size_t i = 0; i < image.size(); i++) {
            // anim over anim_decay -> anim_decay
            composite_one(anim_decay_px, anim_px, anim_decay_px);
            // anim_decay over image -> blend
            composite_one(image_px, anim_decay_px, blend_px);
            // text over blend -> blend
            composite_one(blend_px, text_px, blend_px);
            anim_decay_px++;
            anim_px++;
            image_px++;
            text_px++;
            blend_px++;
        }
    }

    void decay_animations(int decay_factor_after_compute, int decay_factor_during_compute) {
        // Decay the anim_decay
        do_decay(decay_factor_after_compute, anim_decay.data());

        // Also decay the anim
        do_decay(decay_factor_during_compute, anim.data());
    }

    void clear_animations() {
        std::fill(anim.begin(), anim.end(), 0);
    }
};

using FlagProcessor = std::function<void(VizState *state)>;

int run(bool ignore_trace_tags, FlagProcessor flag_processor) {
    // State that determines how different funcs get drawn
    VizState state;

    // halide_clock counts halide events. video_clock counts how many
    // of these events have been output. When halide_clock gets ahead
    // of video_clock, we emit a new frame.
    size_t halide_clock = 0, video_clock = 0;
    bool is_state_finalized = false;
    bool seen_global_config_tag = false;

    std::unique_ptr<Surface> surface;

    const std::function<void()> finalize_state = [&]() -> void {
        if (is_state_finalized) {
            return;
        }

        is_state_finalized = true;

        if (verbose) {
            std::ostringstream dumps;
            for (const auto &p : state.funcs) {
                const auto &fi = p.second;
                assert(fi.type_and_dim_valid);
                fi.type_and_dim.dump(dumps, p.first);
            }
            info() << dumps.str();
        }

        // We wait until now to process the cmd-line args;
        // this allows us to override trace-tag specifications
        // via the commandline, which is handy for experimentations.
        flag_processor(&state);

        // allocate the surface after all tags and flags are processed
        surface = std::make_unique<Surface>(state.globals.frame_size);

        if (state.globals.auto_layout_grid.x < 0 || state.globals.auto_layout_grid.y < 0) {
            int cells_needed = 0;
            for (const auto &p : state.funcs) {
                if (p.second.type_and_dim_valid) {
                    cells_needed++;
                }
            }
            Point cell_size = best_cell_size(cells_needed, state.globals.frame_size.x, state.globals.frame_size.y);
            state.globals.auto_layout_grid.x = state.globals.frame_size.x / cell_size.x;
            state.globals.auto_layout_grid.y = state.globals.frame_size.y / cell_size.y;
            assert(state.globals.auto_layout_grid.x * state.globals.auto_layout_grid.y >= cells_needed);
            info() << "For cells_needed = " << cells_needed
                   << " using " << state.globals.auto_layout_grid.x << "x" << state.globals.auto_layout_grid.y << " grid"
                   << " with cells of size " << cell_size.x << "x" << cell_size.y;
        }

        // If globals.default_uninitialized_memory_color was never set, init to black or checkerboard.
        if (state.globals.default_uninitialized_memory_color & 0xff000000) {
            if (state.globals.auto_layout) {
                // auto-layout defaults to checkerboard.
                state.globals.default_uninitialized_memory_color = 0x00010101;
            } else {
                // non-auto-layout defaults to black, to preserve existing look.
                state.globals.default_uninitialized_memory_color = 0x00000000;
            }
        }

        do_auto_layout(state);
        finalize_func_config_values(state.globals, state.funcs);
    };

    struct PipelineInfo {
        std::string name;
        int32_t id;
    };
    std::map<uint32_t, PipelineInfo> pipeline_info;

    int layout_order = 0;
    std::list<std::pair<Label, int>> labels_being_drawn;
    size_t end_counter = 0;
    size_t packet_clock = 0;
    for (;;) {
        // Hold for some number of frames once the trace has finished.
        if (end_counter) {
            halide_clock += state.globals.timestep;
            if (end_counter >= (size_t)state.globals.hold_frames) {
                break;
            }
        }

        if (halide_clock > video_clock) {
            assert(is_state_finalized);

            const int64_t frame_bytes = surface->frame_elems() * sizeof(uint32_t);

            while (halide_clock > video_clock) {
                // Always render text last, since it's on top of everything
                // and there's no need to re-render for every packet.
                for (auto it = labels_being_drawn.begin(); it != labels_being_drawn.end();) {
                    const Label &label = it->first;
                    int first_draw_clock = it->second;
                    int frames_since_first_draw = (halide_clock - first_draw_clock) / state.globals.timestep;
                    if (frames_since_first_draw < label.fade_in_frames) {
                        uint32_t color = ((1 + frames_since_first_draw) * 255) / std::max(1, label.fade_in_frames);
                        if (color > 255) {
                            color = 255;
                        }
                        color *= 0x10101;
                        surface->draw_text(label.text, label.pos, color, label.h_scale);
                        ++it;
                    } else {
                        // Once we reach or exceed the final frame, draw at 100% opacity, then remove
                        surface->draw_text(label.text, label.pos, 0xffffff, label.h_scale);
                        it = labels_being_drawn.erase(it);
                    }
                }

                // Composite text over anim over image
                surface->composite();

                // Dump the frame
                int64_t bytes_written = write(STDOUT_FILENO, surface->frame_data(), frame_bytes);
                if (bytes_written < frame_bytes) {
                    fail() << "Could not write frame to stdout.";
                }

                video_clock += state.globals.timestep;

                surface->decay_animations(state.globals.decay_factor_after_compute, state.globals.decay_factor_during_compute);
            }

            // Blank anim
            surface->clear_animations();
        }

        // Read a tracing packet
        PacketAndPayload p;
        if (!p.read()) {
            end_counter++;
            continue;
        }
        packet_clock++;

        // It's a pipeline begin/end event
        if (p.event == halide_trace_begin_pipeline) {
            pipeline_info[p.id] = {p.func(), p.id};
            continue;
        } else if (p.event == halide_trace_end_pipeline) {
            assert(pipeline_info.count(p.parent_id));
            pipeline_info.erase(p.parent_id);
            continue;
        } else if (p.event == halide_trace_tag) {
            // If there are trace tags, they will come immediately after the pipeline's
            // halide_trace_begin_pipeline but before any realizations.
            if (halide_clock != 0 || video_clock != 0) {
                // Messing with timestamp, framesize, etc partway thru
                // a visualization would be bad, but let's just warn
                // rather than fail.
                // TODO: May need to check parent_id here, as nested
                // pipelines called via define_extern could emit these.
                warn() << "trace_tags are only expected at the start of a visualization:"
                       << " (" << p.trace_tag() << ") for func (" << p.func() << ")";
            }
            if (FuncConfig::match(p.trace_tag())) {
                if (ignore_trace_tags) {
                    continue;
                }
                FuncConfig cfg(p.trace_tag());
                auto &fi = state.funcs[p.func()];
                fi.config = cfg;
                fi.config_valid = true;
            } else if (GlobalConfig::match(p.trace_tag())) {
                if (ignore_trace_tags) {
                    continue;
                }
                if (seen_global_config_tag) {
                    warn() << "saw multiple GlobalConfig trace_tags, some will be ignored.";
                }
                state.globals = GlobalConfig(p.trace_tag());
                seen_global_config_tag = true;
            } else if (FuncTypeAndDim::match(p.trace_tag())) {
                auto &fi = state.funcs[p.func()];
                fi.type_and_dim = FuncTypeAndDim(p.trace_tag());
                fi.type_and_dim_valid = true;
                fi.layout_order = layout_order++;
            } else {
                warn() << "Ignoring trace_tag: (" << p.trace_tag() << ") for func (" << p.func() << ")";
            }
            continue;
        }

        finalize_state();

        const PipelineInfo pipeline = pipeline_info[p.parent_id];

        if (p.event == halide_trace_begin_realization ||
            p.event == halide_trace_produce ||
            p.event == halide_trace_consume) {
            assert(!pipeline_info.count(p.id));
            pipeline_info[p.id] = pipeline;
        } else if (p.event == halide_trace_end_realization ||
                   p.event == halide_trace_end_produce ||
                   p.event == halide_trace_end_consume) {
            assert(pipeline_info.count(p.parent_id));
            pipeline_info.erase(p.parent_id);
        }

        std::string qualified_name = pipeline.name + ":" + p.func();
        if (state.funcs.find(qualified_name) == state.funcs.end()) {
            if (state.funcs.find(p.func()) != state.funcs.end()) {
                state.funcs[qualified_name] = state.funcs[p.func()];
                state.funcs.erase(p.func());
            } else {
                warn() << "ignoring func " << qualified_name << " event " << p.event << "; parent event " << p.parent_id << " " << pipeline.name;
            }
        }

        // Draw the event
        FuncInfo &fi = state.funcs[qualified_name];
        if (!fi.config_valid) {
            continue;
        }

        if (fi.stats.first_draw_time < 0) {
            fi.stats.first_draw_time = halide_clock;

            for (const auto &label : fi.config.labels) {
                // Convert offset to absolute position before enqueuing
                Label l = label;
                l.pos.x += fi.config.pos.x;
                l.pos.y += fi.config.pos.y;
                labels_being_drawn.emplace_back(l, (int)halide_clock);
            }
        }

        if (fi.stats.first_packet_idx < 0) {
            fi.stats.first_packet_idx = packet_clock;
            fi.stats.qualified_name = qualified_name;
        }

        switch (p.event) {
        case halide_trace_load:
        case halide_trace_store: {
            if (p.event == halide_trace_store) {
                // Stores take time proportional to the number of
                // items stored times the cost of the func.
                halide_clock += fi.config.store_cost * p.type.lanes;
                fi.stats.observe_store(p);
            } else {
                halide_clock += fi.config.load_cost * p.type.lanes;
                fi.stats.observe_load(p);
            }

            // zero- or one-dimensional Funcs can have dimensions < strides.size().
            // This may seem confusing, so keep in mind:
            // fi.config.strides are provided by the --stride flag, so it can contain anything; i
            // if you don't specify them at all, they default to {{1,0},{0,1} (aka size=2).
            // So if we have excess strides, just ignore them.
            const int dims = std::min(p.dimensions / p.type.lanes, (int)fi.config.strides.size());
            const int *coords = p.coordinates();
            for (int lane = 0; lane < p.type.lanes; lane++) {
                // Compute the screen-space x, y coord to draw this.
                int x = fi.config.pos.x;
                int y = fi.config.pos.y;
                const float z = fi.config.zoom;
                for (int d = 0; d < dims; d++) {
                    const int coord = d * p.type.lanes + lane;
                    assert(coord < p.dimensions);
                    const int a = coords[coord];
                    const auto &stride = fi.config.strides[d];
                    x += z * stride.x * a;
                    y += z * stride.y * a;
                }

                // The box to draw must be entirely on-screen
                if (y < 0 || y >= state.globals.frame_size.y ||
                    x < 0 || x >= state.globals.frame_size.x ||
                    y + z - 1 < 0 || y + z - 1 >= state.globals.frame_size.y ||
                    x + z - 1 < 0 || x + z - 1 >= state.globals.frame_size.x) {
                    continue;
                }

                // Update one or more of the color channels of the
                // image layer in case it's a store or a load from
                // the input.
                if (p.event == halide_trace_store || fi.stats.num_realizations == 0 /* load from an input */) {
                    // Get the old color, in case we're only
                    // updating one of the color channels.
                    uint32_t image_color = surface->get_image_pixel(x, y);
                    double value = get_value_as<double>(p, lane);

                    // Normalize it.
                    value = std::max(0.0, std::min(255.0, 255.0 * (value - fi.config.min) /
                                                              (fi.config.max - fi.config.min)));

                    // Convert to 8-bit color.
                    uint8_t int_value = (uint8_t)value;

                    if (fi.config.color_dim < 0) {
                        // Grayscale
                        image_color = (int_value * 0x00010101) | 0xff000000;
                    } else {
                        // Color
                        uint32_t channel = coords[fi.config.color_dim * p.type.lanes + lane];
                        uint32_t mask = ~(255 << (channel * 8));
                        image_color &= mask;
                        image_color |= int_value << (channel * 8);
                    }
                    surface->draw_image_pixel(fi.config.zoom, x, y, image_color);
                }

                // Stores are orange, loads are blue.
                uint32_t color = p.event == halide_trace_load ? 0xffffdd44 : 0xff44ddff;
                surface->draw_anim_pixel(fi.config.zoom, x, y, color);
            }
            break;
        }
        case halide_trace_begin_realization:
            fi.stats.num_realizations++;
            surface->fill_realization(0xff000000 | fi.config.uninitialized_memory_color, fi, p);
            break;
        case halide_trace_end_realization:
            if (fi.config.blank_on_end_realization > 0) {
                surface->fill_realization(0, fi, p);
            }
            break;
        case halide_trace_produce:
            fi.stats.num_productions++;
            break;
        case halide_trace_end_produce:
        case halide_trace_consume:
        case halide_trace_end_consume:
        // Note that you can get nested pipeline begin/end events when you trace
        // something that has extern stages that are also Halide-being-traced;
        // these should just be ignored.
        case halide_trace_begin_pipeline:
        case halide_trace_end_pipeline:
        case halide_trace_tag:
            break;
        default:
            fail() << "Unknown tracing event code: " << p.event;
        }
    }

    if (verbose) {
        info() << "Total number of Funcs: " << state.funcs.size();

        // Dump this info at the end, since some is determined as we go
        std::ostringstream dumps;
        state.globals.dump(dumps);
        for (const auto &p : state.funcs) {
            const auto &fi = p.second;
            if (fi.config_valid) {
                fi.config.dump(dumps, p.first);
            }
        }
        info() << dumps.str();

        // Print stats about the Func gleaned from the trace.
        std::vector<std::pair<std::string, FuncInfo>> funcs;
        for (const auto &p : state.funcs) {
            funcs.emplace_back(p);
        }
        struct by_first_packet_idx {
            bool operator()(const std::pair<std::string, FuncInfo> &a,
                            const std::pair<std::string, FuncInfo> &b) const {
                return a.second.stats.first_packet_idx < b.second.stats.first_packet_idx;
            }
        };
        std::sort(funcs.begin(), funcs.end(), by_first_packet_idx());
        for (std::pair<std::string, FuncInfo> p : funcs) {
            p.second.stats.report();
        }
    }

    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc == 1) {
        std::cerr << usage();
        return 0;
    }

    bool ignore_trace_tags = false;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--help")) {
            std::cout << usage();
            exit(0);
        } else if (!strcmp(argv[i], "--ignore_tags")) {
            ignore_trace_tags = true;
        } else if (!strcmp(argv[i], "--no-ignore_tags")) {
            ignore_trace_tags = false;
        } else if (!strcmp(argv[i], "--verbose")) {
            verbose = true;
        } else if (!strcmp(argv[i], "--no-verbose")) {
            verbose = false;
        }
    }

    FlagProcessor flag_processor = [argc, argv](VizState *state) -> void {
        process_args(argc, argv, state);
    };

#ifdef _MSC_VER
    _setmode(STDIN_FILENO, _O_BINARY);
    _setmode(STDOUT_FILENO, _O_BINARY);
#endif

    run(ignore_trace_tags, flag_processor);
}
