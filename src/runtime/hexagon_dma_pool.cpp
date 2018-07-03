#include "HalideRuntime.h"
#include "mini_hexagon_dma.h"
#include "hexagon_dma_pool.h"

namespace Halide { namespace Runtime { namespace Internal { namespace HexagonDma {

#define MAX_NUMBER_OF_DMA_ENGINES 8

typedef struct hexagon_dma_block {
    void *engineId;
    bool used;
} hexagon_dma_block_t;

typedef struct {
hexagon_dma_block_t dma_list[MAX_NUMBER_OF_DMA_ENGINES];
int desired_num_of_engines;
} hexagon_dma_pool_t;

hexagon_dma_pool_t hexagon_dma_pool; 

static inline void *hexagon_dma_pool_get (void *user_context, void *pool_addr) {
    halide_assert(user_context, pool_addr);  
    hexagon_dma_pool_t *pdma_pool = (hexagon_dma_pool_t *) pool_addr; 
    
    for (int i=0; i < pdma_pool->desired_num_of_engines; i++) {
        if(!pdma_pool->dma_list[i].used) {
            if (pdma_pool->dma_list[i].engineId == NULL) {
                pdma_pool->dma_list[i].engineId = (void *)hDmaWrapper_AllocDma();
                if (!pdma_pool->dma_list[i].engineId) {
                    return NULL;  
                }
            }
            pdma_pool->dma_list[i].used = true; 
            return (void *)pdma_pool->dma_list[i].engineId;
        }     
    }
    return NULL;
}
//hexagon_dma_pool_put
static inline int hexagon_dma_pool_put(void *user_context, void *pool_addr, void *engine_addr) {
    halide_assert(user_context, pool_addr); 
    halide_assert(user_context, engine_addr);

    hexagon_dma_pool_t *pdma_pool = (hexagon_dma_pool_t *) pool_addr;
    for (int i=0; i < pdma_pool->desired_num_of_engines; i++) {
        if (pdma_pool->dma_list[i].engineId == engine_addr) {
            pdma_pool->dma_list[i].used = false;
            return halide_error_code_success;
        }
    }
    return halide_error_code_generic_error;
}

}}}}

using namespace Halide::Runtime::Internal::HexagonDma;

extern "C" { 

//halide_hexagon_allocate_dma_pool
void *halide_hexagon_dma_allocate_pool(void *user_context, int num) {
    halide_assert(user_context, num < MAX_NUMBER_OF_DMA_ENGINES);
    halide_print(user_context, "halide_hexagon_allocate_dma_pool\n");
    hexagon_dma_pool_t *pdma_pool = (hexagon_dma_pool_t *) malloc(sizeof(hexagon_dma_pool_t));
    if (!pdma_pool) {
        halide_print(user_context, "halide_hexagon_allocate_dma_pool failure\n");
        return NULL;
    }
    pdma_pool->desired_num_of_engines = num;
    for (int i=0; i < MAX_NUMBER_OF_DMA_ENGINES; i++) {
        pdma_pool->dma_list[i].engineId = NULL;
        pdma_pool->dma_list[i].used = false;
    }
    return pdma_pool;
}

//halide_hexagon_free_dma_pool
WEAK int halide_hexagon_free_dma_pool(void *user_context, void *pool_addr) {
    halide_print(user_context, "halide_hexagon_free_dma_pool\n");
    halide_assert(user_context, pool_addr);
    hexagon_dma_pool_t *pdma_pool = (hexagon_dma_pool_t *)pool_addr;
    for (int i=0; i < pdma_pool->desired_num_of_engines; i++) {
        if (pdma_pool->dma_list[i].engineId) {
            int err = nDmaWrapper_FreeDma((t_DmaWrapper_DmaEngineHandle)pdma_pool->dma_list[i].engineId);
            if (err != 0) {
                return halide_error_code_generic_error;
            }
        } 
    }
    free(pool_addr);
    return halide_error_code_success;
}

WEAK void *halide_hexagon_allocate_from_dma_pool(void *user_context, void *pool_addr) {
    return hexagon_dma_pool_get(user_context, pool_addr);
}

WEAK int halide_hexagon_free_from_dma_pool(void *user_context, void *pool_addr, void *engine_addr) {
    return hexagon_dma_pool_put(user_context, pool_addr, engine_addr);
}

}

