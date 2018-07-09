#include "HalideRuntime.h"
#include "mini_hexagon_dma.h"
#include "hexagon_dma_pool.h"

namespace Halide { namespace Runtime { namespace Internal { namespace HexagonDma {

#define MAX_NUMBER_OF_DMA_ENGINES 8
#define MAX_NUMBER_OF_WORK_UNITS 4

typedef struct {
    bool in_use;
    uint8_t num_of_engines;
    uint8_t mapped_engines[MAX_NUMBER_OF_WORK_UNITS];
} hexagon_virtual_engine_t;

typedef struct {
    bool used;
    bool assigned;
    void *engine_addr;
} hexagon_dma_engine_t;

typedef struct {
hexagon_dma_engine_t dma_engine_list[MAX_NUMBER_OF_DMA_ENGINES];
hexagon_virtual_engine_t virtual_engine_list[MAX_NUMBER_OF_DMA_ENGINES];
} hexagon_dma_pool_t;

hexagon_dma_pool_t *pdma_pool = NULL;

// In this function we pick the dma engine and assign it to a virtual engine
static inline void *hexagon_dma_pool_get (void *user_context, void *virtual_engine_id) {
    halide_assert(user_context, pdma_pool);
    halide_assert(user_context, virtual_engine_id);
    hexagon_virtual_engine_t *virtual_engine_addr = (hexagon_virtual_engine_t *)virtual_engine_id;

    // Seatch the virtual engine address if assigned dma are free
    for (int j=0; j < virtual_engine_addr->num_of_engines; j++) {
        int dma_id = virtual_engine_addr->mapped_engines[j]-1;
        if ((dma_id != -1) && (pdma_pool->dma_engine_list[dma_id].used == false)) {
            pdma_pool->dma_engine_list[dma_id].used = true;
            return  pdma_pool->dma_engine_list[dma_id].engine_addr;
        }
    }

    //  If ssigned DMAa are less than 4 add a new one
    if ( virtual_engine_addr->num_of_engines <= MAX_NUMBER_OF_WORK_UNITS ) {
        for (int j=0; j < MAX_NUMBER_OF_DMA_ENGINES; j++) {
            if (pdma_pool->dma_engine_list[j].assigned == false) {
                pdma_pool->dma_engine_list[j].assigned = true;
                virtual_engine_addr->mapped_engines[virtual_engine_addr->num_of_engines] = j+1;
                if (!pdma_pool->dma_engine_list[j].engine_addr) {
                    pdma_pool->dma_engine_list[j].engine_addr = (void *)hDmaWrapper_AllocDma();
                }
                virtual_engine_addr->num_of_engines++;
                return  pdma_pool->dma_engine_list[j].engine_addr;
            }
        }
    }
    halide_print(user_context, "Hexagon DMA: Error in assigning a dma engine to a virtual engine\n");
    return NULL;
}
// In this function we simply mark the dma engine as free
static inline int hexagon_dma_pool_put(void *user_context, void *dma_engine, void *virtual_engine_id) {
    halide_assert(user_context, virtual_engine_id);
    hexagon_virtual_engine_t *virtual_engine_addr = (hexagon_virtual_engine_t *)virtual_engine_id;

    for (int j=0; j < virtual_engine_addr->num_of_engines; j++) {
        int dma_id = virtual_engine_addr->mapped_engines[j]-1;
        if((dma_id != -1) && (pdma_pool->dma_engine_list[dma_id].engine_addr == dma_engine)) {
            pdma_pool->dma_engine_list[dma_id].used = false;
            return halide_error_code_success;
        }
    }
    return halide_error_code_generic_error;
}

}}}}

using namespace Halide::Runtime::Internal::HexagonDma;

extern "C" {
// halide_hexagon_free_dma_engine
void halide_hexagon_free_dma_engine(void *user_context, void *virtual_engine_id) {
    halide_assert(user_context, pdma_pool);
    halide_assert(user_context, virtual_engine_id);
    hexagon_virtual_engine_t *virtual_engine_addr = (hexagon_virtual_engine_t *)virtual_engine_id;

    for (int j=0; j < MAX_NUMBER_OF_WORK_UNITS; j++) {
        int num = virtual_engine_addr->mapped_engines[j] - 1;
        if (num != -1) {
            pdma_pool->dma_engine_list[num].assigned = false;
            pdma_pool->dma_engine_list[num].used = false;
            //nDmaWrapper_FinishFrame(pdma_pool->dma_engine_list[num].engine_addr);
        }
        virtual_engine_addr->mapped_engines[j] = 0;
    }
    virtual_engine_addr->num_of_engines = 0;
    virtual_engine_addr->in_use = false;
}

// halide_hexagon_create_dma_pool
void *halide_hexagon_create_dma_pool(void *user_context) {
    if (!pdma_pool) {
        pdma_pool = (hexagon_dma_pool_t *) malloc (sizeof(hexagon_dma_pool_t));
        for (int i=0; i < MAX_NUMBER_OF_DMA_ENGINES; i++) {
            pdma_pool->dma_engine_list[i].used = false;
            pdma_pool->dma_engine_list[i].engine_addr = NULL;
            pdma_pool->dma_engine_list[i].assigned = false;
            pdma_pool->virtual_engine_list[i].in_use = false;
            for (int j=0; j < MAX_NUMBER_OF_WORK_UNITS; j++) {
                pdma_pool->virtual_engine_list[i].mapped_engines[j] = 0;
            }
            pdma_pool->virtual_engine_list[i].num_of_engines = 0;
        }
    }

    // halide_hexagon_max_number_of_dma_engines
    for (int i=0; i < MAX_NUMBER_OF_DMA_ENGINES; i++) {
        if (pdma_pool->virtual_engine_list[i].in_use == false) {
            pdma_pool->virtual_engine_list[i].in_use = true;
            void *virtual_addr =  &(pdma_pool->virtual_engine_list[i]);
            return (void *) virtual_addr;
        }
    }
    return NULL;
}

//halide_hexagon_free_dma_pool
WEAK int halide_hexagon_delete_dma_pool(void *user_context) {
    halide_assert(user_context, pdma_pool);
    //Free the Real DMA Engines
    int nRet = halide_error_code_success;
    for (int i=0; i < MAX_NUMBER_OF_DMA_ENGINES; i++) {
        if (pdma_pool->dma_engine_list[i].engine_addr) {
            int err = nDmaWrapper_FreeDma((t_DmaWrapper_DmaEngineHandle)pdma_pool->dma_engine_list[i].engine_addr);
            if (err != QURT_EOK) {
                nRet = err;
            }
        }
    }
    free(pdma_pool);
    pdma_pool = NULL;
    return nRet;
}

WEAK void *halide_hexagon_allocate_from_dma_pool(void *user_context, void *virtual_engine_id) {
    return hexagon_dma_pool_get(user_context, virtual_engine_id);
}

WEAK int halide_hexagon_free_to_dma_pool(void *user_context, void* dma_engine, void *virtual_engine_id) {
    return hexagon_dma_pool_put(user_context, dma_engine, virtual_engine_id);
}

}
