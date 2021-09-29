#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char uint8_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef __SIZE_TYPE__ size_t;

extern void *tcm_alloc_on_bank(size_t size, unsigned char alignment, unsigned char bank);
extern void tcm_free(void *ptr);

// NOTE(vksnk): original definition has WEAK in front of it, but xtensa linker
// doesn't seem to handle it correctly.
int halide_malloc_alignment();

void *halide_tcm_malloc(void *user_context, unsigned int x) {
    const size_t alignment = halide_malloc_alignment();
    void* ptr = tcm_alloc_on_bank(x, alignment, /*bank=*/0);
    // Try to allocate on the second bank.
    if (!ptr) {
      ptr = tcm_alloc_on_bank(x, alignment, /*bank=*/1);
    }
    return ptr;
}

void halide_tcm_free(void *user_context, void *ptr) {
    tcm_free(ptr);
}

struct idma_buffer_t;

typedef enum {
    IDMA_1D_DESC = 1,
    IDMA_2D_DESC = 2,
    IDMA_64B_DESC = 4
} idma_type_t;

typedef enum {
    IDMA_ERR_NO_BUF = -40,      /* No valid ring buffer */
    IDMA_ERR_BAD_DESC = -20,    /* Descriptor not correct */
    IDMA_ERR_BAD_CHAN,          /* Invalid channel number */
    IDMA_ERR_NOT_INIT,          /* iDMAlib and HW not initialized  */
    IDMA_ERR_TASK_NOT_INIT,     /* Cannot scheduled uninitialized task  */
    IDMA_ERR_BAD_TASK,          /* Task not correct  */
    IDMA_ERR_BUSY,              /* iDMA busy when not expected */
    IDMA_ERR_IN_SPEC_MODE,      /* iDMAlib in unexpected mode */
    IDMA_ERR_NOT_SPEC_MODE,     /* iDMAlib in unexpected mode */
    IDMA_ERR_TASK_EMPTY,        /* No descs in the task/buffer */
    IDMA_ERR_TASK_OUTSTAND_NEG, /* Number of outstanding descs is a negative value  */
    IDMA_ERR_TASK_IN_ERROR,     /* Task in error */
    IDMA_ERR_BUFFER_IN_ERROR,   /* Buffer in error */
    IDMA_ERR_NO_NEXT_TASK,      /* Next task to process is missing  */
    IDMA_ERR_BUF_OVFL,          /* Attempt to schedule too many descriptors */
    IDMA_ERR_HW_ERROR,          /* HW error detected */
    IDMA_ERR_BAD_INIT,          /* Bad idma_init args */
    IDMA_OK = 0,                /* No error */
    IDMA_CANT_SLEEP = 1,        /* Cannot sleep (no pending descriptors) */
} idma_status_t;

typedef void (*idma_callback_fn)(void *arg);

#define DESC_IDMA_PRIOR_H 0x08000 /* QoS high */

idma_status_t
idma_init_loop(int32_t ch,
               idma_buffer_t *bufh,
               idma_type_t type,
               int32_t ndescs,
               void *cb_data,
               idma_callback_fn cb_func);

int32_t
idma_copy_desc(int32_t ch,
               void *dst,
               void *src,
               size_t size,
               uint32_t flags);

int32_t idma_buffer_status(int32_t ch);

idma_status_t idma_sleep(int32_t ch);

idma_buffer_t *gxp_idma_descriptor_alloc(idma_type_t type, int count);
void gxp_idma_descriptor_free(idma_buffer_t *buffer);

void DmaCallback(void *data) {
}

static idma_buffer_t *dma_desc = nullptr;
int halide_init_dma() {
    dma_desc = gxp_idma_descriptor_alloc(IDMA_1D_DESC, /*count=*/2);
    if (!dma_desc) {
        return -1;
    }

    constexpr int kDmaCh = 0;  // DMA Channel.
    idma_status_t init_status =
        idma_init_loop(kDmaCh, dma_desc, IDMA_1D_DESC, 2, nullptr, &DmaCallback);
    return init_status;
}

void halide_release_dma() {
    gxp_idma_descriptor_free(dma_desc);
}

int32_t halide_xtensa_copy_1d(void *dst, int32_t dst_base, void *src, int32_t src_base, int extent, int item_size) {
    return idma_copy_desc(0, (uint8_t *)dst + dst_base * item_size, (uint8_t *)src + src_base * item_size, extent * item_size, DESC_IDMA_PRIOR_H);
}

int32_t halide_xtensa_wait_for_copy(int32_t id) {
    while (idma_buffer_status(0) > 0) {
        idma_sleep(0);
    }

    return 0;
}

#ifdef __cplusplus
}  // extern "C"
#endif
