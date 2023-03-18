
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "zlib.h"

#ifdef _WIN32
#include <fcntl.h>  // O_BINARY
#include <io.h>     // setmode
#endif

// Embeds a binary blob (from stdin) in a C++ source array of unsigned
// chars. Similar to the xxd utility.

static int usage() {
    fprintf(stderr, "Usage: binary2cpp identifier [-header] [-zlib]\n");
    return 1;
}

std::vector<uint8_t> zlib_deflate(const std::vector<uint8_t> &input, int level = 9) {
    constexpr size_t kChunkSize = 16384;
    size_t offset = 0;
    size_t remaining = input.size();
    int flush = Z_NO_FLUSH;
    std::vector<uint8_t> output;

    z_stream z;
    memset(&z, 0, sizeof(z));
    if (int result = deflateInit(&z, level); result != Z_OK) {
        fprintf(stderr, "deflateInit failed\n");
        exit(1);
    }

    do {
        z.avail_in = std::min(kChunkSize, remaining);
        z.next_in = const_cast<uint8_t *>(&input[offset]);
        offset += z.avail_in;
        remaining -= z.avail_in;
        flush = (remaining == 0) ? Z_FINISH : Z_NO_FLUSH;

        do {
            uint8_t out_buf[kChunkSize];
            z.avail_out = kChunkSize;
            z.next_out = out_buf;
            if (int result = deflate(&z, flush); result != Z_OK && result != Z_STREAM_END) {
                fprintf(stderr, "deflate failed: %d\n", result);
                exit(1);
            }

            const size_t actual_out = kChunkSize - z.avail_out;
            output.insert(output.end(), out_buf, out_buf + actual_out);
        } while (z.avail_out == 0);
        assert(z.avail_in == 0);
    } while (flush != Z_FINISH);

    assert(offset == input.size());
    assert(remaining == 0);

    if (int result = deflateEnd(&z); result != Z_OK) {
        fprintf(stderr, "deflateEnd failed\n");
        exit(1);
    }

    return output;
}

int main(int argc, const char **argv) {
    bool compress = false;
    bool header = false;

    if (argc < 2) {
        return usage();
    }
    std::string target = argv[1];

    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-header")) {
            header = true;
        } else if (!strcmp(argv[i], "-zlib")) {
            compress = true;
        } else {
            return usage();
        }
    }

    if (compress) {
        target = "z_" + target;
    }

    if (header) {
        printf("#ifndef _H_%s_binary2cpp\n", target.c_str());
        printf("#define _H_%s_binary2cpp\n", target.c_str());
        printf("extern \"C\" {\n");
        printf("extern unsigned char %s[];\n", target.c_str());
        printf("extern int %s_length;\n", target.c_str());
        printf("}  // extern \"C\"\n");
        printf("#endif  // _H_%s_binary2cpp\n", target.c_str());
        return 0;
    }

#ifdef _WIN32
    setmode(fileno(stdin), O_BINARY);  // On windows bad things will happen unless we read stdin in binary mode
#endif

    // Just slurp everything until EOF.
    // Not particularly efficient, but we don't really care.
    std::vector<uint8_t> input;
    while (true) {
        int c = getchar();
        if (c == EOF) {
            break;
        }
        input.push_back((uint8_t)c);
    }

    std::vector<uint8_t> output;
    if (compress) {
        output = zlib_deflate(input);
    } else {
        output = std::move(input);
    }

    printf("extern \"C\" {\n");
    printf("int %s_length = %d;\n", target.c_str(), (int)output.size());
    if (compress) {
        printf("// Uncompressed length: %d\n", (int)input.size());
    }
    printf("unsigned char %s[%d+1] = {\n", target.c_str(), (int)output.size());
    int line_break = 0;
    for (auto it = output.begin(); it != output.end(); it++) {
        printf("0x%02x, ", (int)*it);
        // Not necessary, but makes a bit easier to read
        if (++line_break > 12) {
            printf("\n");
            line_break = 0;
        }
    }
    // Always append a zero to the end -- this is not included in the 'length' var
    printf("0};\n");
    printf("}  // extern \"C\"\n");
    return 0;
}
