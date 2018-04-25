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
#include <string>
#include <list>
#include <set>

#include "inconsolata.h"
#include "HalideRuntime.h"
#include "HalideTraceUtils.h"

#include "halide_trace_config.h"

using namespace Halide;
using namespace Halide::Trace;
using namespace Internal;

namespace {

using std::map;
using std::vector;
using std::string;
using std::queue;
using std::array;
using std::pair;
using std::list;
using std::set;

// A struct specifying how a single Func will get visualized.
struct FuncInfo {

    bool configured = false;

    // Configuration for how the func should be drawn
    FuncConfig config;

    // Information about actual observed values gathered while parsing the trace
    struct Observed {
        string qualified_name;
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
            std::cerr <<
                    "Func " << qualified_name << ":\n";
            for (int i = 0; i < 16; i++) {
                if (min_coord[i] == 0 && max_coord[i] == 0) {
                    break;
                }
                if (i > 0) {
                    std::cerr << " x ";
                }
                std::cerr << "[" << min_coord[i] << ", " << max_coord[i] << ")";
            }
            std::cerr <<
                    "\n"
                    " range of values: [" << min_value << ", " << max_value << "]\n"
                    " number of realizations: " << num_realizations << "\n"
                    " number of productions: " << num_productions << "\n"
                    " number of loads: " << loads << "\n"
                    " number of stores: " << stores << "\n";
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

static constexpr int FONT_W = 12;
static constexpr int FONT_H = 32;

void draw_text(const std::string &text, const Point &pos, uint32_t color, uint32_t *dst, const Point &dst_size) {
    // The font array contains 96 characters of FONT_W * FONT_H letters.
    assert(inconsolata_raw_len == 96 * FONT_W * FONT_H);

    // Drop any alpha component of color
    color &= 0xffffff;

    int c = -1;
    for (int chr : text) {
        ++c;

        // We only handle a subset of ascii
        if (chr < 32 || chr > 127) {
            chr = 32;
        }
        chr -= 32;

        uint8_t *font_ptr = inconsolata_raw + chr * (FONT_W * FONT_H);
        for (int fy = 0; fy < FONT_H; fy++) {
            for (int fx = 0; fx < FONT_W; fx++) {
                int px = pos.x + FONT_W*c + fx;
                int py = pos.y - FONT_H + fy + 1;
                if (px < 0 || px >= dst_size.x ||
                    py < 0 || py >= dst_size.y) continue;
                dst[py * dst_size.x + px] = (font_ptr[fy * FONT_W + fx] << 24) | color;
            }
        }
    }
}

void usage() {
    std::cerr <<
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

 --func name: Mark a Func to be visualized. Uses the currently set
     values of the parameters above to specify how.

 --label func label n: When the named Func is first touched, the label
     appears with its bottom left corner at the current coordinates
     and fades in over n frames.

 --rlabel func label dx dy n: Like "--label", but relative to the Func's
     position, using dx and dy as an offset.

))USAGE";
}

// If the condition is false, print usage and exit with error.
void expect(bool cond, int i) {
    if (!cond) {
        if (i) {
            std::cerr << "Argument parsing failed at argument " << i << "\n";
        }
        usage();
        exit(-1);
    }
}

// Set all boxes corresponding to positions in a Func's allocation to
// the given color. Recursive to handle arbitrary
// dimensionalities. Used by begin and end realization events.
void fill_realization(uint32_t *image, const Point &image_size, uint32_t color,
                      const FuncInfo &fi, const Packet &p,
                      int current_dimension = 0, int x_off = 0, int y_off = 0) {
    if (2 * current_dimension == p.dimensions) {
        int x_min = x_off * fi.config.zoom + fi.config.pos.x;
        int y_min = y_off * fi.config.zoom + fi.config.pos.y;
        for (int y = 0; y < fi.config.zoom; y++) {
            if (y_min + y < 0 || y_min + y >= image_size.y) continue;
            for (int x = 0; x < fi.config.zoom; x++) {
                if (x_min + x < 0 || x_min + x >= image_size.x) continue;
                int idx = (y_min + y) * image_size.x + (x_min + x);
                image[idx] = color;
            }
        }
    } else {
        int min = p.get_coord(current_dimension * 2 + 0);
        int extent = p.get_coord(current_dimension * 2 + 1);
        // If we don't have enough strides, assume subsequent dimensions have stride (0, 0)
        const Point pt = current_dimension < fi.config.strides.size() ? fi.config.strides[current_dimension] : Point{0, 0};
        x_off += pt.x * min;
        y_off += pt.y * min;
        for (int i = min; i < min + extent; i++) {
            fill_realization(image, image_size, color, fi, p,
                current_dimension + 1, x_off, y_off);
            x_off += pt.x;
            y_off += pt.y;
        }
    }
}

int parse_int(const char *str) {
    char *endptr = nullptr;
    errno = 0;
    long result = strtol(str, &endptr, 0);
    if (errno == ERANGE || str == endptr) {
        std::cerr << "Unable to parse '" << str << "' as an int\n";
        usage();
        exit(-1);
    }
    return (int) result;
}

float parse_float(const char *str) {
    char *endptr = nullptr;
    errno = 0;
    float result = strtof(str, &endptr);
    if (errno == ERANGE || str == endptr) {
        std::cerr << "Unable to parse '" << str << "' as a float\n";
        usage();
        exit(-1);
    }
    return result;
}

double parse_double(const char *str) {
    char *endptr = nullptr;
    errno = 0;
    double result = strtod(str, &endptr);
    if (errno == ERANGE || str == endptr) {
        std::cerr << "Unable to parse '" << str << "' as a double\n";
        usage();
        exit(-1);
    }
    return result;
}

void do_decay(int decay_factor, std::vector<uint32_t> &storage) {
    if (decay_factor != 1) {
        const uint32_t inv_d1 = (1 << 24) / std::max(1, decay_factor);
        for (size_t i = 0; i < storage.size(); i++) {
            uint32_t color = storage[i];
            uint32_t rgb = color & 0x00ffffff;
            uint32_t alpha = (color >> 24);
            alpha *= inv_d1;
            alpha &= 0xff000000;
            storage[i] = alpha | rgb;
        }
    }
}

// Given a FuncConfig, check each field for "use some reasonable default"
// value and fill in something reasonable.
FuncConfig fix_func_config_defaults(const FuncConfig &cfg) {
    // Make a FuncConfig with 'safe' defaults for everything,
    // then merge the existing cfg into it.
    FuncConfig safe;
    safe.zoom = 1.f;
    safe.load_cost = 0;
    safe.store_cost = 1;
    safe.pos = {0, 0};
    safe.strides = { {1, 0}, {0, 1} };
    safe.color_dim = -1;
    safe.min = 0.0;
    safe.max = 1.0;
    safe.labels = {};
    safe.blank_on_end_realization = 0;
    safe.uninitialized_memory_color = 0x00000000;
    safe.merge_from(cfg);
    safe.uninitialized_memory_color |= 0xff000000;
    return safe;
}

void process_args(int argc, char **argv, GlobalConfig &global, map<string, FuncInfo> &func_info) {
    // The struct's default values are what we want
    FuncConfig config;
    vector<Point> pos_stack;
    set<string> labels_seen;

    // Parse command line args
    int i = 1;
    while (i < argc) {
        string next = argv[i];
        if (next == "--size") {
            expect(i + 2 < argc, i);
            global.frame_size.x = parse_int(argv[++i]);
            global.frame_size.y = parse_int(argv[++i]);
        } else if (next == "--func") {
            expect(i + 1 < argc, i);
            const char *func = argv[++i];
            FuncInfo &fi = func_info[func];
            fi.config.merge_from(config);
            fi.configured = true;
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
                config.strides.push_back({x, y});
            }
        } else if (next == "--label") {
            expect(i + 3 < argc, i);
            char *func = argv[++i];
            char *text = argv[++i];
            int n = parse_int(argv[++i]);
            FuncInfo &fi = func_info[func];
            // A Label's position is relative to its Func's position;
            // the --label flag has always expected an absolute position,
            // so convert it to an offset.
            Point offset = { config.pos.x - fi.config.pos.x, config.pos.y - fi.config.pos.y };
            if (!labels_seen.count(func)) {
                // If there is at least one --label specified for a Func,
                // it overrides the entire previous set of labels, rather
                // than simply appending.
                fi.config.labels.clear();
                labels_seen.insert(func);
            }
            fi.config.labels.push_back({text, offset, n});
        } else if (next == "--rlabel") {
            expect(i + 5 < argc, i);
            char *func = argv[++i];
            char *text = argv[++i];
            int dx = parse_int(argv[++i]);
            int dy = parse_int(argv[++i]);
            int n = parse_int(argv[++i]);
            FuncInfo &fi = func_info[func];
            Point offset = { dx, dy };
            if (!labels_seen.count(func)) {
                // If there is at least one --label specified for a Func,
                // it overrides the entire previous set of labels, rather
                // than simply appending.
                fi.config.labels.clear();
                labels_seen.insert(func);
            }
            fi.config.labels.push_back({text, offset, n});
        } else if (next == "--timestep") {
            expect(i + 1 < argc, i);
            global.timestep = parse_int(argv[++i]);
        } else if (next == "--decay") {
            expect(i + 2 < argc, i);
            global.decay_factor_during_compute = parse_int(argv[++i]);
            global.decay_factor_after_compute = parse_int(argv[++i]);
        } else if (next == "--hold") {
            expect(i + 1 < argc, i);
            global.hold_frames = parse_int(argv[++i]);
        } else if (next == "--uninit") {
            expect(i + 3 < argc, i);
            int r = parse_int(argv[++i]);
            int g = parse_int(argv[++i]);
            int b = parse_int(argv[++i]);
            config.uninitialized_memory_color = ((b & 255) << 16) | ((g & 255) << 8) | (r & 255);
        } else {
            expect(false, i);
        }
        i++;
    }
}

int run(int argc, char **argv) {
    if (argc == 1) {
        usage();
        return 0;
    }

    // State that determines how different funcs get drawn
    GlobalConfig global;
    map<string, FuncInfo> func_info;

    // halide_clock counts halide events. video_clock counts how many
    // of these events have been output. When halide_clock gets ahead
    // of video_clock, we emit a new frame.
    size_t halide_clock = 0, video_clock = 0;
    bool all_args_final = false;
    bool seen_global_config_tag = false;

    // There are three layers - image data, an animation on top of
    // it, and text labels. These layers get composited.
    struct Buffers {
        std::vector<uint32_t> image, anim, anim_decay, text, blend;

        void resize(const Point &frame_size) {
            const int frame_elems = frame_size.x * frame_size.y;
            image.resize(frame_elems, 0);
            anim.resize(frame_elems, 0);
            anim_decay.resize(frame_elems, 0);
            text.resize(frame_elems, 0);
            blend.resize(frame_elems, 0);
        }
    } buffers;

    // Leave buffers unallocated for now;
    // we'll allocate once all tags and flags are processed

    struct PipelineInfo {
        string name;
        int32_t id;
    };

    map<uint32_t, PipelineInfo> pipeline_info;

    list<pair<Label, int>> labels_being_drawn;
    size_t end_counter = 0;
    size_t packet_clock = 0;
    for (;;) {
        // Hold for some number of frames once the trace has finished.
        if (end_counter) {
            halide_clock += global.timestep;
            if (end_counter >= (size_t) global.hold_frames) {
                break;
            }
        }

        if (halide_clock > video_clock) {
            assert(all_args_final);
            const ssize_t frame_bytes = buffers.image.size() * sizeof(uint32_t);

            while (halide_clock > video_clock) {
                // Composite text over anim over image
                for (int i = 0; i < buffers.image.size(); i++) {
                    uint8_t *anim_decay_px  = (uint8_t *)(buffers.anim_decay.data() + i);
                    uint8_t *anim_px  = (uint8_t *)(buffers.anim.data() + i);
                    uint8_t *image_px = (uint8_t *)(buffers.image.data() + i);
                    uint8_t *text_px  = (uint8_t *)(buffers.text.data() + i);
                    uint8_t *blend_px = (uint8_t *)(buffers.blend.data() + i);
                    // anim over anim_decay
                    composite(anim_decay_px, anim_px, anim_decay_px);
                    // anim_decay over image
                    composite(image_px, anim_decay_px, blend_px);
                    // text over image
                    composite(blend_px, text_px, blend_px);
                }

                // Dump the frame
                ssize_t bytes_written = write(1, buffers.blend.data(), frame_bytes);
                if (bytes_written < frame_bytes) {
                    std::cerr << "Could not write frame to stdout.\n";
                    return -1;
                }

                video_clock += global.timestep;

                // Decay the anim_decay
                do_decay(global.decay_factor_after_compute, buffers.anim_decay);

                // Also decay the anim
                do_decay(global.decay_factor_during_compute, buffers.anim);
            }

            // Blank anim
            std::fill(buffers.anim.begin(), buffers.anim.end(), 0);
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
            assert(pipeline_info.count(p.parent_id));
            pipeline_info.erase(p.parent_id);
            continue;
        } else if (p.event == halide_trace_tag) {
            // If there are trace tags, they will come immediately after the pipeline's
            // halide_trace_begin_pipeline but before any realizations.
            if (halide_clock != 0 || video_clock != 0) {
                // Messing with timestamp, framesize, etc partway thru
                // a visualization would be bad.
                // TODO: May need to check parent_id here, as nested
                // pipelines called via define_extern could emit these.
                std::cerr << "trace_tags are only expected at the start of a visualization.\n";
                exit(1);
            }
            if (FuncConfig::match(p.trace_tag())) {
                FuncConfig cfg(p.trace_tag());
                func_info[p.func()].config = cfg;
                func_info[p.func()].configured = true;
            } else if (GlobalConfig::match(p.trace_tag())) {
                if (seen_global_config_tag) {
                    std::cerr << "Warning, saw multiple GlobalConfig trace_tags, some will be ignored.\n";
                }
                global = GlobalConfig(p.trace_tag());
                seen_global_config_tag = true;
            } else {
                std::cerr << "Ignoring trace_tag: (" << p.trace_tag() << ")\n";
            }
            continue;
        }

        if (!all_args_final) {
            // We wait until now to process the cmd-line args;
            // this allows us to override trace-tag specifications
            // via the commandline, which is handy for experimentations.
            process_args(argc, argv, global, func_info);

            // Ensure that all FuncConfigs have reasonable values.
            for (auto &p : func_info) {
                p.second.config = fix_func_config_defaults(p.second.config);
            }

            // allocate the buffers after all tags and flags are processed
            buffers.resize(global.frame_size);

            // dump after any tags are handled
            global.dump(std::cerr);
            for (const auto &p : func_info) {
                const auto &fi = p.second;
                if (fi.configured) {
                    fi.config.dump(std::cerr, p.first);
                }
            }
            all_args_final = true;
        }

        PipelineInfo pipeline = pipeline_info[p.parent_id];

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

        string qualified_name = pipeline.name + ":" + p.func();

        if (func_info.find(qualified_name) == func_info.end()) {
            if (func_info.find(p.func()) != func_info.end()) {
                func_info[qualified_name] = func_info[p.func()];
                func_info.erase(p.func());
            } else {
                std::cerr << "Warning: ignoring func " << qualified_name << " event " << p.event << "\n";
                std::cerr << "Parent event " << p.parent_id << " " << pipeline.name << "\n";
            }
        }

        // Draw the event
        FuncInfo &fi = func_info[qualified_name];
        if (!fi.configured) continue;

        if (fi.stats.first_draw_time < 0) {
            fi.stats.first_draw_time = halide_clock;
            for (const auto &label : fi.config.labels) {
                // Convert offset to absolute position before enqueuing
                Label l = label;
                l.pos.x += fi.config.pos.x;
                l.pos.y += fi.config.pos.y;
                labels_being_drawn.push_back({l, halide_clock});
            }
        }

        if (fi.stats.first_packet_idx < 0) {
            fi.stats.first_packet_idx = packet_clock;
            fi.stats.qualified_name = qualified_name;
        }

        for (auto it = labels_being_drawn.begin(); it != labels_being_drawn.end(); ) {
            const Label &label = it->first;
            int first_draw_clock = it->second;
            int frames_since_first_draw = (halide_clock - first_draw_clock) / global.timestep;
            if (frames_since_first_draw < label.fade_in_frames) {
                uint32_t color = ((1 + frames_since_first_draw) * 255) / std::max(1, label.fade_in_frames);
                if (color > 255) color = 255;
                color *= 0x10101;
                draw_text(label.text, label.pos, color, buffers.text.data(), global.frame_size);
                ++it;
            } else {
                // Once we reach or exceed the final frame, draw at 100% opacity, then remove
                draw_text(label.text, label.pos, 0xffffff, buffers.text.data(), global.frame_size);
                it = labels_being_drawn.erase(it);
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

            // zero- or one-dimensional Funcs can have dimensions < strides.size().
            // This may seem confusing, so keep in mind:
            // fi.config.strides are provided by the --stride flag, so it can contain anything; i
            // if you don't specify them at all, they default to {{1,0},{0,1} (aka size=2).
            // So if we have excess strides, just ignore them.
            const int dims = std::min(p.dimensions/p.type.lanes, (int) fi.config.strides.size());
            for (int lane = 0; lane < p.type.lanes; lane++) {
                // Compute the screen-space x, y coord to draw this.
                int x = fi.config.pos.x;
                int y = fi.config.pos.y;
                const float z = fi.config.zoom;
                for (int d = 0; d < dims; d++) {
                    const int coord = d * p.type.lanes + lane;
                    assert(coord < p.dimensions);
                    const int a = p.get_coord(coord);
                    const auto &stride = fi.config.strides[d];
                    x += z * stride.x * a;
                    y += z * stride.y * a;
                }

                // The box to draw must be entirely on-screen
                if (y < 0 || y >= global.frame_size.y ||
                    x < 0 || x >= global.frame_size.x ||
                    y + z - 1 < 0 || y + z - 1 >= global.frame_size.y ||
                    x + z - 1 < 0 || x + z - 1 >= global.frame_size.x) {
                    continue;
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
                    image_color = buffers.image[global.frame_size.x * y + x];

                    double value = p.get_value_as<double>(lane);

                    // Normalize it.
                    value = std::max(0.0, std::min(255.0, 255.0 * (value - fi.config.min) / (fi.config.max - fi.config.min)));

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
                        int px = global.frame_size.x * (y + dy) + x + dx;
                        buffers.anim[px] = color;
                        if (update_image) {
                            buffers.image[px] = image_color;
                        }
                    }
                }
            }
            break;
        }
        case halide_trace_begin_realization:
            fi.stats.num_realizations++;
            fill_realization(buffers.image.data(), global.frame_size, fi.config.uninitialized_memory_color, fi, p);
            break;
        case halide_trace_end_realization:
            if (fi.config.blank_on_end_realization > 0) {
                fill_realization(buffers.image.data(), global.frame_size, 0, fi, p);
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
            std::cerr << "Unknown tracing event code: " << p.event << "\n";
            exit(-1);
        }

    }

    std::cerr << "Total number of Funcs: " << func_info.size() << "\n";

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
