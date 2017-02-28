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
    // Configuration for how the func should be drawn
    struct Config {
        int zoom = 0;
        int cost = 0;
        int dims = 0;
        int x, y = 0;
        int x_stride[16];
        int y_stride[16];
        int color_dim = 0;
        float min = 0.0f, max = 0.0f;
        vector<Label> labels;
        bool blank_on_end_realization = false;

        void dump(const char *name) {
            fprintf(stderr,
                    "Func %s:\n"
                    " min: %f max: %f\n"
                    " color_dim: %d\n"
                    " blank: %d\n"
                    " dims: %d\n"
                    " zoom: %d\n"
                    " cost: %d\n"
                    " x: %d y: %d\n"
                    " x_stride: %d %d %d %d\n"
                    " y_stride: %d %d %d %d\n",
                    name,
                    min, max,
                    color_dim,
                    blank_on_end_realization,
                    dims,
                    zoom, cost, x, y,
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
    dst[0] = (alpha * b[0] + (256 - alpha) * a[0]) >> 8;
    dst[1] = (alpha * b[1] + (256 - alpha) * a[1]) >> 8;
    dst[2] = (alpha * b[2] + (256 - alpha) * a[2]) >> 8;
    dst[3] = 0xff;
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
            "\n"
            "HalideTraceViz accepts Halide-generated binary tracing packets from\n"
            "stdin, and outputs them as raw 8-bit rgba32 pixel values to\n"
            "stdout. You should pipe the output of HalideTraceViz into a video\n"
            "encoder or player.\n"
            "\n"
            "E.g. to encode a video:\n"
            " HL_TRACE=3 <command to make pipeline> && \\\n"
            " HL_TRACE_FILE=/dev/stdout <command to run pipeline> | \\\n"
            " HalideTraceViz -s 1920 1080 -t 10000 <the -f args> | \\\n"
            " avconv -f rawvideo -pix_fmt bgr32 -s 1920x1080 -i /dev/stdin -c:v h264 output.avi\n"
            "\n"
            "To just watch the trace instead of encoding a video replace the last\n"
            "line with something like:\n"
            " mplayer -demuxer rawvideo -rawvideo w=1920:h=1080:format=rgba:fps=30 -idle -fixed-vo -\n"
            "\n"
            "The arguments to HalideTraceViz are: \n"
            " -s width height: The size of the output frames. Defaults to 1920 x 1080.\n"
            "\n"
            " -t timestep: How many Halide computations should be covered by each\n"
            "    frame. Defaults to 10000.\n"
            " -d decay factor: How quickly should the yellow and blue highlights \n"
            "    decay over time\n"
            " -h hold frames: How many frames to output after the end of the trace. \n"
            "    Defaults to 250.\n"
            " -l func label x y n: When func is first touched, the label appears at\n"
            "    the given coordinates and fades in over n frames.\n"
            "\n"
            " For each Func you want to visualize, also specify:\n"
            " -f func_name min_value max_value color_dim blank zoom cost x y strides\n"
            " where\n"
            "  func_name: The name of the func or input image. If you have multiple \n"
            "    pipelines that use Funcs or the same name, you can optionally \n"
            "    prefix this with the name of the containing pipeline like so: \n"
            "    pipeline_name:func_name\n"
            "\n"
            "  min_value: The minimum value taken on by the Func. Values less than\n"
            "    or equal to this will map to black\n"
            "\n"
            "  max_value: The maximum value taken on by the Func. Values greater\n"
            "    than or equal to this will map to white\n"
            "\n"
            "  color_dim: Which dimension of the Func corresponds to color\n"
            "    channels. Usually 2. Set it to -1 if you want to visualize the Func\n"
            "    as grayscale\n"
            "\n"
            "  blank: Should the output occupied by the Func be set to black on end\n"
            "    realization events. Zero for no, one for yes.\n"
            "\n"
            "  zoom: Each value of the Func will draw as a zoom x zoom box in the output\n"
            "\n"
            "  cost: How much time in the output video should storing one value to\n"
            "    this Func take. Relative to the timestep.\n"
            "\n"
            "  x, y: The position on the screen corresponding to the Func's 0, 0\n"
            "    coordinate.\n"
            "\n"
            "  strides: A matrix that maps the coordinates of the Func to screen\n"
            "    pixels. Specified column major. For example, 1 0 0 1 0 0\n"
            "    specifies that the Func has three dimensions where the\n"
            "    first one maps to screen-space x coordinates, the second\n"
            "    one maps to screen-space y coordinates, and the third one\n"
            "    does not affect screen-space coordinates.\n"
        );

}

int run(int argc, char **argv) {
    // State that determines how different funcs get drawn
    int frame_width = 1920, frame_height = 1080;
    int decay_factor = 2;
    map<string, FuncInfo> func_info;

    int timestep = 10000;
    int hold_frames = 250;

    // Parse command line args
    int i = 1;
    while (i < argc) {
        string next = argv[i];
        if (next == "-s") {
            if (i + 2 >= argc) {
                usage();
                return -1;
            }
            frame_width = atoi(argv[++i]);
            frame_height = atoi(argv[++i]);
        } else if (next == "-f") {
            if (i + 9 >= argc) {
                usage();
                return -1;
            }
            char *func = argv[++i];
            FuncInfo &fi = func_info[func];
            fi.config.min = atof(argv[++i]);
            fi.config.max = atof(argv[++i]);
            fi.config.color_dim = atoi(argv[++i]);
            fi.config.blank_on_end_realization = atoi(argv[++i]);
            fi.config.zoom = atoi(argv[++i]);
            fi.config.cost = atoi(argv[++i]);
            fi.config.x = atoi(argv[++i]);
            fi.config.y = atoi(argv[++i]);
            int d = 0;
            for (; i+1 < argc && argv[i+1][0] != '-' && d < 16; d++) {
                fi.config.x_stride[d] = atoi(argv[++i]);
                fi.config.y_stride[d] = atoi(argv[++i]);
            }
            fi.config.dims = d;
            fi.config.dump(func);

        } else if (next == "-l") {
            if (i + 5 >= argc) {
                usage();
                return -1;
            }
            char *func = argv[++i];
            char *text = argv[++i];
            int x = atoi(argv[++i]);
            int y = atoi(argv[++i]);
            int n = atoi(argv[++i]);
            Label l = {text, x, y, n};
            fprintf(stderr, "Adding label %s to func %s\n",
                    text, func);
            func_info[func].config.labels.push_back(l);
        } else if (next == "-t") {
            if (i + 1 >= argc) {
                usage();
                return -1;
            }
            assert(i + 1 < argc);
            timestep = atoi(argv[++i]);
        } else if (next == "-d") {
            if (i + 1 >= argc) {
                usage();
                return -1;
            }
            assert(i + 1 < argc);
            decay_factor = atoi(argv[++i]);
        } else if (next == "-h") {
            if (i + 1 >= argc) {
                usage();
                return -1;
            }
            assert(i + 1 < argc);
            hold_frames = atoi(argv[++i]);
        } else {
            usage();
            return -1;
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

        while (halide_clock >= video_clock) {
            // Composite text over anim over image
            for (int i = 0; i < frame_width * frame_height; i++) {
                uint8_t *anim_px  = (uint8_t *)(anim + i);
                uint8_t *image_px = (uint8_t *)(image + i);
                uint8_t *text_px  = (uint8_t *)(text + i);
                uint8_t *blend_px = (uint8_t *)(blend + i);
                composite(image_px, anim_px, blend_px);
                composite(blend_px, text_px, blend_px);
            }

            // Dump the frame
            ssize_t bytes = 4 * frame_width * frame_height;
            ssize_t bytes_written = write(1, blend, bytes);
            if (bytes_written < bytes) {
                fprintf(stderr, "Could not write frame to stdout.\n");
                return -1;
            }

            video_clock += timestep;

            // Decay the alpha channel on the anim
            for (int i = 0; i < frame_width * frame_height; i++) {
                uint32_t color = anim[i];
                uint32_t rgb = color & 0x00ffffff;
                uint8_t alpha = (color >> 24);
                alpha /= decay_factor;
                anim[i] = (alpha << 24) | rgb;
            }
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

        string qualified_name = pipeline.name + ":" + p.func();

        if (func_info.find(qualified_name) == func_info.end()) {
            if (func_info.find(p.func()) != func_info.end()) {
                func_info[qualified_name] = func_info[p.func()];
                func_info.erase(p.func());
            } else {
                fprintf(stderr, "Warning: ignoring func %s event %d    \n", qualified_name.c_str(), p.event);
            }
        }

        // Draw the event
        FuncInfo &fi = func_info[qualified_name];

        if (fi.stats.first_draw_time == 0) {
            fi.stats.first_draw_time = halide_clock;
        }

        if (fi.stats.first_packet_idx == 0) {
            fi.stats.first_packet_idx = packet_clock;
            fi.stats.qualified_name = qualified_name;
        }

        switch (p.event) {
        case halide_trace_load:
        case halide_trace_store:
        {
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

            if (p.event == halide_trace_store) {
                // Stores take time proportional to the number of
                // items stored times the cost of the func.
                halide_clock += fi.config.cost * p.type.lanes;

                fi.stats.observe_store(p);
            } else {
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
            pipeline_info[p.id] = pipeline;
            break;
        case halide_trace_end_realization:
            if (fi.config.blank_on_end_realization) {
                assert(p.dimensions >= 2 * fi.config.dims);
                int x_min = fi.config.x, y_min = fi.config.y;
                int x_extent = 0, y_extent = 0;
                for (int d = 0; d < fi.config.dims; d++) {
                    int m = p.get_coord(d * 2 + 0);
                    int e = p.get_coord(d * 2 + 1);
                    x_min += fi.config.zoom * fi.config.x_stride[d] * m;
                    y_min += fi.config.zoom * fi.config.y_stride[d] * m;
                    x_extent += fi.config.zoom * fi.config.x_stride[d] * e;
                    y_extent += fi.config.zoom * fi.config.y_stride[d] * e;
                }
                if (x_extent == 0) x_extent = fi.config.zoom;
                if (y_extent == 0) y_extent = fi.config.zoom;
                for (int y = y_min; y < y_min + y_extent; y++) {
                    for (int x = x_min; x < x_min + x_extent; x++) {
                        image[y * frame_width + x] = 0;
                    }
                }
            }
            pipeline_info.erase(p.parent_id);
            break;
        case halide_trace_produce:
            pipeline_info[p.id] = pipeline;
            fi.stats.num_productions++;
            break;
        case halide_trace_end_produce:
            pipeline_info.erase(p.parent_id);
            break;
        case halide_trace_consume:
            pipeline_info[p.id] = pipeline;
            break;
        case halide_trace_end_consume:
            pipeline_info.erase(p.parent_id);
            break;
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
