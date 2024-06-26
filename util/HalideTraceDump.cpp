#include "HalideBuffer.h"
#include "HalideTraceUtils.h"
#include "halide_image_io.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <string>
#include <vector>

/** \file
 *
 * A tool which can read a binary Halide trace file, and dump files
 * containing the final pixel values recorded for each traced Func.
 *
 * Currently dumps into supported Halide image formats.
 */

using namespace Halide;
using namespace Internal;

using Halide::Runtime::Buffer;
using std::map;
using std::string;
using std::vector;

struct BufferOutputOpts {
    enum OutputType {
        PNG = 0,
        JPG,
        PGM,
        TMP,
        MAT
    };

    enum OutputType type;
};

struct FuncInfo {
    int min_coords[16];
    int max_coords[16];
    int dimensions;
    halide_type_t type;
    Buffer<> values;

    FuncInfo() = default;
    FuncInfo(Packet *p) {
        int real_dims = p->dimensions / p->type.lanes;
        if (real_dims > 16) {
            fprintf(stderr, "Error: found trace packet with dimensionality > 16. Aborting.\n");
            exit(1);
        }
        for (int i = 0; i < real_dims; i++) {
            min_coords[i] = INT32_MAX;
            max_coords[i] = INT32_MIN;
        }
        dimensions = real_dims;
        type = p->type;
        type.lanes = 1;
    }

    void add_preprocess(Packet *p) {
        int real_dims = p->dimensions / p->type.lanes;
        int lanes = p->type.lanes;

        halide_type_t scalar_type = p->type;
        scalar_type.lanes = 1;

        if (scalar_type != type) {
            fprintf(stderr, "Error: packet type doesn't match previous packets of same Func. Aborting.\n");
            exit(1);
        }
        if (real_dims != dimensions) {
            fprintf(stderr, "Error: packet dimensionality doesn't match previous packets of same Func. Aborting.\n");
            exit(1);
        }

        for (int lane = 0; lane < lanes; lane++) {
            for (int i = 0; i < real_dims; i++) {
                if (p->coordinates()[lanes * i + lane] < min_coords[i]) {
                    min_coords[i] = p->coordinates()[lanes * i + lane];
                }
                if (p->coordinates()[lanes * i + lane] > max_coords[i]) {
                    max_coords[i] = p->coordinates()[lanes * i + lane];
                }
            }
        }
    }

    void allocate() {
        std::vector<int> extents;
        for (int i = 0; i < dimensions; i++) {
            extents.push_back(max_coords[i] - min_coords[i] + 1);
        }
        values = Buffer<>(type, extents);
        if (values.data() == nullptr) {
            fprintf(stderr, "Memory allocation failure. Aborting.\n");
            exit(1);
        }
    }

    void add(Packet *p) {
        halide_type_t scalar_type = p->type;
        scalar_type.lanes = 1;
        if (scalar_type == halide_type_of<float>()) {
            add_typed<float>(p);
        } else if (scalar_type == halide_type_of<double>()) {
            add_typed<double>(p);
        } else if (scalar_type == halide_type_of<uint8_t>()) {
            add_typed<uint8_t>(p);
        } else if (scalar_type == halide_type_of<uint16_t>()) {
            add_typed<uint16_t>(p);
        } else if (scalar_type == halide_type_of<uint32_t>()) {
            add_typed<uint32_t>(p);
        } else if (scalar_type == halide_type_of<uint64_t>()) {
            add_typed<uint64_t>(p);
        } else if (scalar_type == halide_type_of<int8_t>()) {
            add_typed<int8_t>(p);
        } else if (scalar_type == halide_type_of<int16_t>()) {
            add_typed<int16_t>(p);
        } else if (scalar_type == halide_type_of<int32_t>()) {
            add_typed<int32_t>(p);
        } else if (scalar_type == halide_type_of<int64_t>()) {
            add_typed<int64_t>(p);
        } else if (scalar_type == halide_type_of<bool>()) {
            add_typed<bool>(p);
        } else {
            printf("Packet with unknown type\n");
            exit(1);
        }
    }

    template<typename T>
    void add_typed(Packet *p) {
        Buffer<T> &buf = values.as<T>();
        int lanes = p->type.lanes;

        if (!allocated()) {
            fprintf(stderr, "Packet storage not allocated. Aborting.\n");
            exit(1);
        }

        if (p->dimensions < 0) {
            fprintf(stderr, "Negative dimensionality found. Aborting.\n");
            exit(1);
        }

        for (int lane = 0; lane < lanes; lane++) {
            int coord[16];
            for (int i = 0; i < dimensions; i++) {
                coord[i] = p->coordinates()[lanes * i + lane] - min_coords[i];
            }
            buf(coord) = p->get_value_as<T>(lane);
        }
    }

    bool allocated() const {
        return (values.data() != nullptr);
    }
};

bool check_and_continue(bool condition, const char *msg) {
    if (!condition) {
        fprintf(stderr, "Failed to dump func: %s\n", msg);
    }
    return condition;
}

void dump_func(string name, FuncInfo &func, BufferOutputOpts output_opts) {
    // Remove special characters
    for (char &c : name) {
        if (!std::isalnum(c)) {
            c = '_';
        }
    }

    string filename;
    switch (output_opts.type) {
    case BufferOutputOpts::PNG:
        filename = name + ".png";
        break;
    case BufferOutputOpts::JPG:
        filename = name + ".jpg";
        break;
    case BufferOutputOpts::PGM:
        filename = name + ".pgm";
        break;
    case BufferOutputOpts::TMP:
        filename = name + ".tmp";
        break;
    case BufferOutputOpts::MAT:
        filename = name + ".mat";
        break;
    default:
        exit(1);
    }

    printf("[INFO] Dumping func '%s' to file: %s\n", name.c_str(), filename.c_str());

    // Rely on save_image to do type-checking
    Halide::Tools::convert_and_save_image<Buffer<>, check_and_continue>(func.values, filename);
}

void finish_dump(map<string, FuncInfo> &func_info, BufferOutputOpts output_opts) {
    printf("\nTrace stats:\n");
    printf("  Funcs:\n");
    for (auto &pair : func_info) {
        const string &name = pair.first;
        FuncInfo &info = pair.second;
        printf("    %s:\n", name.c_str());

        // Type
        printf("      Type: ");
        if (info.type.code == halide_type_code_t::halide_type_int) {
            printf("int%d\n", info.type.bits);
        } else if (info.type.code == halide_type_code_t::halide_type_uint) {
            printf("uint%d\n", info.type.bits);
        } else if (info.type.code == halide_type_code_t::halide_type_float) {
            printf("float%d\n", info.type.bits);
        } else {
            fprintf(stderr, "Unsupported Func type. Aborting.\n");
            exit(1);
        }

        // Dimensions
        printf("      Dimensions: %d\n", info.dimensions);

        // Size of the func
        printf("      Size: ");
        for (int idx = 0; idx < info.dimensions; idx++) {
            if (idx > 0) {
                printf("x");
            }
            printf("%d", (info.max_coords[idx] - info.min_coords[idx]) + 1);
        }
        printf("\n");

        // Minima
        printf("      Minimum stored to in each dim: {");
        for (int idx = 0; idx < info.dimensions; idx++) {
            if (idx > 0) {
                printf(", ");
            }
            printf("%d", info.min_coords[idx]);
        }
        printf("}\n");

        // Maxima
        printf("      Maximum stored to in each dim: {");
        for (int idx = 0; idx < info.dimensions; idx++) {
            if (idx > 0) {
                printf(", ");
            }
            printf("%d", info.max_coords[idx]);
        }
        printf("}\n");
    }

    for (auto &pair : func_info) {
        string name = pair.first;
        FuncInfo &info = pair.second;
        dump_func(name, info, output_opts);
    }

    printf("Done.\n");
}

void usage(char *const *argv) {
    const string usage =
        "Usage: " + string(argv[0]) +
        " -i trace_file -t {png,jpg,pgm,tmp,mat}\n"
        "\n"
        "This tool reads a binary trace produced by Halide, and dumps all\n"
        "Funcs into individual image files in the current directory.\n"
        "To generate a suitable binary trace, use Func::trace_stores(), or the\n"
        "target features trace_stores and trace_realizations, and run with\n"
        "HL_TRACE_FILE=<filename>.\n";
    fprintf(stderr, "%s\n", usage.c_str());
    exit(1);
}

int main(int argc, char *const *argv) {
    char *buf_filename = nullptr;
    char *buf_imagetype = nullptr;
    BufferOutputOpts outputopts;
    for (int i = 1; i < argc - 1; i++) {
        string arg = argv[i];
        if (arg == "-t") {
            i++;
            buf_imagetype = argv[i];
        } else if (arg == "-i") {
            i++;
            buf_filename = argv[i];
        }
    }

    if (buf_filename == nullptr) {
        usage(argv);
    }
    if (buf_imagetype == nullptr) {
        usage(argv);
    }

    string imagetype(buf_imagetype);
    if (imagetype == "jpg") {
        outputopts.type = BufferOutputOpts::JPG;
    } else if (imagetype == "png") {
        outputopts.type = BufferOutputOpts::PNG;
    } else if (imagetype == "pgm") {
        outputopts.type = BufferOutputOpts::PGM;
    } else if (imagetype == "tmp") {
        outputopts.type = BufferOutputOpts::TMP;
    } else if (imagetype == "mat") {
        outputopts.type = BufferOutputOpts::MAT;
    } else {
        usage(argv);
    }

    FILE *file_desc = fopen(buf_filename, "r");
    if (file_desc == nullptr) {
        fprintf(stderr, "[Error opening file: %s. Exiting.\n", argv[1]);
        exit(1);
    }

    printf("[INFO] Starting parse of binary trace...\n");
    int packet_count = 0;

    map<string, FuncInfo> func_info;

    printf("[INFO] First pass...\n");

    for (;;) {
        Packet p;
        if (!p.read_from_filedesc(file_desc)) {
            printf("[INFO] Finished pass 1 after %d packets.\n", packet_count);
            break;
        }

        // Packet read was successful.
        packet_count++;
        if ((packet_count % 100000) == 0) {
            printf("[INFO] Pass 1: Read %d packets so far.\n", packet_count);
        }

        // Check if this was a store packet.
        if ((p.event == halide_trace_store) || (p.event == halide_trace_load)) {
            if (func_info.find(string(p.func())) == func_info.end()) {
                printf("[INFO] Found Func with tracked accesses: %s\n", p.func());
                func_info[string(p.func())] = FuncInfo(&p);
            }
            func_info[string(p.func())].add_preprocess(&p);
        }
    }

    packet_count = 0;
    fseek(file_desc, 0, SEEK_SET);
    if (ferror(file_desc)) {
        fprintf(stderr, "Error: couldn't seek back to beginning of trace file. Aborting.\n");
        exit(1);
    }

    for (auto &pair : func_info) {
        pair.second.allocate();
    }

    for (;;) {
        Packet p;
        if (!p.read_from_filedesc(file_desc)) {
            printf("[INFO] Finished pass 2 after %d packets.\n", packet_count);
            if (file_desc != nullptr) {
                fclose(file_desc);
            }
            finish_dump(func_info, outputopts);
            exit(0);
        }

        // Packet read was successful.
        packet_count++;
        if ((packet_count % 100000) == 0) {
            printf("[INFO] Pass 2: Read %d packets so far.\n", packet_count);
        }

        // Check if this was a store packet.
        if ((p.event == halide_trace_store) || (p.event == halide_trace_load)) {
            if (func_info.find(string(p.func())) == func_info.end()) {
                fprintf(stderr, "Unable to find Func on 2nd pass. Aborting.\n");
                exit(1);
            }
            func_info[string(p.func())].add(&p);
        }
    }
}
