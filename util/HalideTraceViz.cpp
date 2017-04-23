#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <vector>
#include <array>
#include <string>
#include <queue>
#include <iostream>
#include <algorithm>
#ifdef _MSC_VER
#include <io.h>
typedef int64_t ssize_t;
#else
#include <unistd.h>
#endif
#include <string.h>

#include "inconsolata.h"
#include "HalideRuntime.h"

namespace {

using std::map;
using std::vector;
using std::string;
using std::queue;
using std::array;
using std::pair;

// A struct representing a single Halide tracing packet.
struct Packet : public halide_trace_packet_t {
    // Not all of this will be used, but this
    // is the max possible packet size we
    // consider here.
    uint8_t payload[4096];

    int get_coord(int idx) const {
        return coordinates()[idx];
    }

    template<typename T>
    T get_value_as(int idx) const {
        switch (type.code) {
        case halide_type_int:
            switch (type.bits) {
            case 8:
                return (T)(((const int8_t *)value())[idx]);
            case 16:
                return (T)(((const int16_t *)value())[idx]);
            case 32:
                return (T)(((const int32_t *)value())[idx]);
            case 64:
                return (T)(((const int64_t *)value())[idx]);
            default:
                bad_type_error();
            }
            break;
        case halide_type_uint:
            switch (type.bits) {
            case 8:
                return (T)(((const uint8_t *)value())[idx]);
            case 16:
                return (T)(((const uint16_t *)value())[idx]);
            case 32:
                return (T)(((const uint32_t *)value())[idx]);
            case 64:
                return (T)(((const uint64_t *)value())[idx]);
            default:
                bad_type_error();
            }
            break;
        case halide_type_float:
            switch (type.bits) {
            case 32:
                return (T)(((const float *)value())[idx]);
            case 64:
                return (T)(((const double *)value())[idx]);
            default:
                bad_type_error();
            }
            break;
        default:
            bad_type_error();
        }
        return (T)0;
    }

    // Grab a packet from stdin. Returns false when stdin closes.
    bool read_from_stdin() {
        uint32_t header_size = (uint32_t)sizeof(halide_trace_packet_t);
        if (!read_stdin(this, header_size)) {
            return false;
        }
        uint32_t payload_size = size - header_size;
        if (payload_size > (uint32_t)sizeof(payload)) {
            fprintf(stderr, "Payload larger than %d bytes in trace stream (%d)\n", (int)sizeof(payload), (int)payload_size);
            abort();
            return false;
        }
        if (!read_stdin(payload, payload_size)) {
            fprintf(stderr, "Unexpected EOF mid-packet");
            return false;
        }
        return true;
    }

private:
    // Do a blocking read of some number of bytes from stdin.
    bool read_stdin(void *d, ssize_t size) {
        uint8_t *dst = (uint8_t *)d;
        if (!size) return true;
        for (;;) {
            ssize_t s = read(0, dst, size);
            if (s == 0) {
                // EOF
                return false;
            } else if (s < 0) {
                perror("Failed during read");
                exit(-1);
                return 0;
            } else if (s == size) {
                return true;
            }
            size -= s;
            dst += s;
        }
    }

    void bad_type_error() const {
        fprintf(stderr, "Can't visualize packet with type: %d bits: %d\n", type.code, type.bits);
    }
};

// A struct specifying a text label that will appear on the screen at some point.
struct Label {
    const char *text;
    int x, y, n;
};

// A struct specifying how a single Func will get visualized.
struct FuncInfo {

    bool configured = false;

    // Configuration for how the func should be drawn
    struct Config {
        int zoom = 0;
        int load_cost = 0;
        int store_cost = 0;
        int dims = 0;
        int x, y = 0;
        int x_stride[16];
        int y_stride[16];
        int color_dim = 0;
        float min = 0.0f, max = 0.0f;
        vector<Label> labels;
        bool blank_on_end_realization = false;
        uint32_t uninitialized_memory_color = 0xff000000;

        void dump(const char *name) {
            fprintf(stderr,
                    "Func %s:\n"
                    " min: %f max: %f\n"
                    " color_dim: %d\n"
                    " blank: %d\n"
                    " dims: %d\n"
                    " zoom: %d\n"
                    " load cost: %d\n"
                    " store cost: %d\n"
                    " x: %d y: %d\n"
                    " x_stride: %d %d %d %d\n"
                    " y_stride: %d %d %d %d\n",
                    name,
                    min, max,
                    color_dim,
                    blank_on_end_realization,
                    dims,
                    zoom, load_cost, store_cost, x, y,
                    x_stride[0], x_stride[1], x_stride[2], x_stride[3],
                    y_stride[0], y_stride[1], y_stride[2], y_stride[3]);
        }

        Config() {
            memset(x_stride, 0, sizeof(x_stride));
            memset(y_stride, 0, sizeof(y_stride));
        }
    } config;

    // Information about actual observed values gathered while parsing the trace
    struct Observed {
        string qualified_name;
        int first_draw_time = 0, first_packet_idx = 0;
        double min_value = 0.0, max_value = 0.0;
        int min_coord[16];
        int max_coord[16];
        int num_realizations = 0, num_productions = 0;
        uint64_t stores = 0, loads = 0;

        Observed() {
            memset(min_coord, 0, sizeof(min_coord));
            memset(max_coord, 0, sizeof(max_coord));
        }

        void observe_load(const Packet &p) {
            observe_load_or_store(p);
            loads += p.type.lanes;
        }

        void observe_store(const Packet &p) {
            observe_load_or_store(p);
            stores += p.type.lanes;
        }

        void observe_load_or_store(const Packet &p) {
            for (int i = 0; i < std::min(16, p.dimensions / p.type.lanes); i++) {
                for (int lane = 0; lane < p.type.lanes; lane++) {
                    int coord = p.get_coord(i*p.type.lanes + lane);
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
                double value = p.get_value_as<double>(i);
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
            fprintf(stderr,
                    "Func %s:\n"
                    " bounds of domain: ", qualified_name.c_str());
            for (int i = 0; i < 16; i++) {
                if (min_coord[i] == 0 && max_coord[i] == 0) break;
                if (i > 0) {
                    fprintf(stderr, " x ");
                }
                fprintf(stderr, "[%d, %d)", min_coord[i], max_coord[i]);
            }
            // TODO: Convert this file to using std::cerr so I don't
            // have to struggle with cross-platform printf format
            // specifiers. (stores and loads below really shouldn't be a double)
            fprintf(stderr,
                    "\n"
                    " range of values: [%f, %f]\n"
                    " number of realizations: %d\n"
                    " number of productions: %d\n"
                    " number of loads: %g\n"
                    " number of stores: %g\n",
                    min_value, max_value,
                    num_realizations, num_productions,
                    (double)loads,
                    (double)stores);
        }

    } stats;
};

// Composite a single pixel of b over a single pixel of a, writing the result into dst
void composite(uint8_t *a, uint8_t *b, uint8_t *dst) {
    uint8_t alpha = b[3];
    // alpha is almost always 0 or 255.
    if (alpha == 0) {
        ((uint32_t *)dst)[0] = ((uint32_t *)a)[0];
    } else if (alpha == 255) {
        ((uint32_t *)dst)[0] = ((uint32_t *)b)[0];
    } else {
        dst[0] = (alpha * b[0] + (255 - alpha) * a[0]) / 255;
        dst[1] = (alpha * b[1] + (255 - alpha) * a[1]) / 255;
        dst[2] = (alpha * b[2] + (255 - alpha) * a[2]) / 255;
        dst[3] = 255 - (((255 - a[3]) * (255 - alpha)) / 255);
    }
}

#define FONT_W 12
#define FONT_H 32
void draw_text(const char *text, int x, int y, uint32_t color, uint32_t *dst, int dst_width, int dst_height) {
    // The font array contains 96 characters of FONT_W * FONT_H letters.
    assert(inconsolata_raw_len == 96 * FONT_W * FONT_H);

    // Drop any alpha component of color
    color &= 0xffffff;

    for (int c = 0; ; c++) {
        int chr = text[c];
        if (chr == 0) return;

        // We only handle a subset of ascii
        if (chr < 32 || chr > 127) chr = 32;
        chr -= 32;

        uint8_t *font_ptr = inconsolata_raw + chr * (FONT_W * FONT_H);
        for (int fy = 0; fy < FONT_H; fy++) {
            for (int fx = 0; fx < FONT_W; fx++) {
                int px = x + FONT_W*c + fx;
                int py = y - FONT_H + fy + 1;
                if (px < 0 || px >= dst_width ||
                    py < 0 || py >= dst_height) continue;
                dst[py * dst_width + px] = (font_ptr[fy * FONT_W + fx] << 24) | color;
            }
        }
    }
}

void usage() {
    fprintf(stderr,
            R"USAGE(
HalideTraceViz accepts Halide-generated binary tracing packets from
stdin, and outputs them as raw 8-bit rgba32 pixel values to
stdout. You should pipe the output of HalideTraceViz into a video
encoder or player.

E.g. to encode a video:
 HL_TARGET=host-trace_stores-trace_loads-trace_realizations <command to make pipeline> && \
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
     box in the output.

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

 --func name: Mark a Func to be visualized. Uses the currently set
     values of the parameters above to specify how.

 --label func label n: When the named Func is first touched, the label
     appears with its bottom left corner at the current coordinates
     and fades in over n frames.

)USAGE");
}

// If the condition is false, print usage and exit with error.
void expect(bool cond, int i) {
    if (!cond) {
        if (i) {
            fprintf(stderr, "Argument parsing failed at argument %d\n", i);
        }
        usage();
        exit(-1);
    }
}

// See all boxes corresponding to positions in a Func's allocation to
// the given color. Recursive to handle arbitrary
// dimensionalities. Used by begin and end realization events.
void fill_realization(uint32_t *image, int image_width, uint32_t color, const FuncInfo &fi,
                      Packet &p, int current_dimension = 0, int x_off = 0, int y_off = 0) {
    assert(p.dimensions >= 2 * fi.config.dims);
    if (2 * current_dimension == p.dimensions) {
        int x_min = x_off * fi.config.zoom + fi.config.x;
        int y_min = y_off * fi.config.zoom + fi.config.y;
        for (int y = 0; y < fi.config.zoom; y++) {
            for (int x = 0; x < fi.config.zoom; x++) {
                int idx = (y_min + y) * image_width + (x_min + x);
                image[idx] = color;
            }
        }
    } else {
        int min = p.get_coord(current_dimension * 2 + 0);
        int extent = p.get_coord(current_dimension * 2 + 1);
        x_off += fi.config.x_stride[current_dimension] * min;
        y_off += fi.config.y_stride[current_dimension] * min;
        for (int i = min; i < min + extent; i++) {
            fill_realization(image, image_width, color, fi, p, current_dimension + 1, x_off, y_off);
            x_off += fi.config.x_stride[current_dimension];
            y_off += fi.config.y_stride[current_dimension];
        }
    }
}

int run(int argc, char **argv) {
    if (argc == 1) {
        usage();
        return 0;
    }

    // State that determines how different funcs get drawn
    int frame_width = 1920, frame_height = 1080;
    float decay_factor[2] = {1, 2};
    map<string, FuncInfo> func_info;

    int timestep = 10000;
    int hold_frames = 250;

    FuncInfo::Config config;
    config.x = config.y = 0;
    config.zoom = 1;
    config.color_dim = -1;
    config.min = 0;
    config.max = 1;
    config.store_cost = 1;
    config.load_cost = 0;
    config.blank_on_end_realization = false;
    config.dims = 2;
    config.x_stride[0] = 1;
    config.y_stride[0] = 0;
    config.x_stride[1] = 0;
    config.y_stride[1] = 1;
    config.uninitialized_memory_color = 255 << 24;

    vector<pair<int, int>> pos_stack;

    // Parse command line args
    int i = 1;
    while (i < argc) {
        string next = argv[i];
        if (next == "--size") {
            expect(i + 2 < argc, i);
            frame_width = atoi(argv[++i]);
            frame_height = atoi(argv[++i]);
        } else if (next == "--func") {
            expect(i + 1 < argc, i);
            const char *func = argv[++i];
            FuncInfo &fi = func_info[func];
            fi.config.labels.swap(config.labels);
            fi.config = config;
            fi.config.dump(func);
            fi.configured = true;
        } else if (next == "--min") {
            expect(i + 1 < argc, i);
            config.min = atof(argv[++i]);
        } else if (next == "--max") {
            expect(i + 1 < argc, i);
            config.max = atof(argv[++i]);
        } else if (next == "--move") {
            expect(i + 2 < argc, i);
            config.x = atoi(argv[++i]);
            config.y = atoi(argv[++i]);
        } else if (next == "--left") {
            expect(i + 1 < argc, i);
            config.x -= atoi(argv[++i]);
        } else if (next == "--right") {
            expect(i + 1 < argc, i);
            config.x += atoi(argv[++i]);
        } else if (next == "--up") {
            expect(i + 1 < argc, i);
            config.y -= atoi(argv[++i]);
        } else if (next == "--down") {
            expect(i + 1 < argc, i);
            config.y += atoi(argv[++i]);
        } else if (next == "--push") {
            pos_stack.push_back({config.x, config.y});
        } else if (next == "--pop") {
            expect(!pos_stack.empty(), i);
            config.x = pos_stack.back().first;
            config.y = pos_stack.back().second;
            pos_stack.pop_back();
        } else if (next == "--rgb") {
            expect(i + 1 < argc, i);
            config.color_dim = atoi(argv[++i]);
        } else if (next == "--gray") {
            config.color_dim = -1;
        } else if (next == "--blank") {
            config.blank_on_end_realization = true;
        } else if (next == "--no-blank") {
            config.blank_on_end_realization = false;
        } else if (next == "--zoom") {
            expect(i + 1 < argc, i);
            config.zoom = atoi(argv[++i]);
        } else if (next == "--load") {
            expect(i + 1 < argc, i);
            config.load_cost = atoi(argv[++i]);
        } else if (next == "--store") {
            expect(i + 1 < argc, i);
            config.store_cost = atoi(argv[++i]);
        } else if (next == "--strides") {
            config.dims = 0;
            while (i + 1 < argc) {
                const char *next_arg = argv[i + 1];
                if (next_arg[0] == '-' &&
                    next_arg[1] == '-') {
                    break;
                }
                expect(i + 2 < argc, i);
                config.x_stride[config.dims] = atoi(argv[++i]);
                config.y_stride[config.dims] = atoi(argv[++i]);
                config.dims++;
            }
        } else if (next == "--label") {
            expect(i + 3 < argc, i);
            char *func = argv[++i];
            char *text = argv[++i];
            int n = atoi(argv[++i]);
            Label l = {text, config.x, config.y, n};
            func_info[func].config.labels.push_back(l);
        } else if (next == "--timestep") {
            expect(i + 1 < argc, i);
            timestep = atoi(argv[++i]);
        } else if (next == "--decay") {
            expect(i + 2 < argc, i);
            decay_factor[0] = atof(argv[++i]);
            decay_factor[1] = atof(argv[++i]);
        } else if (next == "--hold") {
            expect(i + 1 < argc, i);
            hold_frames = atoi(argv[++i]);
        } else if (next == "--uninit") {
            expect(i + 3 < argc, i);
            int r = atoi(argv[++i]);
            int g = atoi(argv[++i]);
            int b = atoi(argv[++i]);
            config.uninitialized_memory_color = (255 << 24) | ((b & 255) << 16) | ((g & 255) << 8) | (r & 255);
        } else {
            expect(false, i);
        }
        i++;
    }

    // halide_clock counts halide events. video_clock counts how many
    // of these events have been output. When halide_clock gets ahead
    // of video_clock, we emit a new frame.
    size_t halide_clock = 0, video_clock = 0;

    // There are three layers - image data, an animation on top of
    // it, and text labels. These layers get composited.
    uint32_t *image = new uint32_t[frame_width * frame_height];
    memset(image, 0, 4 * frame_width * frame_height);

    uint32_t *anim = new uint32_t[frame_width * frame_height];
    memset(anim, 0, 4 * frame_width * frame_height);

    uint32_t *anim_decay = new uint32_t[frame_width * frame_height];
    memset(anim_decay, 0, 4 * frame_width * frame_height);

    uint32_t *text = new uint32_t[frame_width * frame_height];
    memset(text, 0, 4 * frame_width * frame_height);

    uint32_t *blend = new uint32_t[frame_width * frame_height];
    memset(blend, 0, 4 * frame_width * frame_height);

    struct PipelineInfo {
        string name;
        int32_t id;
    };

    map<uint32_t, PipelineInfo> pipeline_info;

    size_t end_counter = 0;
    size_t packet_clock = 0;
    for (;;) {
        // Hold for some number of frames once the trace has finished.
        if (end_counter) {
            halide_clock += timestep;
            if (end_counter == (size_t)hold_frames) {
                break;
            }
        }

        if (halide_clock >= video_clock) {
            const ssize_t frame_bytes = 4 * frame_width * frame_height;

            while (halide_clock >= video_clock) {
                // Composite text over anim over image
                for (int i = 0; i < frame_width * frame_height; i++) {
                    uint8_t *anim_decay_px  = (uint8_t *)(anim_decay + i);
                    uint8_t *anim_px  = (uint8_t *)(anim + i);
                    uint8_t *image_px = (uint8_t *)(image + i);
                    uint8_t *text_px  = (uint8_t *)(text + i);
                    uint8_t *blend_px = (uint8_t *)(blend + i);
                    // anim over anim_decay
                    composite(anim_decay_px, anim_px, anim_decay_px);
                    // anim_decay over image
                    composite(image_px, anim_decay_px, blend_px);
                    // text over image
                    composite(blend_px, text_px, blend_px);
                }

                // Dump the frame
                ssize_t bytes_written = write(1, blend, frame_bytes);
                if (bytes_written < frame_bytes) {
                    fprintf(stderr, "Could not write frame to stdout.\n");
                    return -1;
                }

                video_clock += timestep;

                // Decay the anim_decay
                if (decay_factor[1] != 1) {
                    const uint32_t inv_d1 = (1 << 24) / decay_factor[1];
                    for (int i = 0; i < frame_width * frame_height; i++) {
                        uint32_t color = anim_decay[i];
                        uint32_t rgb = color & 0x00ffffff;
                        uint32_t alpha = (color >> 24);
                        alpha *= inv_d1;
                        alpha &= 0xff000000;
                        anim_decay[i] = alpha | rgb;
                    }
                }

                // Also decay the anim
                const uint32_t inv_d0 = (1 << 24) / decay_factor[0];
                for (int i = 0; i < frame_width * frame_height; i++) {
                    uint32_t color = anim[i];
                    uint32_t rgb = color & 0x00ffffff;
                    uint32_t alpha = (color >> 24);
                    alpha *= inv_d0;
                    alpha &= 0xff000000;
                    anim[i] = alpha | rgb;
                }
            }

            // Blank anim
            memset(anim, 0, frame_bytes);
        }

        // Read a tracing packet
        Packet p;
        if (!p.read_from_stdin()) {
            end_counter++;
            continue;
        }
        packet_clock++;

        // It's a pipeline begin/end event
        if (p.event == halide_trace_begin_pipeline) {
            pipeline_info[p.id] = {p.func(), p.id};
            continue;
        } else if (p.event == halide_trace_end_pipeline) {
            pipeline_info.erase(p.parent_id);
            continue;
        }

        PipelineInfo pipeline = pipeline_info[p.parent_id];

        if (p.event == halide_trace_begin_realization ||
            p.event == halide_trace_produce ||
            p.event == halide_trace_consume) {
            pipeline_info[p.id] = pipeline;
        } else if (p.event == halide_trace_end_realization ||
                   p.event == halide_trace_end_produce ||
                   p.event == halide_trace_end_consume) {
            pipeline_info.erase(p.parent_id);
        }

        string qualified_name = pipeline.name + ":" + p.func();

        if (func_info.find(qualified_name) == func_info.end()) {
            if (func_info.find(p.func()) != func_info.end()) {
                func_info[qualified_name] = func_info[p.func()];
                func_info.erase(p.func());
            } else {
                fprintf(stderr, "Warning: ignoring func %s event %d    \n", qualified_name.c_str(), p.event);
                fprintf(stderr, "Parent event %d %s\n", p.parent_id, pipeline.name.c_str());
            }
        }

        // Draw the event
        FuncInfo &fi = func_info[qualified_name];
        if (!fi.configured) continue;

        if (fi.stats.first_draw_time == 0) {
            fi.stats.first_draw_time = halide_clock;
        }

        if (fi.stats.first_packet_idx == 0) {
            fi.stats.first_packet_idx = packet_clock;
            fi.stats.qualified_name = qualified_name;
        }

        int frames_since_first_draw = (halide_clock - fi.stats.first_draw_time) / timestep;

        for (size_t i = 0; i < fi.config.labels.size(); i++) {
            const Label &label = fi.config.labels[i];
            if (frames_since_first_draw <= label.n) {
                uint32_t color = ((1 + frames_since_first_draw) * 255) / label.n;
                if (color > 255) color = 255;
                color *= 0x10101;

                draw_text(label.text, label.x, label.y, color, text, frame_width, frame_height);
            }
        }

        switch (p.event) {
        case halide_trace_load:
        case halide_trace_store:
        {
            if (p.event == halide_trace_store) {
                // Stores take time proportional to the number of
                // items stored times the cost of the func.
                halide_clock += fi.config.store_cost * p.type.lanes;
                fi.stats.observe_store(p);
            } else {
                halide_clock += fi.config.load_cost * p.type.lanes;
                fi.stats.observe_load(p);
            }

            // Check the tracing packet contained enough information
            // given the number of dimensions the user claims this
            // Func has.
            assert(p.dimensions >= p.type.lanes * fi.config.dims);
            if (p.dimensions >= p.type.lanes * fi.config.dims) {
                for (int lane = 0; lane < p.type.lanes; lane++) {
                    // Compute the screen-space x, y coord to draw this.
                    int x = fi.config.x;
                    int y = fi.config.y;
                    for (int d = 0; d < fi.config.dims; d++) {
                        int a = p.get_coord(d * p.type.lanes + lane);
                        x += fi.config.zoom * fi.config.x_stride[d] * a;
                        y += fi.config.zoom * fi.config.y_stride[d] * a;
                    }

                    // Stores are orange, loads are blue.
                    uint32_t color = p.event == halide_trace_load ? 0xffffdd44 : 0xff44ddff;

                    uint32_t image_color;
                    bool update_image = false;

                    // Update one or more of the color channels of the
                    // image layer in case it's a store or a load from
                    // the input.
                    if (p.event == halide_trace_store ||
                        fi.stats.num_realizations == 0 /* load from an input */) {
                        update_image = true;
                        // Get the old color, in case we're only
                        // updating one of the color channels.
                        image_color = image[frame_width * y + x];

                        double value = p.get_value_as<double>(lane);

                        // Normalize it.
                        value = 255 * (value - fi.config.min) / (fi.config.max - fi.config.min);
                        if (value < 0) value = 0;
                        if (value > 255) value = 255;

                        // Convert to 8-bit color.
                        uint8_t int_value = (uint8_t)value;

                        if (fi.config.color_dim < 0) {
                            // Grayscale
                            image_color = (int_value * 0x00010101) | 0xff000000;
                        } else {
                            // Color
                            uint32_t channel = p.get_coord(fi.config.color_dim * p.type.lanes + lane);
                            uint32_t mask = ~(255 << (channel * 8));
                            image_color &= mask;
                            image_color |= int_value << (channel * 8);
                        }
                    }

                    // Draw the pixel
                    for (int dy = 0; dy < fi.config.zoom; dy++) {
                        for (int dx = 0; dx < fi.config.zoom; dx++) {
                            if (y + dy >= 0 && y + dy < frame_height &&
                                x + dx >= 0 && x + dx < frame_width) {
                                int px = frame_width * (y + dy) + x + dx;
                                anim[px] = color;
                                if (update_image) {
                                    image[px] = image_color;
                                }
                            }
                        }
                    }
                }
            }
            break;
        }
        case halide_trace_begin_realization:
            fi.stats.num_realizations++;
            fill_realization(image, frame_width, fi.config.uninitialized_memory_color, fi, p);
            break;
        case halide_trace_end_realization:
            if (fi.config.blank_on_end_realization) {
                fill_realization(image, frame_width, 0, fi, p);
            }
            break;
        case halide_trace_produce:
            fi.stats.num_productions++;
            break;
        case halide_trace_end_produce:
        case halide_trace_consume:
        case halide_trace_end_consume:
        case halide_trace_begin_pipeline:
        case halide_trace_end_pipeline:
            break;
        default:
            fprintf(stderr, "Unknown tracing event code: %d\n", p.event);
            exit(-1);
        }

    }

    fprintf(stderr, "Total number of Funcs: %d\n", (int)func_info.size());

    // Print stats about the Func gleaned from the trace.
    vector<std::pair<std::string, FuncInfo> > funcs;
    for (std::pair<std::string, FuncInfo> p : func_info) {
        funcs.push_back(p);
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

    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    run(argc, argv);
}
