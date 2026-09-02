#ifndef SUPERBLURS_UTIL_H
#define SUPERBLURS_UTIL_H

#include <atomic>
#include <mutex>
#include <random>
#include <string>
#include <unistd.h>

// Grab this to serialize debugging prints
static std::mutex global_mutex;

// Set this to tie debugging info to a particular tile
static thread_local int thread_id = 0;

inline void write_tmp_file(const char *filename, const float *data,
                           size_t width, size_t height, size_t stride) {
    int header[5] = {(int)width, (int)height, 1, 1, 0};

    std::string filename_str = filename;
    static int pid = (int)getpid();
    static thread_local int counter = 0;
    filename_str = (std::to_string(pid) + "_" +
                    std::to_string(thread_id) + "_" +
                    std::to_string(counter++) + "_" +
                    filename_str);

    global_mutex.lock();
    fprintf(stderr, "Dumping to %s: %p %zu %zu %zu\n", filename_str.c_str(), data, width, height, stride);
    global_mutex.unlock();

    FILE *f = fopen(filename_str.c_str(), "wb");
    fwrite(header, sizeof(int), 5, f);
    for (int y = 0; y < height; y++) {
        fwrite(data + y * stride, sizeof(float), width, f);
    }
    fclose(f);

    if (counter >= 1024) {
        fprintf(stderr, "Aborting to avoid filling disk with temporaries\n");
        abort();
    }
}

inline void fill_with_white_noise(float *data, size_t size) {
    std::mt19937 rng{0};
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (size_t i = 0; i < size; i++) {
        data[i] = dist(rng);
    }
}

inline void dump_to_file(const char *filename, const float *data,
                         size_t width, size_t height, size_t stride) {
    static bool dump = getenv("DUMP_INTERMEDIATES") != nullptr;
    if (dump) {
        write_tmp_file(filename, data, width, height, stride);
    }
}

#endif
