#ifndef SUPERBLURS_CIRCULARBUFFER_H
#define SUPERBLURS_CIRCULARBUFFER_H

#include <memory>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

// A circular buffer of the given size in floats. Can read up to N floats
// before or after the ptr at all times, with wrap-around semantics using
// virtual memory tricks.
template<size_t N>
class CircularBuffer {
    std::shared_ptr<uint8_t> mmapped_region;
    uint8_t *mem = 0;
    uint32_t idx_bytes = 0;

    static constexpr size_t page_size_in_bytes = 4096;
    static constexpr size_t page_size_in_floats = page_size_in_bytes / sizeof(float);
    static constexpr size_t page_size_in_float_vecs = page_size_in_bytes / sizeof(float_vec);
    static constexpr size_t num_pages = (N + page_size_in_floats - 1) / page_size_in_floats;
    static constexpr size_t num_float_vecs = num_pages * page_size_in_float_vecs;
    static constexpr size_t num_floats = num_pages * page_size_in_floats;
    static constexpr size_t num_bytes = num_pages * page_size_in_bytes;

public:
    void push(const float_vec *in, int num) {
        float_vec *dst = (float_vec *)(mem + idx_bytes);
        for (int i = 0; i < num; i++) {
            dst[i] = in[i];
        }

        idx_bytes += num * sizeof(float_vec);

        if constexpr (num_bytes & (num_bytes - 1)) {
            idx_bytes -= (idx_bytes > num_bytes) ? num_bytes : 0;
        } else {
            idx_bytes &= num_bytes - 1;
        }
    }

    // Load from an offset in floats.
    // void load(int idx, int num, float_vec *__restrict__ out) const {
    void load(int idx, int num, float_vec *out) const {
        std::memcpy(out, ((const float *)(mem + idx_bytes)) + idx, num * sizeof(float_vec));
    }

    void reset() {
        std::memset(mem, 0, num_bytes);
    }

    CircularBuffer() {
        // Make a virtual file of size num_bytes
        int fd = memfd_create("dummy", 0);
        ftruncate(fd, num_bytes);
        // Make a region of address space of size num_bytes * 3
        mem = (uint8_t *)mmap(nullptr, num_bytes * 3, PROT_NONE, MAP_ANON | MAP_PRIVATE, 0, 0);
        // Map each num_bytes of the address space to the same num_bytes of the virtual file
        for (int i = 0; i < 3; i++) {
            auto ret = (uint8_t *)mmap(mem + num_bytes * i, num_bytes, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_SHARED, fd, 0);
            assert(ret == mem + num_bytes * i);
        }
        // Don't actually need the virtual file anymore
        close(fd);

        assert(mem);
        reset();

        // Double-check it worked
        mem[num_bytes + 3] = 78;
        assert(mem[3] == 78);
        assert(mem[2 * num_bytes + 3] == 78);
        mem[3] = 0;
        assert(mem[num_bytes + 3] == 0);
        assert(mem[2 * num_bytes + 3] == 0);

        idx_bytes = 0;

        mem += num_bytes;  // Remember to subtract this in the destructor.

        struct Deleter {
            void operator()(uint8_t *mem) {
                munmap(mem - num_bytes, num_bytes * 3);
            }
        } deleter;

        mmapped_region = std::shared_ptr<uint8_t>(mem, deleter);
    }
};

template<size_t num_floats, int max_push_rate>
struct CircularBufferV2 {
    constexpr static size_t num_vecs = (num_floats + vec_lanes - 1) / vec_lanes;
    float_vec memory[num_vecs * 2 + max_push_rate] = {};
    int cursor = num_vecs;
    int cursor_limit = num_vecs * 2;

    CircularBufferV2() {
    }

    void push(const float_vec *in, int num) {

        if (cursor > cursor_limit) {
            // Copy the last num_vecs - num back to the start
            int new_cursor = num_vecs;
            int offset = cursor - new_cursor;
            for (int i = 0; i < new_cursor; i++) {
                ((volatile float_vec *)memory)[i] = memory[i + offset];
            }
            cursor = new_cursor;
        }

        for (int i = 0; i < num; i++) {
            memory[cursor++] = in[i];
        }
    }

    void load(int idx, int num, float_vec *out) const {
        int load_idx = cursor * vec_lanes + idx;
        std::memcpy(out, ((const float *)memory) + load_idx, num * sizeof(float_vec));
    }
};

// Outputs zeros for N floats before it starts giving you the values from the
// input. TODO: use registers for a small delay.
template<int N, int _rate>
struct Delay {
    constexpr static int rate = _rate;
    constexpr static int fmas_per_output = 0;

    CircularBuffer<N + rate * vec_lanes> c;

    // A notional number of extra non-zero outputs (that will actually be
    // zero). Exists solely for the shape logic.
    int right_pad = 0;

    void reset() {
        c.reset();
    }

    void shape(size_t input_size, size_t *output_size) const {
        *output_size = input_size + N + right_pad;
    }

    __attribute__((always_inline)) void run(const float_vec *input, float_vec *output) {
        if (N == 0) {
            std::memcpy(output, input, rate * sizeof(float_vec));
        } else {
            if (true || N < rate * vec_lanes) {
                c.push(input, rate);
                c.load(-(N + rate * vec_lanes), rate, output);
            } else {
                c.load(-N, rate, output);
                c.push(input, rate);
            }
        }
    }

    Delay(int p = 0)
        : right_pad(p) {
    }
};

#endif
