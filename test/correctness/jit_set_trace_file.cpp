#include "Halide.h"
#include <cstdio>
#include <string>
#include <vector>

using namespace Halide;

int file_descriptor(FILE *f) {
#ifdef _WIN32
    return _fileno(f);
#else
    return fileno(f);
#endif
}

// Counts the store packets for func_name in the binary trace in f.
int count_stores(FILE *f, const std::string &func_name) {
    fflush(f);
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    std::vector<uint8_t> data(len);
    if (len > 0 && fread(data.data(), 1, len, f) != (size_t)len) {
        printf("Failed to read trace file\n");
        exit(1);
    }
    int stores = 0;
    for (size_t pos = 0; pos + sizeof(halide_trace_packet_t) <= data.size();) {
        const halide_trace_packet_t *p = (const halide_trace_packet_t *)(data.data() + pos);
        if (p->size == 0 || pos + p->size > data.size()) {
            printf("Malformed trace packet at offset %d\n", (int)pos);
            exit(1);
        }
        if (p->event == halide_trace_store && func_name == p->func()) {
            stores++;
        }
        pos += p->size;
    }
    return stores;
}

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly JIT does not use the shared runtime.\n");
        return 0;
    }

    FILE *first = tmpfile();
    FILE *second = tmpfile();
    if (!first || !second) {
        printf("Failed to create temporary files\n");
        return 1;
    }

    // Set before the shared runtime exists, so it applies when the runtime is
    // created.
    Internal::JITSharedRuntime::set_trace_file(file_descriptor(first));

    Func f("f"), g("g");
    Var x;
    f(x) = x;
    g(x) = x * 2;
    f.trace_stores();
    g.trace_stores();

    f.realize({10});

    // Set while the shared runtime exists. Switching flushes the first file.
    Internal::JITSharedRuntime::set_trace_file(file_descriptor(second));

    g.realize({20});

    Internal::JITSharedRuntime::set_trace_file(-1);

    int first_f = count_stores(first, "f"), first_g = count_stores(first, "g");
    int second_f = count_stores(second, "f"), second_g = count_stores(second, "g");
    if (first_f != 10 || first_g != 0 || second_f != 0 || second_g != 20) {
        printf("Unexpected store counts: first file f=%d g=%d, second file f=%d g=%d\n",
               first_f, first_g, second_f, second_g);
        return 1;
    }

    fclose(first);
    fclose(second);

    printf("Success!\n");
    return 0;
}
