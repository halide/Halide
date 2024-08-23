#include "Halide.h"
#include "HalideBuffer.h"
#include "HalideRuntime.h"

using namespace Halide;

Halide::Runtime::Buffer<int32_t> make_gpu_buffer(bool hexagon_rpc, int offset = 0,
                                                 DeviceAPI api = DeviceAPI::Default_GPU) {
    Var x, y;
    Func f;
    f(x, y) = x + y * 256 + offset;

    if (hexagon_rpc) {
        f.hexagon();
    } else {
        Var xi, yi;
        f.gpu_tile(x, y, xi, yi, 8, 8, TailStrategy::Auto, api);
    }

    Buffer<int32_t> result = f.realize({128, 128});
    return *result.get();
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();

    bool hexagon_rpc = (target.arch != Target::Hexagon) &&
                       target.has_feature(Target::HVX);

    if (!hexagon_rpc && !target.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    printf("Test copy to device.\n");
    {
        Halide::Runtime::Buffer<int32_t> gpu_buf = make_gpu_buffer(hexagon_rpc);
        assert(gpu_buf.raw_buffer()->device_interface != nullptr);

        Halide::Runtime::Buffer<int32_t> cpu_buf(128, 128);
        cpu_buf.fill(0);
        assert(gpu_buf.raw_buffer()->device_interface->buffer_copy(nullptr, cpu_buf, gpu_buf.raw_buffer()->device_interface, gpu_buf) == 0);

        gpu_buf.copy_to_host();
        for (int i = 0; i < 128; i++) {
            for (int j = 0; j < 128; j++) {
                assert(gpu_buf(i, j) == 0);
            }
        }
    }

    printf("Test copy from device.\n");
    {
        Halide::Runtime::Buffer<int32_t> gpu_buf = make_gpu_buffer(hexagon_rpc);
        assert(gpu_buf.raw_buffer()->device_interface != nullptr);

        Halide::Runtime::Buffer<int32_t> cpu_buf(128, 128);
        assert(gpu_buf.raw_buffer()->device_interface->buffer_copy(nullptr, gpu_buf, nullptr, cpu_buf) == 0);

        for (int i = 0; i < 128; i++) {
            for (int j = 0; j < 128; j++) {
                assert(cpu_buf(i, j) == (i + j * 256));
            }
        }
    }

    printf("Test copy device to device.\n");
    {
        Halide::Runtime::Buffer<int32_t> gpu_buf1 = make_gpu_buffer(hexagon_rpc);
        assert(gpu_buf1.raw_buffer()->device_interface != nullptr);
        Halide::Runtime::Buffer<int32_t> gpu_buf2 = make_gpu_buffer(hexagon_rpc, 256000);
        assert(gpu_buf2.raw_buffer()->device_interface != nullptr);

        assert(gpu_buf1.raw_buffer()->device_interface->buffer_copy(nullptr, gpu_buf2, gpu_buf1.raw_buffer()->device_interface, gpu_buf1) == 0);
        gpu_buf1.copy_to_host();

        for (int i = 0; i < 128; i++) {
            for (int j = 0; j < 128; j++) {
                assert(gpu_buf1(i, j) == (i + j * 256 + 256000));
            }
        }
    }

    printf("Test copy host to device -- subset area.\n");
    {
        Halide::Runtime::Buffer<int32_t> cpu_buf(128, 128);
        cpu_buf.fill(0);

        Halide::Runtime::Buffer<int32_t> gpu_buf1 = make_gpu_buffer(hexagon_rpc);
        assert(gpu_buf1.raw_buffer()->device_interface != nullptr);

        Halide::Runtime::Buffer<int32_t> gpu_buf2 = gpu_buf1.cropped({{32, 64}, {32, 64}});
        assert(gpu_buf2.raw_buffer()->device_interface != nullptr);

        assert(gpu_buf1.raw_buffer()->device_interface->buffer_copy(nullptr, cpu_buf, gpu_buf2.raw_buffer()->device_interface, gpu_buf2) == 0);
        gpu_buf1.set_device_dirty();
        gpu_buf1.copy_to_host();

        for (int i = 0; i < 128; i++) {
            for (int j = 0; j < 128; j++) {
                bool in_gpu3 = (i >= gpu_buf2.dim(0).min()) &&
                               (i < (gpu_buf2.dim(0).min() + gpu_buf2.dim(0).extent())) &&
                               (j >= gpu_buf2.dim(1).min()) &&
                               (j < (gpu_buf2.dim(1).min() + gpu_buf2.dim(1).extent()));
                assert(gpu_buf1(i, j) == (in_gpu3 ? 0 : (i + j * 256)));
            }
        }
    }

    printf("Test copy device to host -- subset area.\n");
    {
        Halide::Runtime::Buffer<int32_t> cpu_buf(128, 128);
        cpu_buf.fill(0);
        Halide::Runtime::Buffer<int32_t> cpu_buf1 = cpu_buf.cropped({{32, 64}, {32, 64}});

        Halide::Runtime::Buffer<int32_t> gpu_buf = make_gpu_buffer(hexagon_rpc);
        assert(gpu_buf.raw_buffer()->device_interface != nullptr);

        assert(gpu_buf.raw_buffer()->device_interface->buffer_copy(nullptr, gpu_buf, nullptr, cpu_buf1) == 0);

        for (int i = 0; i < 128; i++) {
            for (int j = 0; j < 128; j++) {
                bool in_cpu1 = (i >= cpu_buf1.dim(0).min()) &&
                               (i < (cpu_buf1.dim(0).min() + cpu_buf1.dim(0).extent())) &&
                               (j >= cpu_buf1.dim(1).min()) &&
                               (j < (cpu_buf1.dim(1).min() + cpu_buf1.dim(1).extent()));
                assert(cpu_buf(i, j) == (in_cpu1 ? (i + j * 256) : 0));
            }
        }
    }

    printf("Test copy device to device -- subset area.\n");
    {
        Halide::Runtime::Buffer<int32_t> gpu_buf1 = make_gpu_buffer(hexagon_rpc);
        assert(gpu_buf1.raw_buffer()->device_interface != nullptr);

        Halide::Runtime::Buffer<int32_t> gpu_buf2 = make_gpu_buffer(hexagon_rpc, 256000);
        assert(gpu_buf2.raw_buffer()->device_interface != nullptr);

        Halide::Runtime::Buffer<int32_t> gpu_buf3 = gpu_buf2.cropped({{32, 64}, {32, 64}});
        assert(gpu_buf3.raw_buffer()->device_interface != nullptr);

        assert(gpu_buf1.raw_buffer()->device_interface->buffer_copy(nullptr, gpu_buf1, gpu_buf3.raw_buffer()->device_interface, gpu_buf3) == 0);
        gpu_buf2.set_device_dirty();
        gpu_buf2.copy_to_host();

        for (int i = 0; i < 128; i++) {
            for (int j = 0; j < 128; j++) {
                bool in_gpu3 = (i >= gpu_buf3.dim(0).min()) &&
                               (i < (gpu_buf3.dim(0).min() + gpu_buf3.dim(0).extent())) &&
                               (j >= gpu_buf3.dim(1).min()) &&
                               (j < (gpu_buf3.dim(1).min() + gpu_buf3.dim(1).extent()));
                assert(gpu_buf2(i, j) == (i + j * 256 + (in_gpu3 ? 0 : 256000)));
            }
        }
    }

    printf("Test copy from device no src host.\n");
    {
        Halide::Runtime::Buffer<int32_t> gpu_buf = make_gpu_buffer(hexagon_rpc);
        assert(gpu_buf.raw_buffer()->device_interface != nullptr);
        halide_buffer_t no_host_src = *gpu_buf.raw_buffer();
        no_host_src.host = nullptr;
        no_host_src.set_device_dirty(false);

        Halide::Runtime::Buffer<int32_t> cpu_buf(128, 128);
        assert(gpu_buf.raw_buffer()->device_interface->buffer_copy(nullptr, &no_host_src, nullptr, cpu_buf) == 0);

        for (int i = 0; i < 128; i++) {
            for (int j = 0; j < 128; j++) {
                assert(cpu_buf(i, j) == (i + j * 256));
            }
        }
    }

    printf("Test copy to device no dest host.\n");
    {
        Halide::Runtime::Buffer<int32_t> gpu_buf = make_gpu_buffer(hexagon_rpc);
        assert(gpu_buf.raw_buffer()->device_interface != nullptr);
        halide_buffer_t no_host_dst = *gpu_buf.raw_buffer();
        no_host_dst.host = nullptr;

        Halide::Runtime::Buffer<int32_t> cpu_buf(128, 128);
        cpu_buf.fill(0);
        assert(gpu_buf.raw_buffer()->device_interface->buffer_copy(nullptr, cpu_buf, gpu_buf.raw_buffer()->device_interface, &no_host_dst) == 0);
        gpu_buf.set_device_dirty(true);

        gpu_buf.copy_to_host();
        for (int i = 0; i < 128; i++) {
            for (int j = 0; j < 128; j++) {
                assert(gpu_buf(i, j) == 0);
            }
        }
    }

    printf("Test copy device to host no dest host -- confirm error not segfault.\n");
    {
        Halide::Runtime::Buffer<int32_t> gpu_buf1 = make_gpu_buffer(hexagon_rpc);
        assert(gpu_buf1.raw_buffer()->device_interface != nullptr);
        halide_buffer_t no_host_dst = *gpu_buf1.raw_buffer();
        no_host_dst.host = nullptr;

        Halide::Runtime::Buffer<int32_t> gpu_buf2 = make_gpu_buffer(hexagon_rpc, 256000);
        assert(gpu_buf2.raw_buffer()->device_interface != nullptr);

        assert(gpu_buf1.raw_buffer()->device_interface->buffer_copy(nullptr, gpu_buf2, nullptr, &no_host_dst) == halide_error_code_host_is_null);
    }

    // Test copying between different device APIs. Probably will not
    // run on test infrastructure as we do not configure more than one
    // GPU API at a time. For now, special case CUDA and OpenCL as these are
    // the most likely to be supported together.
    if (target.has_feature(Target::CUDA) && target.has_feature(Target::OpenCL)) {
        printf("Test cross device copy device to device.\n");
        {
            Halide::Runtime::Buffer<int32_t> gpu_buf1 = make_gpu_buffer(false, 0, DeviceAPI::CUDA);
            assert(gpu_buf1.raw_buffer()->device_interface != nullptr);

            Halide::Runtime::Buffer<int32_t> gpu_buf2 = make_gpu_buffer(false, 256000, DeviceAPI::OpenCL);
            assert(gpu_buf2.raw_buffer()->device_interface != nullptr);

            assert(gpu_buf1.raw_buffer()->device_interface->buffer_copy(nullptr, gpu_buf2, gpu_buf1.raw_buffer()->device_interface, gpu_buf1) == 0);
            gpu_buf1.copy_to_host();

            for (int i = 0; i < 128; i++) {
                for (int j = 0; j < 128; j++) {
                    assert(gpu_buf1(i, j) == (i + j * 256 + 256000));
                }
            }
        }

        printf("Test cross device copy device to device -- subset area.\n");
        {
            Halide::Runtime::Buffer<int32_t> gpu_buf1 = make_gpu_buffer(false, 0, DeviceAPI::CUDA);
            assert(gpu_buf1.raw_buffer()->device_interface != nullptr);

            Halide::Runtime::Buffer<int32_t> gpu_buf2 = make_gpu_buffer(false, 256000, DeviceAPI::OpenCL);
            assert(gpu_buf2.raw_buffer()->device_interface != nullptr);

            Halide::Runtime::Buffer<int32_t> gpu_buf3 = gpu_buf2.cropped({{32, 64}, {32, 64}});
            assert(gpu_buf3.raw_buffer()->device_interface != nullptr);

            assert(gpu_buf1.raw_buffer()->device_interface->buffer_copy(nullptr, gpu_buf1, gpu_buf3.raw_buffer()->device_interface, gpu_buf3) == 0);
            gpu_buf2.set_device_dirty();
            gpu_buf2.copy_to_host();

            for (int i = 0; i < 128; i++) {
                for (int j = 0; j < 128; j++) {
                    bool in_gpu3 = (i >= gpu_buf3.dim(0).min()) &&
                                   (i < (gpu_buf3.dim(0).min() + gpu_buf3.dim(0).extent())) &&
                                   (j >= gpu_buf3.dim(1).min()) &&
                                   (j < (gpu_buf3.dim(1).min() + gpu_buf3.dim(1).extent()));
                    assert(gpu_buf2(i, j) == (i + j * 256 + (in_gpu3 ? 0 : 256000)));
                }
            }
        }

        printf("Test cross device copy device to device no source host.\n");
        {
            Halide::Runtime::Buffer<int32_t> gpu_buf1 = make_gpu_buffer(false, 0, DeviceAPI::CUDA);
            assert(gpu_buf1.raw_buffer()->device_interface != nullptr);

            Halide::Runtime::Buffer<int32_t> gpu_buf2 = make_gpu_buffer(false, 256000, DeviceAPI::OpenCL);
            assert(gpu_buf2.raw_buffer()->device_interface != nullptr);
            halide_buffer_t no_host_src = *gpu_buf2.raw_buffer();
            no_host_src.host = nullptr;

            assert(gpu_buf1.raw_buffer()->device_interface->buffer_copy(nullptr, &no_host_src, gpu_buf1.raw_buffer()->device_interface, gpu_buf1) == 0);
            gpu_buf1.copy_to_host();

            for (int i = 0; i < 128; i++) {
                for (int j = 0; j < 128; j++) {
                    assert(gpu_buf1(i, j) == (i + j * 256 + 256000));
                }
            }
        }

        printf("Test cross device copy device to device no dest host.\n");
        {
            Halide::Runtime::Buffer<int32_t> gpu_buf1 = make_gpu_buffer(false, 0, DeviceAPI::CUDA);
            assert(gpu_buf1.raw_buffer()->device_interface != nullptr);
            halide_buffer_t no_host_dst = *gpu_buf1.raw_buffer();
            no_host_dst.host = nullptr;

            Halide::Runtime::Buffer<int32_t> gpu_buf2 = make_gpu_buffer(false, 256000, DeviceAPI::OpenCL);
            assert(gpu_buf2.raw_buffer()->device_interface != nullptr);

            assert(gpu_buf1.raw_buffer()->device_interface->buffer_copy(nullptr, gpu_buf2, gpu_buf1.raw_buffer()->device_interface, &no_host_dst) == 0);
            gpu_buf1.set_device_dirty();
            gpu_buf1.copy_to_host();

            for (int i = 0; i < 128; i++) {
                for (int j = 0; j < 128; j++) {
                    assert(gpu_buf1(i, j) == (i + j * 256 + 256000));
                }
            }
        }

        printf("Test cross device copy device to device no source or dest host.\n");
        {
            Halide::Runtime::Buffer<int32_t> gpu_buf1 = make_gpu_buffer(false, 0, DeviceAPI::CUDA);
            assert(gpu_buf1.raw_buffer()->device_interface != nullptr);
            halide_buffer_t no_host_dst = *gpu_buf1.raw_buffer();
            no_host_dst.host = nullptr;

            Halide::Runtime::Buffer<int32_t> gpu_buf2 = make_gpu_buffer(false, 256000, DeviceAPI::OpenCL);
            assert(gpu_buf2.raw_buffer()->device_interface != nullptr);
            halide_buffer_t no_host_src = *gpu_buf2.raw_buffer();
            no_host_src.host = nullptr;

            int err = gpu_buf1.raw_buffer()->device_interface->buffer_copy(nullptr, &no_host_src, gpu_buf1.raw_buffer()->device_interface, &no_host_dst);
            if (err == 0) {
                gpu_buf1.set_device_dirty();
                gpu_buf1.copy_to_host();

                for (int i = 0; i < 128; i++) {
                    for (int j = 0; j < 128; j++) {
                        assert(gpu_buf1(i, j) == (i + j * 256 + 256000));
                    }
                }
            } else {
                // halide_buffer_copy is not guaranteed to handle cross device case without host memory in one of the buffers.
                assert(err == halide_error_code_incompatible_device_interface);
                printf("Cross device with no host buffers case is not handled. Ignoring (correct) error.\n");
            }
        }

        printf("Test cross device copy device to host.\n");
        {
            Halide::Runtime::Buffer<int32_t> gpu_buf1 = make_gpu_buffer(false, 0, DeviceAPI::CUDA);
            assert(gpu_buf1.raw_buffer()->device_interface != nullptr);

            Halide::Runtime::Buffer<int32_t> gpu_buf2 = make_gpu_buffer(false, 256000, DeviceAPI::OpenCL);
            assert(gpu_buf2.raw_buffer()->device_interface != nullptr);

            assert(gpu_buf1.raw_buffer()->device_interface->buffer_copy(nullptr, gpu_buf1, nullptr, gpu_buf2) == 0);
            gpu_buf1.set_device_dirty();
            gpu_buf1.copy_to_host();

            for (int i = 0; i < 128; i++) {
                for (int j = 0; j < 128; j++) {
                    assert(gpu_buf1(i, j) == (i + j * 256));
                }
            }
        }

        printf("Test cross device copy device to host with no dest host.\n");
        {
            Halide::Runtime::Buffer<int32_t> gpu_buf1 = make_gpu_buffer(false, 0, DeviceAPI::CUDA);
            assert(gpu_buf1.raw_buffer()->device_interface != nullptr);

            Halide::Runtime::Buffer<int32_t> gpu_buf2 = make_gpu_buffer(false, 256000, DeviceAPI::OpenCL);
            assert(gpu_buf2.raw_buffer()->device_interface != nullptr);
            halide_buffer_t no_host_dst = *gpu_buf2.raw_buffer();
            no_host_dst.host = nullptr;

            assert(gpu_buf1.raw_buffer()->device_interface->buffer_copy(nullptr, gpu_buf1, nullptr, &no_host_dst) == halide_error_code_host_is_null);
        }
    }

    printf("Success!\n");

    return 0;
}
