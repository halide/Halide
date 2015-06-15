#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <map>
#include <vector>
#include <string>
#include <queue>
#include <iostream>
#include <unistd.h>
#include <string.h>

#include "inconsolata.h"

namespace {

using std::map;
using std::vector;
using std::string;
using std::queue;

// A struct representing a single Halide tracing packet.
struct Packet {
    // The first 32 bytes are metadata
    uint32_t id, parent;
    uint8_t event, type, bits, width, value_idx, num_int_args;
    char name[17];
    uint8_t payload[4096-32]; // Not all of this will be used, but this is the max possible packet size.

    size_t value_bytes() const {
        size_t bytes_per_elem = 1;
        while (bytes_per_elem*8 < bits) bytes_per_elem <<= 1;
        return bytes_per_elem * width;
    }

    size_t int_args_bytes() const {
        return sizeof(int) * num_int_args;
    }

    size_t payload_bytes() const {
        return value_bytes() + int_args_bytes();
    }

    int get_int_arg(int idx) const {
        return ((int *)(payload + value_bytes()))[idx];
    }

    template<typename T>
    T get_value_as(int idx) const {
        switch (type) {
        case 0: // int
            switch (bits) {
            case 8:
                return (T)(((int8_t *)payload)[idx]);
            case 16:
                return (T)(((int16_t *)payload)[idx]);
            case 32:
                return (T)(((int32_t *)payload)[idx]);
            case 64:
                return (T)(((int64_t *)payload)[idx]);
            default:
                bad_type_error();
            }
            break;
        case 1: // uint
            switch (bits) {
            case 8:
                return (T)(((uint8_t *)payload)[idx]);
            case 16:
                return (T)(((uint16_t *)payload)[idx]);
            case 32:
                return (T)(((uint32_t *)payload)[idx]);
            case 64:
                return (T)(((uint64_t *)payload)[idx]);
            default:
                bad_type_error();
            }
            break;
        case 2: // float
            switch (bits) {
            case 32:
                return (T)(((float *)payload)[idx]);
            case 64:
                return (T)(((double *)payload)[idx]);
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
        if (!read_stdin(this, 32)) {
            return false;
        }
        assert(read_stdin(payload, payload_bytes()) &&
               "Unexpected EOF mid-packet");
        return true;
    }

private:
    // Do a blocking read of some number of bytes from stdin.
    bool read_stdin(void *d, ssize_t size) {
        uint8_t *dst = (uint8_t *)d;
        if (!size) return true;
        while (1) {
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
        fprintf(stderr, "Can't visualize packet with type: %d bits: %d\n", type, bits);
    }
};

// A struct specifying a text label that will appear on the screen at some point.
struct Label {
    const char *text;
    int x, y;
};

// A struct specifying how a single Func will get visualized.
struct FuncDrawInfo {
    int zoom;
    int cost;
    int dims;
    int x, y;
    int x_stride[4];
    int y_stride[4];
    int color_dim;
    float min, max;
    vector<Label> labels;
    int first_draw_time;

    FuncDrawInfo() {
        memset(this, 0, sizeof(FuncDrawInfo));
    }

    void dump(const char *name) {
        fprintf(stderr,
                "Func %s:\n"
                " min: %f max: %f\n"
                " color_dim: %d\n"
                " dims: %d\n"
                " zoom: %d\n"
                " cost: %d\n"
                " x: %d y: %d\n"
                " x_stride: %d %d %d %d\n"
                " y_stride: %d %d %d %d\n",
                name,
                min, max,
                color_dim,
                dims,
                zoom, cost, x, y,
                x_stride[0], x_stride[1], x_stride[2], x_stride[3],
                y_stride[0], y_stride[1], y_stride[2], y_stride[3]);
    }
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
            " -l func label x y: When func is first touched, the label appears at\n"
            "    the given coordinates.\n"
            "\n"
            " For each Func you want to visualize, also specify:\n"
            " -f func_name min_value max_value color_dim zoom cost x y strides\n"
            " where\n"
            "  func_name: The name of the func or input image\n"
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
    assert(sizeof(Packet) == 4096);

    // State that determines how different funcs get drawn
    int frame_width = 1920, frame_height = 1080;
    map<string, FuncDrawInfo> draw_info;

    int timestep = 10000;

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
            if (i + 8 >= argc) {
                usage();
                return -1;
            }
            char *func = argv[++i];
            FuncDrawInfo &fdi = draw_info[func];
            fdi.min = atof(argv[++i]);
            fdi.max = atof(argv[++i]);
            fdi.color_dim = atoi(argv[++i]);
            fdi.zoom = atoi(argv[++i]);
            fdi.cost = atoi(argv[++i]);
            fdi.x = atoi(argv[++i]);
            fdi.y = atoi(argv[++i]);
            int d = 0;
            for (; i+1 < argc && argv[i+1][0] != '-' && d < 4; d++) {
                fdi.x_stride[d] = atoi(argv[++i]);
                fdi.y_stride[d] = atoi(argv[++i]);
            }
            fdi.dims = d;
            fdi.dump(func);

        } else if (next == "-l") {
            if (i + 4 >= argc) {
                usage();
                return -1;
            }
            char *func = argv[++i];
            char *text = argv[++i];
            int x = atoi(argv[++i]);
            int y = atoi(argv[++i]);
            Label l = {text, x, y};
            fprintf(stderr, "Adding label %s to func %s\n",
                    text, func);
            draw_info[func].labels.push_back(l);
        } else if (next == "-t") {
            if (i + 1 >= argc) {
                usage();
                return -1;
            }
            assert(i + 1 < argc);
            timestep = atoi(argv[++i]);
        } else {
            usage();
            return -1;
        }
        i++;
    }

    if (draw_info.empty()) {
        usage();
        return 0;
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

    size_t end_counter = 0;
    while (1) {
        // Hold for 500 frames once the trace has finished.
        if (end_counter) {
            halide_clock += timestep;
            end_counter++;
            if (end_counter == 500) {
                return 0;
            }
        }

        if (halide_clock >= video_clock) {
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
            size_t bytes = 4 * frame_width * frame_height;
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
                alpha /= 2;
                anim[i] = (alpha << 24) | rgb;
            }
        }

        // Read a tracing packet
        Packet p;
        if (!p.read_from_stdin()) {
            end_counter++;
            continue;
        }

        if (draw_info.find(p.name) == draw_info.end()) {
            fprintf(stderr, "Warning: ignoring func %s\n", p.name);
        }

        // Draw the event
        FuncDrawInfo &di = draw_info[p.name];

        if (di.first_draw_time == 0) {
            di.first_draw_time = halide_clock;
        }

        switch (p.event) {
        case 0: // load
        case 1: // store
        {
            int frames_since_first_draw = (halide_clock - di.first_draw_time) / timestep;
            if (frames_since_first_draw <= 10) {
                uint32_t color = frames_since_first_draw * 26;
                if (color > 255) color = 255;
                color *= 0x10101;
                for (size_t i = 0; i < di.labels.size(); i++) {
                    const Label &label = di.labels[i];
                    draw_text(label.text, label.x, label.y, color, text, frame_width, frame_height);
                }
            }

            if (p.event == 1) {
                // Stores take time proportional to the number of
                // items stored times the cost of the func.
                halide_clock += di.cost * (p.value_bytes() / (p.bits / 8));
            }
            // Check the tracing packet contained enough information
            // given the number of dimensions the user claims this
            // Func has.
            assert(p.num_int_args >= p.width * di.dims);
            if (p.num_int_args >= p.width * di.dims) {
                for (int lane = 0; lane < p.width; lane++) {
                    // Compute the screen-space x, y coord to draw this.
                    int x = di.x;
                    int y = di.y;
                    for (int d = 0; d < di.dims; d++) {
                        int a = p.get_int_arg(d * p.width + lane);
                        x += di.zoom * di.x_stride[d] * a;
                        y += di.zoom * di.y_stride[d] * a;
                    }

                    // Stores are orange, loads are blue.
                    uint32_t color = p.event == 0 ? 0xffffdd44 : 0xff44ddff;

                    uint32_t image_color;
                    bool update_image = false;

                    // If it's a store, or a load from an input,
                    // update one or more of the color channels of the
                    // image layer.
                    if (p.event == 1 || p.parent == -1) {
                        update_image = true;
                        // Get the old color, in case we're only
                        // updating one of the color channels.
                        image_color = image[frame_width * y + x];

                        double value = p.get_value_as<double>(lane);

                        // Normalize it.
                        value = 255 * (value - di.min) / (di.max - di.min);
                        if (value < 0) value = 0;
                        if (value > 255) value = 255;

                        // Convert to 8-bit color.
                        uint8_t int_value = (uint8_t)value;

                        if (di.color_dim < 0) {
                            // Grayscale
                            image_color = (int_value * 0x00010101) | 0xff000000;
                        } else {
                            // Color
                            uint32_t channel = p.get_int_arg(di.color_dim * p.width + lane);
                            uint32_t mask = ~(255 << (channel * 8));
                            image_color &= mask;
                            image_color |= int_value << (channel * 8);
                        }
                    }

                    // Draw the pixel
                    for (int dy = 0; dy < di.zoom; dy++) {
                        for (int dx = 0; dx < di.zoom; dx++) {
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
        case 2: // begin realization
        case 3: // end realization
            // Add a time cost to the beginning and end of realizations
            //halide_clock += 10000;
            // TODO: draw a rectangle of some color?
            break;
        case 4: // produce
        case 5: // update
        case 6: // consume
        case 7: // end consume
            break;
        default:
            fprintf(stderr, "Unknown tracing event code: %d\n", p.event);
            exit(-1);
        }

    }

    return 0;
}

}  // namespace

int main(int argc, char **argv) {
  run(argc, argv);
}
