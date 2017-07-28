#include "HalideTraceUtils.h"
#include "HalideBuffer.h"
#include "halide_image_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <vector>
#include <map>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

/** \file
 *
 * A tool which can read a binary Halide trace file from stdin, and dump files containing the final pixel values
 * recorded for each traced Func.
 *
 * Currently dumps into supported Halide image formats.
 */

using namespace Halide;
using namespace Internal;

using std::map;
using std::vector;
using std::string;
using Halide::Runtime::Buffer;

struct BufferOutputOpts {
    enum OutputType {
        PNG = 0,
        JPG,
        PGM
    };

    enum OutputType type;
};

struct Value {
    uint8_t payload[8];
    bool defined;
    
    /* To make sure to play nice with Halide Buffers, round the total size
     * of the Value struct up to 16 by adding padding bytes. */
    uint8_t padding[8-sizeof(bool)];

    Value() { defined = false; }
    Value(Packet *p, int lane_idx = 0) {
        defined = true;
        int bytes = p->type.bytes();
        uintptr_t addr = (uintptr_t) p->value();
        addr += (bytes*lane_idx);
        memcpy((void*)payload, (void*)addr, bytes);
    }

    template<typename T>
    T get_as(halide_type_t type) {
        return value_as<T>(type, (void*)payload);
    }
};

struct FuncInfo {
    int min_coords[16];
    int max_coords[16];
    int extents[16];
    int dimensions;
    halide_type_t type;
    Value *values;

    FuncInfo() : values(nullptr) {}
    FuncInfo(Packet *p) : values(NULL) {
        int real_dims = p->dimensions/p->type.lanes;
        if (real_dims > 16) {
            fprintf(stderr, "Error: found trace packet with dimensionality > 16. Aborting.\n");
            exit(-1);
        }
        for (int i = 0; i < real_dims; i++) {
            min_coords[i] = INT32_MAX;
            max_coords[i] = INT32_MIN;
        }
        dimensions = real_dims;
        type = p->type;
        type.lanes = 1;
    }
    ~FuncInfo() {
        free(values);
    }

    void add_preprocess(Packet *p) {
        int real_dims = p->dimensions/p->type.lanes;
        int lanes = p->type.lanes;

        halide_type_t scalar_type = p->type;
        scalar_type.lanes = 1;

        if (scalar_type != type) {
            fprintf(stderr, "Error: packet type doesn't match previous packets of same Func. Aborting.\n");
            exit(-1);
        }
        if (real_dims != dimensions) {
            fprintf(stderr, "Error: packet dimensionality doesn't match previous packets of same Func. Aborting.\n");
            exit(-1);
        }

        for (int lane = 0; lane < lanes; lane++) {
            for (int i = 0; i < real_dims; i++) {
                if (p->coordinates()[lanes*i+lane] < min_coords[i]) {
                    min_coords[i] = p->coordinates()[lanes*i+lane];
                }
                if (p->coordinates()[lanes*i+lane] > max_coords[i]) {
                    max_coords[i] = p->coordinates()[lanes*i+lane];
                }
            }
        }
    }

    void allocate() {
        int size = 1;
        for (int i = 0; i < dimensions; i++) {
            extents[i] = max_coords[i] - min_coords[i] + 1;
            size *= extents[i];
        }
        values = (Value *)malloc(size * sizeof(Value));
        for (int i = 0; i < size; i++) {
            values[i] = Value();
        }
        if (values == nullptr) {
            fprintf(stderr, "Memory allocation failure. Aborting.\n");
            exit(-1);
        }
    }

    void add(Packet *p) {
        int real_dims = p->dimensions/p->type.lanes;
        int lanes = p->type.lanes;

        if (values == nullptr) {
            fprintf(stderr, "Packet storage not allocated. Aborting.\n");
            exit(-1);
        }
        if (real_dims < 0) {
            fprintf(stderr, "Negative dimensionality found. Aborting.\n");
            exit(-1);
        }

        for (int lane = 0; lane < lanes; lane++) {
            int offset = p->coordinates()[lane];
            for (int i = 1; i < dimensions; i++) {
                offset += (p->coordinates()[lanes*i+lane] * extents[i-1]);
            }
            values[offset] = Value(p, lane);
        }
    }

    bool allocated() {
        return (values != NULL);
    }

    Buffer<void> get_buffer() {
        halide_buffer_t buf;
        buf.device = 0;
        buf.device_interface = nullptr;
        buf.flags = 0;
        buf.padding = nullptr;
        buf.host = (uint8_t*)values;
        halide_dimension_t dims[dimensions];
        dims[0].min = 0;
        dims[0].extent = extents[0];
        dims[0].stride = sizeof(Value)/type.bytes();
        for (int i = 1; i < dimensions; i++) {
            dims[i].min = 0;
            dims[i].extent = extents[i];
            dims[i].stride = (dims[i-1].stride * dims[i-1].extent);
        }
        buf.dim = dims;
        buf.dimensions = dimensions;
        buf.type = type;

        Buffer<void> buffer(buf);
        return buffer;
    }
};

void dump_func(string name, FuncInfo &func, BufferOutputOpts output_opts) {
    // For now, support only 2D buffers.
    if (func.dimensions != 2) {
        printf("[INFO] Skipping func '%s', which is not 2D (has %d dimensions).\n", name.c_str(), func.dimensions);
    }

    string filename;
    switch(output_opts.type) {
    case BufferOutputOpts::PNG:
        filename = name + ".png";
        break;
    case BufferOutputOpts::JPG:
        filename = name + ".jpg";
        break;
    case BufferOutputOpts::PGM:
        filename = name + ".pgm";
        break;
    default:
        exit(1);
    }

    printf("[INFO] Dumping func '%s' to file: %s\n", name.c_str(), filename.c_str());

    //Rely on save_image to do type-checking
    auto buf = func.get_buffer();
    Halide::Tools::save_image(buf, filename);
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
        }
        else {
            fprintf(stderr, "Unsupported Func type. Aborting.\n");
            exit(-1);
        }

        // Dimensions
        printf("      Dimensions: %d\n", info.dimensions);

        // Size of the func
        printf("      Size: ");
        for (int idx = 0; idx < info.dimensions; idx++) {
            if (idx>0) {
                printf("x");
            }
            printf("%d", (info.max_coords[idx]-info.min_coords[idx])+1);
        }
        printf("\n");

        // Minima
        printf("      Minimum stored to in each dim: {");
        for (int idx = 0; idx < info.dimensions; idx++) {
            if (idx>0) {
                printf(", ");
            }
            printf("%d", info.min_coords[idx]);
        }
        printf("}\n");

        // Maxima
        printf("      Maximum stored to in each dim: {");
        for (int idx = 0; idx < info.dimensions; idx++) {
            if (idx>0) {
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

void usage(char * const *argv) {
    const string usage =
            "Usage: " + string(argv[0]) + " -i trace_file -t output_type\n"
            "  Available output types: png, jpg, pgm\n"
            "The tool will record all element loads and stores found in the trace, and "
            "dump them into separate image files per Func.\n"
            "To generate a trace, use the trace_loads() and trace_stores() scheduling "
            "commands in your Halide application, and run with HL_TRACE_FILE=<filename>.\n"
            "Only 2D buffers will be dumped.";
    fprintf(stderr, "%s\n", usage.c_str());
    exit(1);
}

int main(int argc, char * const *argv) {

    char *buf_filename = nullptr;
    char *buf_imagetype = nullptr;
    BufferOutputOpts outputopts;
    int c;
    while ((c = getopt (argc, argv, "i:t:")) != -1) {
        switch (c)
        {
        case 'i':
            buf_filename = optarg;
            break;
        case 't':
            buf_imagetype = optarg;
            break;
        case '?':
        default:
            usage(argv);
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
    }
    else {
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
        if ( (p.event == halide_trace_store) || (p.event == halide_trace_load) ) {
            if (func_info.find(string(p.func())) == func_info.end()) {
                printf("[INFO] Found Func with tracked accesses: %s\n", p.func());
                func_info[string(p.func())] = FuncInfo(&p);
            }
            func_info[string(p.func())].add_preprocess(&p);
        }
    }

    packet_count = 0;
    fseek(file_desc, 0, SEEK_SET);
    if(ferror(file_desc)){
        fprintf(stderr, "Error: couldn't seek back to beginning of trace file. Aborting.\n");
        exit(-1);
    }

    for (auto &pair : func_info) {
        pair.second.allocate();
    }

    for (;;) {
        Packet p;
        if (!p.read_from_filedesc(file_desc)) {
            printf("[INFO] Finished pass 2 after %d packets.\n", packet_count);
            if (file_desc > 0) {
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
        if ( (p.event == halide_trace_store) || (p.event == halide_trace_load) ) {
            if (func_info.find(string(p.func())) == func_info.end()) {
                fprintf(stderr, "Unable to find Func on 2nd pass. Aborting.\n");
                exit(-1);
            }
            func_info[string(p.func())].add(&p);
        }
    }
}
