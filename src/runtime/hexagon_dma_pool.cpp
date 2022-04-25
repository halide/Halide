#include "hexagon_dma_pool.h"
#include "HalideRuntime.h"
#include "mini_hexagon_dma.h"
#include "printer.h"
#include "scoped_mutex_lock.h"

namespace Halide {
namespace Runtime {
namespace Internal {
namespace HexagonDma {

#define MAX_NUMBER_OF_DMA_ENGINES 8
#define MAX_NUMBER_OF_WORK_UNITS 4

typedef struct {
    bool in_use;
    uint8_t num_of_engines;
    uint8_t mapped_engines[MAX_NUMBER_OF_WORK_UNITS];
} hexagon_dma_virtual_engine_t;

typedef struct {
    bool used;      // DMA Engine is assigned to a virtual engine and in current use
    bool assigned;  // DMA Engine is assigned to a virtual engine
    void *engine_addr;
} hexagon_dma_engine_t;

typedef struct {
    hexagon_dma_engine_t dma_engine_list[MAX_NUMBER_OF_DMA_ENGINES];
    hexagon_dma_virtual_engine_t virtual_engine_list[MAX_NUMBER_OF_DMA_ENGINES];
} hexagon_dma_pool_t;

WEAK hexagon_dma_pool_t *hexagon_dma_pool = nullptr;
WEAK halide_mutex hexagon_dma_pool_mutex;

}  // namespace HexagonDma
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

using namespace Halide::Runtime::Internal::HexagonDma;

namespace {

// In this function we pick the dma engine and assign it to a virtual engine
inline void *hexagon_dma_pool_get(void *user_context, void *virtual_engine_id) {
    halide_abort_if_false(user_context, hexagon_dma_pool);
    halide_abort_if_false(user_context, virtual_engine_id);
    ScopedMutexLock lock(&hexagon_dma_pool_mutex);

    hexagon_dma_virtual_engine_t *virtual_engine_addr = (hexagon_dma_virtual_engine_t *)virtual_engine_id;
    // Walk through the real dma engines assigned to the virtual engine and check if 'used' flag is set to false
    for (int j = 0; j < virtual_engine_addr->num_of_engines; j++) {
        int dma_id = virtual_engine_addr->mapped_engines[j] - 1;
        if ((dma_id != -1) && (hexagon_dma_pool->dma_engine_list[dma_id].used == false)) {
            hexagon_dma_pool->dma_engine_list[dma_id].used = true;
            return hexagon_dma_pool->dma_engine_list[dma_id].engine_addr;
        }
    }

    //  If the number of dma engines assigned to virtual engines is less than MAX_NUMBER_OF_WORK_UNITS,
    //  assign a real engine to a virtual engine
    if (virtual_engine_addr->num_of_engines < MAX_NUMBER_OF_WORK_UNITS) {
        for (int j = 0; j < MAX_NUMBER_OF_DMA_ENGINES; j++) {
            if (hexagon_dma_pool->dma_engine_list[j].assigned == false) {
                hexagon_dma_pool->dma_engine_list[j].assigned = true;
                virtual_engine_addr->mapped_engines[virtual_engine_addr->num_of_engines] = j + 1;
                if (!hexagon_dma_pool->dma_engine_list[j].engine_addr) {
                    hexagon_dma_pool->dma_engine_list[j].engine_addr = (void *)hDmaWrapper_AllocDma();
                    halide_abort_if_false(user_context, hexagon_dma_pool->dma_engine_list[j].engine_addr);
                }
                virtual_engine_addr->num_of_engines++;
                return hexagon_dma_pool->dma_engine_list[j].engine_addr;
            }
        }
    }
    error(user_context) << "Hexagon: Error in assigning a dma engine to a virtual engine\n";
    return nullptr;
}

// In this function we simply mark the dma engine as free
inline int hexagon_dma_pool_put(void *user_context, void *dma_engine, void *virtual_engine_id) {
    halide_abort_if_false(user_context, virtual_engine_id);
    ScopedMutexLock lock(&hexagon_dma_pool_mutex);

    hexagon_dma_virtual_engine_t *virtual_engine_addr = (hexagon_dma_virtual_engine_t *)virtual_engine_id;
    for (int j = 0; j < virtual_engine_addr->num_of_engines; j++) {
        int dma_id = virtual_engine_addr->mapped_engines[j] - 1;
        if ((dma_id != -1) && (hexagon_dma_pool->dma_engine_list[dma_id].engine_addr == dma_engine)) {
            hexagon_dma_pool->dma_engine_list[dma_id].used = false;
            return halide_error_code_success;
        }
    }
    error(user_context) << "Hexagon: Error in freeing a dma engine from a virtual engine\n";
    return halide_error_code_generic_error;
}

}  // namespace

extern "C" {

// halide_hexagon_free_dma_resource
WEAK int halide_hexagon_free_dma_resource(void *user_context, void *virtual_engine_id) {
    halide_abort_if_false(user_context, hexagon_dma_pool);
    halide_abort_if_false(user_context, virtual_engine_id);
    // Free the Real DMA Engines
    int nRet = halide_error_code_success;

    // Lock the Mutex
    ScopedMutexLock lock(&hexagon_dma_pool_mutex);
    hexagon_dma_virtual_engine_t *virtual_engine_addr = (hexagon_dma_virtual_engine_t *)virtual_engine_id;
    for (uint8_t &mapped_engine : virtual_engine_addr->mapped_engines) {
        int num = mapped_engine - 1;
        if (num != -1) {
            hexagon_dma_pool->dma_engine_list[num].assigned = false;
            hexagon_dma_pool->dma_engine_list[num].used = false;
            if (hexagon_dma_pool->dma_engine_list[num].engine_addr) {
                nDmaWrapper_FinishFrame(hexagon_dma_pool->dma_engine_list[num].engine_addr);
            }
        }
        mapped_engine = 0;
    }
    virtual_engine_addr->num_of_engines = 0;
    virtual_engine_addr->in_use = false;

    bool delete_dma_pool = true;
    for (const auto &engine : hexagon_dma_pool->virtual_engine_list) {
        if (engine.in_use) {
            delete_dma_pool = false;
        }
    }

    if (delete_dma_pool) {
        for (const auto &engine : hexagon_dma_pool->dma_engine_list) {
            if (engine.engine_addr) {
                int err = nDmaWrapper_FreeDma((t_DmaWrapper_DmaEngineHandle)engine.engine_addr);
                if (err != QURT_EOK) {
                    error(user_context) << "Hexagon: Failure to Free DMA\n";
                    nRet = err;
                }
            }
        }
        free(hexagon_dma_pool);
        hexagon_dma_pool = nullptr;

        // Free cache pool
        int err = halide_hexagon_free_l2_pool(user_context);
        if (err != 0) {
            error(user_context) << "Hexagon: Failure to free Cache Pool\n";
            nRet = err;
        }
    }
    return nRet;
}

// halide_hexagon_create_dma_pool
WEAK void *halide_hexagon_allocate_dma_resource(void *user_context) {
    ScopedMutexLock lock(&hexagon_dma_pool_mutex);

    if (!hexagon_dma_pool) {
        hexagon_dma_pool = (hexagon_dma_pool_t *)malloc(sizeof(hexagon_dma_pool_t));
        for (int i = 0; i < MAX_NUMBER_OF_DMA_ENGINES; i++) {
            hexagon_dma_pool->dma_engine_list[i].used = false;
            hexagon_dma_pool->dma_engine_list[i].engine_addr = nullptr;
            hexagon_dma_pool->dma_engine_list[i].assigned = false;
            hexagon_dma_pool->virtual_engine_list[i].in_use = false;
            for (uint8_t &engine : hexagon_dma_pool->virtual_engine_list[i].mapped_engines) {
                engine = 0;
            }
            hexagon_dma_pool->virtual_engine_list[i].num_of_engines = 0;
        }
    }

    for (auto &engine : hexagon_dma_pool->virtual_engine_list) {
        if (engine.in_use == false) {
            engine.in_use = true;
            void *virtual_addr = &engine;
            return (void *)virtual_addr;
        }
    }

    error(user_context) << "Hexagon: Failed to allocate engine\n";
    return nullptr;
}

WEAK void *halide_hexagon_allocate_from_dma_pool(void *user_context, void *virtual_engine_id) {
    return hexagon_dma_pool_get(user_context, virtual_engine_id);
}

WEAK int halide_hexagon_free_to_dma_pool(void *user_context, void *dma_engine, void *virtual_engine_id) {
    return hexagon_dma_pool_put(user_context, dma_engine, virtual_engine_id);
}
}
