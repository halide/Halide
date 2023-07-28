#include <cassert>
#include <memory>
#include <mutex>
#include <vector>

#include "HexagonWrapper.h"

#include "sim_protocol.h"

#ifdef _MSC_VER
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

typedef unsigned int handle_t;

std::unique_ptr<HexagonWrapper> sim;

bool debug_mode = false;

int init_sim() {
    if (sim) {
        return 0;
    }

    sim = std::make_unique<HexagonWrapper>(HEX_CPU_V65);

    HEXAPI_Status status = HEX_STAT_SUCCESS;

    // If an explicit path to the simulator is provided, use it.
    const char *sim_remote_path = getenv("HL_HEXAGON_SIM_REMOTE");
    if (!sim_remote_path || !sim_remote_path[0]) {
        // Otherwise... just assume that something with this name will be
        // available in the working directory, I guess.
        sim_remote_path = "hexagon_sim_remote";
    }
    status = sim->ConfigureExecutableBinary(sim_remote_path);
    if (status != HEX_STAT_SUCCESS) {
        printf("HexagonWrapper::ConfigureExecutableBinary failed: %d\n", status);
        return -1;
    }

    status = sim->ConfigureNULLPointerBehavior(HEX_NULLPTR_FATAL);
    if (status != HEX_STAT_SUCCESS) {
        printf("HexagonWrapper::ConfigureNULLPointerBehavior failed: %d\n", status);
        return -1;
    }

    // memfill defaults to 0x1f on the simulator.
    // example: HL_HEXAGON_MEMFILL=0
    const char *memfill = getenv("HL_HEXAGON_MEMFILL");
    if (memfill && memfill[0] != 0) {
        status = sim->ConfigureMemFill(atoi(memfill));
        if (status != HEX_STAT_SUCCESS) {
            printf("HexagonWrapper::ConfigureMemFill failed: %d\n", status);
            return -1;
        }
    }

    const char *timing = getenv("HL_HEXAGON_TIMING");
    if (timing && timing[0] != 0) {
        status = sim->ConfigureTimingMode(HEX_TIMING);
        if (status != HEX_STAT_SUCCESS) {
            printf("HexagonWrapper::ConfigureTimingMode failed: %d\n", status);
            return -1;
        }
    }

    // Configue various tracing capabilites.
    struct Trace {
        const char *env_var;
        HEXAPI_TracingType hex_trace;
    };
    Trace traces[] = {
        {"HL_HEXAGON_SIM_MIN_TRACE", HEX_TRACE_PC_MIN},
        {"HL_HEXAGON_SIM_TRACE", HEX_TRACE_PC},
        {"HL_HEXAGON_SIM_MEM_TRACE", HEX_TRACE_MEM},
    };
    for (auto i : traces) {
        const char *trace = getenv(i.env_var);
        if (trace && trace[0] != 0) {
            status = sim->SetTracing(i.hex_trace, trace);
            if (status != HEX_STAT_SUCCESS) {
                printf("HexagonWrapper::SetTracing failed: %d\n", status);
                return -1;
            }
        }
    }

    // Configure use of debugger
    int pnum = 0;
    const char *s = getenv("HL_HEXAGON_SIM_DBG_PORT");
    if (s && (pnum = atoi(s))) {
        printf("Debugger port: %d\n", pnum);
        status = sim->ConfigureRemoteDebug(pnum);
        if (status != HEX_STAT_SUCCESS) {
            printf("HexagonWrapper::ConfigureRemoteDebug failed: %d\n", status);
            return -1;
        } else {
            debug_mode = true;
        }
    }

    // Configure packet analysis for hexagon-prof.
    const char *packet_analyze = getenv("HL_HEXAGON_PACKET_ANALYZE");
    if (packet_analyze && packet_analyze[0]) {
        status = sim->ConfigurePacketAnalysis(packet_analyze);
        if (status != HEX_STAT_SUCCESS) {
            printf("HexagonWrapper::ConfigurePacketAnalysis failed: %d\n", status);
            return -1;
        }
    }

    // Control use of dlopenbuf. This is to enable testing of the
    // custom implementation of dlopen, which is not used whenever
    // dlopenbuf is available.
    status = sim->EndOfConfiguration();
    if (status != HEX_STAT_SUCCESS) {
        printf("HexagonWrapper::EndOfConfiguration failed: %d\n", status);
        return -1;
    }

    status = sim->LoadExecutableBinary();
    if (status != HEX_STAT_SUCCESS) {
        printf("HexagonWrapper::LoadExecutableBinary failed: %d\n", status);
        return -1;
    }
    return 0;
}

int write_memory(int dest, const void *src, int size) {
    assert(sim);

    while (size > 0) {
        // WriteMemory only works with powers of 2, so align down. It
        // also only writes up to 8 bytes, so we need to do this
        // repeatedly until we've finished copying the buffer.
        HEX_8u_t src_chunk;
        int chunk_size;
        if (size >= 8) {
            src_chunk = *reinterpret_cast<const HEX_8u_t *>(src);
            chunk_size = 8;
        } else if (size >= 4) {
            src_chunk = *reinterpret_cast<const HEX_4u_t *>(src);
            chunk_size = 4;
        } else if (size >= 2) {
            src_chunk = *reinterpret_cast<const HEX_2u_t *>(src);
            chunk_size = 2;
        } else {
            src_chunk = *reinterpret_cast<const HEX_1u_t *>(src);
            chunk_size = 1;
        }
        HEXAPI_Status status = sim->WriteMemory(dest, chunk_size, src_chunk);
        if (status != HEX_STAT_SUCCESS) {
            printf("HexagonWrapper::WriteMemory failed: %d\n", status);
            return -1;
        }

        size -= chunk_size;
        dest += chunk_size;
        src = reinterpret_cast<const char *>(src) + chunk_size;
    }
    return 0;
}

int read_memory(void *dest, int src, int size) {
    assert(sim);

    while (size > 0) {
        // This is the same logic as in write_memory above.
        int next = 1;
        if (size >= 8) {
            next = 8;
        } else if (size >= 4) {
            next = 4;
        } else if (size >= 2) {
            next = 2;
        }
        HEXAPI_Status status = sim->ReadMemory(src, next, dest);
        if (status != HEX_STAT_SUCCESS) {
            printf("HexagonWrapper::ReadMemory failed: %d\n", status);
            return -1;
        }

        size -= next;
        src += next;
        dest = reinterpret_cast<char *>(dest) + next;
    }
    return 0;
}

// A frequently-updated local copy of the remote profiler state.
int profiler_current_func;

int send_message(int msg, const std::vector<int> &arguments) {
    assert(sim);

    HEXAPI_Status status;

    HEX_4u_t remote_msg = 0;
    status = sim->ReadSymbolValue("rpc_call", &remote_msg);
    if (status != HEX_STAT_SUCCESS) {
        printf("HexagonWrapper::ReadSymbolValue(rpcmsg) failed: %d\n", status);
        return -1;
    }
    if (write_memory(remote_msg, &msg, 4) != 0) {
        return -1;
    }

    // The arguments are individual numbered variables.
    for (size_t i = 0; i < arguments.size(); i++) {
        HEX_4u_t remote_arg = 0;
        std::string rpc_arg = "rpc_arg" + std::to_string(i);
        status = sim->ReadSymbolValue(rpc_arg.c_str(), &remote_arg);
        if (status != HEX_STAT_SUCCESS) {
            printf("HexagonWrapper::ReadSymbolValue(%s) failed: %d\n", rpc_arg.c_str(), status);
            return -1;
        }
        if (write_memory(remote_arg, &arguments[i], 4) != 0) {
            return -1;
        }
    }

    HEX_4u_t remote_ret = 0;
    status = sim->ReadSymbolValue("rpc_ret", &remote_ret);
    if (status != HEX_STAT_SUCCESS) {
        printf("HexagonWrapper::ReadSymbolValue(rpc_ret) failed: %d\n", status);
        return -1;
    }
    // Get the remote address of the current func. There's a remote
    // pointer to it, so we need to walk through a few levels of
    // indirection.
    HEX_4u_t remote_profiler_current_func_addr_addr = 0;
    HEX_4u_t remote_profiler_current_func_addr = 0;
    status = sim->ReadSymbolValue("profiler_current_func_addr",
                                  &remote_profiler_current_func_addr_addr);
    if (status != HEX_STAT_SUCCESS) {
        printf("HexagonWrapper::ReadSymbolValue(profiler_current_func_addr) failed: %d\n", status);
        return -1;
    }
    if (read_memory(&remote_profiler_current_func_addr,
                    remote_profiler_current_func_addr_addr,
                    sizeof(HEX_4u_t))) {
        return -1;
    }

    HEXAPI_CoreState state;
    // If we are debugging using LLDB, then we cannot use sim->Step, but
    // we need to use sim->Run to allow LLDB to take over.
    if (msg == Message::Break || (debug_mode && (msg == Message::Run))) {
        // If we're trying to end the remote simulation, just run
        // until completion.
        HEX_4u_t result;
        state = sim->Run(&result);
        if (state != HEX_CORE_FINISHED) {
            printf("HexagonWrapper::Run failed: %d\n", state);
            return -1;
        }
        return 0;
    } else {
        // If we want to return and continue simulating, we execute
        // 1000 cycles at a time, until the remote indicates it has
        // completed handling the current message.
        do {
            HEX_4u_t cycles;
            state = sim->Step(1000, &cycles);
            if (read_memory(&msg, remote_msg, 4) != 0) {
                return -1;
            }
            if (msg == Message::None) {
                HEX_4u_t ret = 0;
                if (read_memory(&ret, remote_ret, 4)) {
                    return -1;
                }
                return ret;
            }

            // Grab the remote profiler state in case we're profiling
            read_memory(&profiler_current_func, remote_profiler_current_func_addr, sizeof(int));
        } while (state == HEX_CORE_SUCCESS);
        printf("HexagonWrapper::StepTime failed: %d\n", state);
        return -1;
    }
}

struct host_buffer {
    unsigned char *data;
    int dataLen;
};

class remote_buffer {
public:
    int data;
    int dataLen;

    remote_buffer()
        : data(0), dataLen(0) {
    }
    remote_buffer(int dataLen)
        : dataLen(dataLen) {
        if (dataLen > 0) {
            data = send_message(Message::Alloc, {dataLen});
            if (data == 0) {
                printf("Failed to allocate %d bytes in the Hexagon simulation.\n", dataLen);
            }
        } else {
            data = 0;
        }
    }
    remote_buffer(const void *data, int dataLen)
        : remote_buffer(dataLen) {
        if (this->data != 0) {
            // Return value ignored, this is a constructor, we don't
            // have exceptions, and we already printed an error.
            write_memory(this->data, data, dataLen);
        }
    }
    remote_buffer(const host_buffer &host_buf)
        : remote_buffer(host_buf.data, host_buf.dataLen) {
    }

    ~remote_buffer() {
        if (data != 0) {
            send_message(Message::Free, {data});
        }
    }

    // Enable usage with std::vector.
    remote_buffer(remote_buffer &&move) noexcept
        : remote_buffer() {
        std::swap(data, move.data);
        std::swap(dataLen, move.dataLen);
    }
    remote_buffer &operator=(remote_buffer &&move) noexcept {
        std::swap(data, move.data);
        std::swap(dataLen, move.dataLen);
        return *this;
    }

    remote_buffer(const remote_buffer &) = delete;
    remote_buffer &operator=(const remote_buffer &) = delete;
};

// We need to only allow one thread at a time to interact with the runtime.
// This is done by simply locking this mutex at the entry of each exported
// runtime function. This is not very efficient, but the simulator is slow
// anyways.
std::mutex mutex;

extern "C" {

DLLEXPORT
int halide_hexagon_remote_load_library(const char *soname, int sonameLen, const unsigned char *code, int codeLen, handle_t *module_ptr) {
    std::lock_guard<std::mutex> guard(mutex);

    int ret = init_sim();
    if (ret != 0) {
        return -1;
    }

    // Copy the pointer arguments to the simulator.
    remote_buffer remote_soname(soname, sonameLen);
    remote_buffer remote_code(code, codeLen);
    remote_buffer remote_module_ptr(module_ptr, 4);

    // Run the init kernels command.
    ret = send_message(Message::LoadLibrary, {remote_soname.data, sonameLen, remote_code.data, codeLen, remote_module_ptr.data});
    if (ret != 0) {
        return ret;
    }

    // Get the module ptr.
    ret = read_memory(module_ptr, remote_module_ptr.data, 4);

    return ret;
}

DLLEXPORT
int halide_hexagon_remote_get_symbol_v4(handle_t module_ptr, const char *name, int nameLen, handle_t *sym) {
    std::lock_guard<std::mutex> guard(mutex);

    assert(sim);

    // Copy the pointer arguments to the simulator.
    remote_buffer remote_name(name, nameLen);

    // Run the init kernels command.
    *sym = send_message(Message::GetSymbol, {static_cast<int>(module_ptr), remote_name.data, nameLen});

    return *sym != 0 ? 0 : -1;
}

DLLEXPORT
int halide_hexagon_remote_run(handle_t module_ptr, handle_t function,
                              const host_buffer *input_buffersPtrs, int input_buffersLen,
                              host_buffer *output_buffersPtrs, int output_buffersLen,
                              const host_buffer *input_scalarsPtrs, int input_scalarsLen) {
    std::lock_guard<std::mutex> guard(mutex);

    assert(sim);

    std::vector<remote_buffer> remote_input_buffers;
    std::vector<remote_buffer> remote_output_buffers;
    std::vector<remote_buffer> remote_input_scalars;

    for (int i = 0; i < input_buffersLen; i++) {
        remote_input_buffers.emplace_back(input_buffersPtrs[i]);
    }
    for (int i = 0; i < output_buffersLen; i++) {
        remote_output_buffers.emplace_back(output_buffersPtrs[i]);
    }
    for (int i = 0; i < input_scalarsLen; i++) {
        remote_input_scalars.emplace_back(input_scalarsPtrs[i]);
    }

    // Copy the pointer arguments to the simulator.
    remote_buffer remote_input_buffersPtrs(input_buffersLen ? remote_input_buffers.data() : nullptr, input_buffersLen * sizeof(remote_buffer));
    remote_buffer remote_output_buffersPtrs(output_buffersLen ? remote_output_buffers.data() : nullptr, output_buffersLen * sizeof(remote_buffer));
    remote_buffer remote_input_scalarsPtrs(input_scalarsLen ? remote_input_scalars.data() : nullptr, input_scalarsLen * sizeof(remote_buffer));

    HEX_8u_t cycles_begin = 0;
    sim->GetSimulatedCycleCount(&cycles_begin);

    // Run the init kernels command.
    int ret = send_message(
        Message::Run,
        {static_cast<int>(module_ptr), static_cast<int>(function),
         remote_input_buffersPtrs.data, input_buffersLen,
         remote_output_buffersPtrs.data, output_buffersLen,
         remote_input_scalarsPtrs.data, input_scalarsLen});
    if (ret != 0) {
        return ret;
    }

    HEX_8u_t cycles_end = 0;
    sim->GetSimulatedCycleCount(&cycles_end);

    if (getenv("HL_HEXAGON_SIM_CYCLES")) {
        int cycles = static_cast<int>(cycles_end - cycles_begin);
        printf("Hexagon simulator executed function 0x%x in %d cycles\n", function, cycles);
    }

    // Copy the outputs back.
    for (int i = 0; i < output_buffersLen; i++) {
        ret = read_memory(output_buffersPtrs[i].data, remote_output_buffers[i].data, output_buffersPtrs[i].dataLen);
        if (ret != 0) {
            return ret;
        }
    }

    return ret;
}

DLLEXPORT
int halide_hexagon_remote_release_library(handle_t module_ptr) {
    std::lock_guard<std::mutex> guard(mutex);

    if (!sim) {
        // Due to static destructor ordering issues, the simulator
        // might have been freed before this gets called.
        return 0;
    }
    // Print out sim statistics if desired.
    if (getenv("HL_HEXAGON_SIM_STATS")) {
        char Buf[4096];
        HEXAPI_Status status = sim->EmitPerfStatistics(0, 0, 0, 0, Buf, sizeof(Buf));
        if (status != HEX_STAT_SUCCESS) {
            printf("HexagonWrapper::EmitStatistics failed: %d\n", status);
        } else {
            // Print out the full stats
            printf("%s\n", Buf);
        }
    }
    return send_message(Message::ReleaseLibrary, {static_cast<int>(module_ptr)});
}

DLLEXPORT
void halide_hexagon_host_malloc_init() {
}

DLLEXPORT
void halide_hexagon_host_malloc_deinit() {
}

DLLEXPORT
void *halide_hexagon_host_malloc(size_t x) {
    // Allocate enough space for aligning the pointer we return.
    const size_t alignment = 4096;
    void *orig = malloc(x + alignment);
    if (orig == nullptr) {
        return nullptr;
    }

    // We want to store the original pointer prior to the pointer we return.
    void *ptr = (void *)(((size_t)orig + alignment + sizeof(void *) - 1) & ~(alignment - 1));
    ((void **)ptr)[-1] = orig;
    return ptr;
}

DLLEXPORT
void halide_hexagon_host_free(void *ptr) {
    free(((void **)ptr)[-1]);
}

DLLEXPORT
int halide_hexagon_remote_poll_profiler_state(int *func, int *threads) {
    // The stepping code periodically grabs the remote value of
    // current_func for us.
    *func = profiler_current_func;
    *threads = 1;
    return 0;
}
DLLEXPORT
int halide_hexagon_remote_profiler_set_current_func(int current_func) {
    profiler_current_func = current_func;
    return 0;
}
}  // extern "C"
