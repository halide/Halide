#include "HalideRuntime.h"

#define WEAK __attribute__((weak))

extern "C" {

#define POOL_SIZE (1 << 8)

WEAK int32_t halide_random_state_pool[POOL_SIZE];
WEAK uint32_t halide_random_state_pool_index = 0;

WEAK uint32_t halide_random_seed = 0;
WEAK void halide_set_random_seed(uint32_t s) {
    halide_random_seed = s;

    // Also clear the internal state.
    for (int i = 0; i < POOL_SIZE; i++) {
        halide_random_state_pool[i] = 0;
    }
    halide_random_state_pool_index = 0;
}

// Get 31 random bits.
WEAK uint32_t rand_u31(void *user_context, int tag) {
    // Claim an entry from the state pool. This is mostly
    // thread-safe. It's possible but unlikely that another thread
    // will catch up with me and use the same state as me.
    uint32_t idx = __sync_fetch_and_add(&halide_random_state_pool_index, 1);
    idx &= (POOL_SIZE - 1);

    // If this state hasn't been initialized yet, use the array index.
    // We use the high bit of the state to track initialization.
    int32_t state = halide_random_state_pool[idx];
    if (state == 0) {
        // The initial state should not be a linear function of the
        // index, because the generator is also linear, so you get
        // weird patterns. A simple cubic seems to work fine. Please
        // never use this for crypto.
        state = (idx + 115) * (idx + 123) * (idx + 17 + halide_random_seed);
    } else {
        state = state & 0x7fffffff;
    }

    // Incorporate the tag, so that different tags result in different
    // sequences.
    state += tag;

    // Update the state using a classic 31-bit linear congruential rng.
    state = (state * 1103515245 + 12345) & 0x7fffffff;

    // Also set the top bit so we know we're initialized.
    halide_random_state_pool[idx] = state | 0x80000000;

    return state;
}

WEAK float rand_f32(void *user_context, int tag) {

    uint32_t bits = rand_u31(user_context, tag);

    // Use the top 23 bits for the mantissa of a float in [1.0f, 2.0f)
    union {
        uint32_t as_uint;
        float as_float;
    } u;

    u.as_uint = (127 << 23) | (bits >> 8);

    // Subtract one to get a float in [0.0f, 1.0f)
    return u.as_float - 1.0f;
}

WEAK int32_t rand_i32(void *user_context, int tag) {

    // Get 31 random bits for the high part.
    uint32_t bits_1 = rand_u31(user_context, tag);

    // Get some more bits to randomize up the low part.
    uint32_t bits_2 = rand_u31(user_context, tag);

    return (bits_1 << 1) ^ (bits_2 >> 15);
}

}
