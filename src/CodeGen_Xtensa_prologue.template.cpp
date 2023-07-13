
#define XCHAL_VISION_SIMD8 (XCHAL_VISION_SIMD16 * 2)

// TODO(vksnk): this is disabled by default, because iDMA is not part of cstub
// so we need to get git repo compiling with xt-tools first (b/173159625)

#ifdef __cplusplus
extern "C" {
#endif

extern void *halide_tcm_malloc(void *user_context, size_t x) __attribute__((malloc));
extern void halide_tcm_free(void *user_context, void *ptr);
extern void *halide_tcm_bump_malloc(void *user_context, size_t x) __attribute__((malloc));
extern void halide_tcm_bump_free(void *user_context, void *ptr);
extern void **halide_init_dma(int32_t channel_count);
extern int32_t halide_xtensa_copy_1d(int32_t channel, void *dst, int32_t dst_base, void *src, int32_t src_base, int32_t extent, int32_t item_size);
extern int32_t halide_xtensa_copy_2d(int32_t channel, void *dst, int32_t dst_base, int32_t dst_stride, void *src, int32_t src_base, int32_t src_stride, int32_t extent0, int32_t extent1, int32_t item_size);
extern int32_t halide_xtensa_wait_for_copy(int32_t channel);
extern int32_t halide_release_dma(int32_t channel_count, void **dma_desc);

#ifdef __cplusplus
}  // extern "C"
#endif

class ScopedDmaInitializer {
    int channel_count_;
    void **dma_desc_ = nullptr;

public:
    ScopedDmaInitializer(int channel_count)
        : channel_count_(channel_count) {
        dma_desc_ = halide_init_dma(channel_count_);
    }

    ScopedDmaInitializer() = delete;
    ScopedDmaInitializer(const ScopedDmaInitializer &) = delete;
    ScopedDmaInitializer &operator=(const ScopedDmaInitializer &) = delete;
    ScopedDmaInitializer(ScopedDmaInitializer &&) = delete;

    ~ScopedDmaInitializer() {
        if (dma_desc_ != nullptr) {
            halide_release_dma(channel_count_, dma_desc_);
        }
    }

    bool is_valid() const {
        return dma_desc_ != nullptr;
    }
};

// TODO(aelphy): xtensa compiler produces sub-optimal results with the default C
// implementation
namespace {
class HalideXtensaFreeHelper {
    typedef void (*FreeFunction)(void *user_context, void *p);
    void *user_context;
    void *p;
    FreeFunction free_function;

public:
    HalideXtensaFreeHelper(
        void *user_context, void *p, FreeFunction free_function)
        : user_context(user_context), p(p), free_function(free_function) {
    }
    ~HalideXtensaFreeHelper() {
        free();
    }
    void free() {
        if (p) {
            // TODO: do all free_functions guarantee to ignore a nullptr?
            free_function(user_context, p);
            p = nullptr;
        }
    }
};

}  // namespace
