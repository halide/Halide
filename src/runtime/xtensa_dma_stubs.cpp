#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char uint8_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef __SIZE_TYPE__ size_t;

void *memcpy(void *destination, const void *source, size_t num);

void *halide_malloc(void *user_context, size_t x);
void halide_free(void *user_context, void *ptr);

void *halide_tcm_malloc(void *user_context, unsigned int x) {
    return halide_malloc(user_context, x);
}

void halide_tcm_free(void *user_context, void *ptr) {
    halide_free(user_context, ptr);
}

int halide_init_dma() {
    return 0;
}

void halide_release_dma() {
}

int32_t halide_xtensa_copy_1d(void *dst, int32_t dst_base, void *src, int32_t src_base, int extent, int item_size) {
    memcpy((uint8_t *)dst + dst_base * item_size, (uint8_t *)src + src_base * item_size, extent * item_size);
    return 0;
}

int32_t halide_xtensa_wait_for_copy(int32_t id) {
    return 0;
}

#ifdef __cplusplus
}  // extern "C"
#endif
