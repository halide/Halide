#include "HalideTraceUtils.h"
#include "HalideBuffer.h"
#include "halide_image_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <vector>
#include <map>
#include <assert.h>
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

int halide_copy_to_host(void *user_context, struct halide_buffer_t *buf) {
    return 0;
}

struct Value {
    uint8_t payload[8];
    uint8_t defined[8];

    Value() { defined[0] = 0; }
    Value(Packet *p, int lane_idx=0) {
        defined[0] = 1;
        int bytes = p->type.bytes();
        unsigned long addr = (unsigned long) p->value();
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
    Value * values;

    FuncInfo() : values(NULL) {}
    FuncInfo(Packet *p) : values(NULL) {
        int real_dims = p->dimensions/p->type.lanes;
        assert(real_dims <= 16);
        for(int i=0; i<real_dims; i++){
            min_coords[i] = INT32_MAX;
            max_coords[i] = INT32_MIN;
        }
        dimensions = real_dims;
        type = p->type;
        type.lanes = 1;
    }
    ~FuncInfo() {
        if(values != NULL) {
            free(values);
        }
    }

    void add_preprocess(Packet *p){
        int real_dims = p->dimensions/p->type.lanes;
        int lanes = p->type.lanes;

        halide_type_t scalar_type = p->type;
        scalar_type.lanes = 1;

        assert(scalar_type == type);
        assert(real_dims == dimensions);

        for(int lane=0; lane < lanes; lane++) {
            for(int i=0; i<real_dims; i++) {
                if(p->coordinates()[lanes*i+lane] < min_coords[i]) { min_coords[i] = p->coordinates()[lanes*i+lane]; }
                if(p->coordinates()[lanes*i+lane] > max_coords[i]) { max_coords[i] = p->coordinates()[lanes*i+lane]; }
            }
        }
    }

    void allocate() {
        int size = 1;
        for(int i=0; i<dimensions; i++) {
            extents[i] = max_coords[i] - min_coords[i] + 1;
            size *= extents[i];
        }
        values = (Value *)malloc(size * sizeof(Value));
        for(int i=0; i<size; i++) {
            values[i] = Value();
        }
        assert(values != NULL);
    }

    void add(Packet *p) {
        int real_dims = p->dimensions/p->type.lanes;
        int lanes = p->type.lanes;

        assert(values != NULL);
        assert(real_dims > 0);

        for(int lane = 0; lane < lanes; lane++) {
            int offset = p->coordinates()[lane];
            for(int i=1; i<dimensions; i++) {
                offset += (p->coordinates()[lanes*i+lane] * extents[i-1]);
            }
            values[offset] = Value(p, lane);
        }
    }

    bool allocated() {
        return (values != NULL);
    }

    template<typename T, int D>
    Buffer<T, D> get_buffer() {
        halide_buffer_t buf;
        buf.host = (uint8_t*)values;
        halide_dimension_t dims[dimensions];
        dims[0].min = 0;
        dims[0].extent = extents[0];
        dims[0].stride = sizeof(Value)/type.bytes();
        for(int i=1; i<dimensions; i++) {
            dims[i].min = 0;
            dims[i].extent = extents[i];
            dims[i].stride = (dims[i-1].stride * dims[i-1].extent);
        }
        buf.dim = dims;
        buf.dimensions = dimensions;
        buf.type = type;

        Buffer<T, D> buffer(buf);
        return buffer;
    }
};

template<typename T, int D>
void save(Buffer<T,D> buf, string filename) {
    Halide::Tools::save_image(buf, filename);
}

void dump_func(string name, FuncInfo &func, BufferOutputOpts output_opts) {
    //For now, support only 2D buffers.
    if(func.dimensions != 2) {
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
    switch(func.type.code){
    case halide_type_code_t::halide_type_int:
        switch(func.type.bits) {
        case 8:
            save<int8_t, 2>(func.get_buffer<int8_t, 2>(), filename);
            break;
        case 16:
            save<int16_t, 2>(func.get_buffer<int16_t, 2>(), filename);
            break;
        case 32:
            save<int32_t, 2>(func.get_buffer<int32_t, 2>(), filename);
            break;
        case 64:
            save<int64_t, 2>(func.get_buffer<int64_t, 2>(), filename);
            break;
        default:
            bad_type_error(func.type);
        }
        break;
    case halide_type_code_t::halide_type_uint:
        switch(func.type.bits) {
        case 8:
            save<uint8_t, 2>(func.get_buffer<uint8_t, 2>(), filename);
            break;
        case 16:
            save<uint16_t, 2>(func.get_buffer<uint16_t, 2>(), filename);
            break;
        case 32:
            save<uint32_t, 2>(func.get_buffer<uint32_t, 2>(), filename);
            break;
        case 64:
            save<uint64_t, 2>(func.get_buffer<uint64_t, 2>(), filename);
            break;
        default:
            bad_type_error(func.type);
        }
        break;
    case halide_type_code_t::halide_type_float:
        switch(func.type.bits) {
        case 32:
            save<float, 2>(func.get_buffer<float, 2>(), filename);
            break;
        case 64:
            save<double, 2>(func.get_buffer<double, 2>(), filename);
            break;
        default:
            bad_type_error(func.type);
        }
        break;
    default:
        bad_type_error(func.type);
    }
}

void finish_dump(map<string, FuncInfo> &func_info, BufferOutputOpts output_opts) {

    printf("\nTrace stats:\n");
    printf("  Funcs:\n");
    for(auto &pair : func_info) {
        const string &name = pair.first;
        FuncInfo &info = pair.second;
        printf("    %s:\n", name.c_str());

        //Type
        printf("      Type: ");
        if(info.type.code == halide_type_code_t::halide_type_int) { printf("int%d\n", info.type.bits); }
        else if(info.type.code == halide_type_code_t::halide_type_uint) { printf("uint%d\n", info.type.bits); }
        else if(info.type.code == halide_type_code_t::halide_type_float) { printf("float%d\n", info.type.bits); }
        else assert(0);

        //Dimensions
        printf("      Dimensions: %d\n", info.dimensions);

        //Size of the func
        printf("      Size: ");
        for(int idx=0; idx<info.dimensions; idx++) {
            if(idx>0) printf("x");
            printf("%d", (info.max_coords[idx]-info.min_coords[idx])+1);
        }
        printf("\n");

        //Minima
        printf("      Minimum stored to in each dim: {");
        for(int idx=0; idx<info.dimensions; idx++) {
            if(idx>0) printf(", ");
            printf("%d", info.min_coords[idx]);
        }
        printf("}\n");

        //Maxima
        printf("      Maximum stored to in each dim: {");
        for(int idx=0; idx<info.dimensions; idx++) {
            if(idx>0) printf(", ");
            printf("%d", info.max_coords[idx]);
        }
        printf("}\n");
    }

    for(auto &pair : func_info) {
        string name = pair.first;
        FuncInfo &info = pair.second;
        dump_func(name, info, output_opts);
    }

    printf("Done.\n");

    return;
}

void usage(char * const * argv) {
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

int main(int argc, char * const * argv) {

    char * buf_filename = NULL;
    char * buf_imagetype = NULL;
    BufferOutputOpts outputopts;
    int c;
    while ((c = getopt (argc, argv, "i:t:")) != -1)
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

    if(buf_filename == NULL) usage(argv);
    if(buf_imagetype == NULL) usage(argv);

    string imagetype(buf_imagetype);
    if(imagetype == "jpg") { outputopts.type = BufferOutputOpts::JPG; }
    else if(imagetype == "png") { outputopts.type = BufferOutputOpts::PNG; }
    else if(imagetype == "pgm") { outputopts.type = BufferOutputOpts::PGM; }
    else { usage(argv); }

    int file_desc = open(buf_filename, O_RDONLY);
    if(file_desc == -1){
        fprintf(stderr, "[Error opening file: %s. Exiting.\n", argv[1]);
        exit(1);
    }


    printf("[INFO] Starting parse of binary trace...\n");
    int packet_count = 0;

    map<string, FuncInfo> func_info;

    printf("[INFO] First pass...\n");

    for(;;) {
        Packet p;
        if (!p.read_from_filedesc(file_desc)) {
            printf("[INFO] Finished pass 1 after %d packets.\n", packet_count);
            break;
        }

        //Packet read was successful.
        packet_count++;
        if((packet_count % 100000) == 0){
            printf("[INFO] Pass 1: Read %d packets so far.\n", packet_count);
        }

        //Check if this was a store packet.
        if( (p.event == halide_trace_store) || (p.event == halide_trace_load) ) {
            if(func_info.find(string(p.func())) == func_info.end()) {
                printf("[INFO] Found Func with tracked accesses: %s\n", p.func());
                func_info[string(p.func())] = FuncInfo(&p);
            }
            func_info[string(p.func())].add_preprocess(&p);
        }
    }

    packet_count = 0;
    lseek(file_desc, 0, SEEK_SET);
    for(auto &pair : func_info) {
        pair.second.allocate();
    }

    for(;;) {
        Packet p;
        if (!p.read_from_filedesc(file_desc)) {
            printf("[INFO] Finished pass 2 after %d packets.\n", packet_count);
            if(file_desc > 0) close(file_desc);
            finish_dump(func_info, outputopts);
            exit(0);
        }

        //Packet read was successful.
        packet_count++;
        if((packet_count % 100000) == 0){
            printf("[INFO] Pass 2: Read %d packets so far.\n", packet_count);
        }

        //Check if this was a store packet.
        if( (p.event == halide_trace_store) || (p.event == halide_trace_load) ) {
            assert(func_info.find(string(p.func())) != func_info.end());
            func_info[string(p.func())].add(&p);
        }
    }
}
