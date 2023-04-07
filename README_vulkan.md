# Vulkan Support for Halide

Halide supports the Khronos Vulkan framework as a compute API backend for GPU-like 
devices, and compiles directly to a binary SPIR-V representation as part of its 
code generation before submitting it to the Vulkan API. Both JIT and AOT usage 
are supported via the `vulkan` target flag (e.g. `HL_JIT_TARGET=host-vulkan`).

Vulkan support is actively under development, and considered *BETA* quality
at this stage.  Tests are passing, but performance tuning and user testing is needed 
to identify potential issues before rolling this into production.  

See [below](#current-status) for details.

# Compiling Halide w/Vulkan Support

You'll need to configure Halide and enable the cmake option TARGET_VULKAN (which is now ON by default).

For example, on Linux & OSX:

```
% cmake -G Ninja -DTARGET_VULKAN=ON -DCMAKE_BUILD_TYPE=Release -DLLVM_DIR=$LLVM_ROOT/lib/cmake/llvm 
% cmake --build build --config Release
```

On Windows, you may need to specify the location of the Vulkan SDK if the paths aren't resolved by CMake automatically.  For example (assuming the Vulkan SDK is installed in the default path):

```
C:\> cmake -G Ninja -DTARGET_VULKAN=ON -DCMAKE_BUILD_TYPE=Release -DLLVM_DIR=$LLVM_ROOT/lib/cmake/llvm -DVulkan_LIBRARY=C:\VulkanSDK\1.3.231.1\Lib\vulkan-1.lib -DVulkan_INCLUDE_DIR=C:\VulkanSDK\1.3.231.1\Include\vulkan -S . -B build
C:\> cmake --build build --config Release

```

# Vulkan Runtime Environment:

Halide has no direct dependency on Vulkan for code-generation, but the runtime
requires a working Vulkan environment to run Halide generated code. Any valid 
Vulkan v1.0+ device driver should work.

Specifically, you'll need:

-   A vendor specific Vulkan device driver
-   The generic Vulkan loader library

For AMD & NVIDIA & Intel devices, download and install the latest graphics driver 
for your platform. Vulkan support should be included.

## Windows 

To build Halide AOT generators, you'll need the Vulkan SDK (specifically the Vulkan loader library and headers):
https://sdk.lunarg.com/sdk/download/latest/windows/vulkan-sdk.exe

For Vulkan device drivers, consult the appropriate hardware vendor for your device.  A few common ones are listed below.

-   [AMD Vulkan Driver](https://www.amd.com/en/technologies/vulkan)
-   [NVIDIA Vulkan Driver](https://developer.nvidia.com/vulkan-driver)
-   [INTEL Vulkan Driver](https://www.intel.com/content/www/us/en/download-center/home.html)

## Linux 

On Ubuntu Linux v22.04, the vulkan runtime is distributed in the `vulkan-tools` package. For earlier versions of Ubuntu (e.g. v20.x or v18.x) the contents of the `vulkan-tools` package was distributed as `vulkan-utils` so use that package instead.

Proprietary drivers can be installed via 'apt' using PPA's for each vendor. Examples for AMD and NVIDIA are provided below.

For AMD on Ubuntu v22.04:
```
$ sudo add-apt-repository ppa:oibaf/graphics-drivers
$ sudo apt update
$ sudo apt upgrade
$ sudo apt install libvulkan1 mesa-vulkan-drivers vulkan-tools
```

For NVIDIA on Ubuntu v22.04:
```
$ sudo add-apt-repository ppa:graphics-drivers/ppa
$ sudo apt update
$ sudo apt upgrade
# - replace ### with latest driver release (e.g. 515)
$ sudo apt install nvidia-driver-### nvidia-settings vulkan vulkan-tools
```

Note that only valid drivers for your system should be installed since there are
reports of the Vulkan loader segfaulting just by having a non-supported driver present. 
Specifically, the seemingly generic `mesa-vulkan-drivers` actually includes the AMD 
graphics driver, which can cause problems if installed on an NVIDIA-only system. 

## Mac

You're better off using Halide's Metal backend instead, but it is possible to run 
Vulkan apps on a Mac via the MoltenVK library:

-   [MoltenVK Project](https://github.com/KhronosGroup/MoltenVK)

The easiest way to get the necessary dependencies is to use the official MoltenVK SDK
installer provided by LunarG:

-   [MoltenVK SDK (Latest Release)](https://sdk.lunarg.com/sdk/download/latest/mac/vulkan-sdk.dmg)

Alternatively, if you have the [Homebrew](https://brew.sh/) package manager installed 
for MacOS, you can use it to install the Vulkan Loader and MoltenVK compatibility 
layer:

```
$ brew install vulkan-loader molten-vk
```

# Testing Your Vulkan Environment

You can validate that everything is configured correctly by running the `vulkaninfo`
app (bundled in the vulkan-utils package) to make sure your device is detected (eg):

```
$ vulkaninfo
==========
VULKANINFO
==========

Vulkan Instance Version: 1.3.224


Instance Extensions: count = 19
===============================
	...

Layers: count = 10
==================
VK_LAYER_KHRONOS_profiles (Khronos Profiles layer) Vulkan version 1.3.224, layer version 1:
	Layer Extensions: count = 0
	Devices: count = 1
		GPU id = 0 (NVIDIA GeForce RTX 3070 Ti)
		Layer-Device Extensions: count = 1

...

```

Make sure everything looks correct before continuing!

# Targetting Vulkan

To generate Halide code for Vulkan, simply add the `vulkan` flag to your target as well as any other optional device specific features you wish to enable for Halide:

| Target Feature | Description | 
| --             | --          |
| `vulkan`       | Enables the vulkan backend |
| `vk_int8`      | Allows 8-bit integer storage types to be used |
| `vk_int16`     | Allows 16-bit integer storage types to be used |
| `vk_int64`     | Allows 64-bit integer storage types to be used |
| `vk_float16`   | Allows 16-bit floating-point values to be used for computation |
| `vk_float64`   | Allows 64-bit floating-point values to be used for computation |
| `vk_v10`       | Generates code compatible with the Vulkan v1.0+ API |
| `vk_v12`       | Generates code compatible with the Vulkan v1.2+ API |
| `vk_v13`       | Generates code compatible with the Vulkan v1.3+ API |

Note that 32-bit integer and floating-point types are always available. All other optional device features are off by default (since they are not required by the Vulkan API, and thus must be explicitly enabled to ensure that the code being generated will be compatible with the device and API version being used for execution). 

For AOT generators add `vulkan` (and any other flags you wish to use) to the target command line option:

```
$ ./lesson_15_generate -g my_first_generator -o . target=host-vulkan-vk_int8-vk_int16
```

For JIT apps use the `HL_JIT_TARGET` environment variable:

```
$ HL_JIT_TARGET=host-vulkan-vk_int8-vk_int16 ./tutorial/lesson_01_basics
```

# Useful Runtime Environment Variables

To modify the default behavior of the runtime, the following environment 
variables can be used to adjust the configuration of the Vulkan backend 
at execution time:

`HL_VK_LAYERS=...` will tell Halide to choose a suitable Vulkan instance
that supports the given list of layers. If not set, `VK_INSTANCE_LAYERS=...` 
will be used instead. If neither are present, Halide will use the first 
Vulkan compute device it can find.  Multiple layers can be specified using 
the appropriate environment variable list delimiter (`:` on Linux/OSX/Posix, 
or `;` on Windows).

`HL_VK_DEVICE_TYPE=...` will tell Halide to choose which type of device
to select for creating the Vulkan instance. Valid options are 'gpu', 
'discrete-gpu', 'integrated-gpu', 'virtual-gpu', or 'cpu'. If not set,
Halide will search for the first 'gpu' like device it can find, or fall back
to the first compute device it can find.

`HL_VK_ALLOC_CONFIG=...` will tell Halide to configure the Vulkan memory
allocator use the given constraints specified as 5x integer values 
separated by the appropriate environment variable list delimiter 
(e.g. `N:N:N:N:N` on Linux/OSX/Posix, or `N;N;N;N;N` on Windows). These values 
correspond to `maximum_pool_size`, `minimum_block_size`, `maximum_block_size`, 
`maximum_block_count` and `nearest_multiple`. 

The `maximum_pool_size` constraint will tell Halide to configure the 
Vulkan memory allocator to never request more than N megabytes for the
entire pool of allocations for the context. This includes all resource 
blocks used for suballocations. Setting this to a non-zero value will 
limit the amount device memory used by Halide, which may be useful when
other applications and frameworks are competing for resources. 
Default is 0 ... meaning no limit.

The `minimum_block_size` constraint will tell Halide to configure the 
Vulkan memory allocator to always request a minimum of N megabytes for 
a resource block, which will be used as a pool for suballocations.  
Increasing this value may improve performance while sacrificing the amount 
of available device memory. Default is 32MB.

The `maximum_block_size` constraint will tell Halide to configure the 
Vulkan memory allocator to never exceed a maximum of N megabytes for a 
resource block.  Decreasing this value may free up more memory but may 
impact performance, and/or restrict allocations to be unusably small. 
Default is 0 ... meaning no limit.

The `maximum_block_count` constraint will tell Halide to configure the 
Vulkan memory allocator to never exceed a total of N block allocations.  
Decreasing this value may free up more memory but may impact performance, 
and/or restrict allocations. Default is 0 ... meaning no limit.

The `nearest_multiple` constraint will tell Halide to configure the 
Vulkan memory allocator to always round up the requested allocation sizes
to the given integer value. This is useful for architectures that
require specific alignments for subregions allocated within a block.
Default is 32 ... setting this to zero means no constraint. 

# Debug Environment Variables

The following environment variables may be useful for tracking down potential
issues related to Vulkan:

`HL_DEBUG_CODEGEN=3` will print out debug info that includees the SPIR-V
code generator used for Vulkan while it is compiling.

`HL_SPIRV_DUMP_FILE=...` specifies a file to dump the binary SPIR-V generated
during compilation. Useful for debugging CodeGen issues. Can be inspected,
validated and disassembled via the SPIR-V tools:

https://github.com/KhronosGroup/SPIRV-Tools


# Current Status

All correctness tests are now passing on tested configs for Linux & Windows using the target `host-vulkan-vk_int8-vk_int16-vk_int64-vk_float16-vk_float64-vk_v13` on LLVM v14.x. 

MacOS passes most tests but encounters internal MoltenVK code translation issues for wide vectors, and ambiguous function calls.

Python apps, tutorials and correctness tests are now passing, but the AOT cases are skipped since the runtime environment needs to be customized to locate the platform specific Vulkan loader library.

Android platform support is currently being worked on.

# Caveats:

-   Other than 32-bit floats and integers, every other data type is optional per the Vulkan spec
-   Float 64-bit types can be enabled, but there aren't any native math functions available in SPIR-V
-   Only one dynamically sized shared memory allocation can be used, but any number of 
    fixed sized allocation are supported (up to the maximum amount allowed by the device)

# Known TODO:

-   Performance tuning of CodeGen and Runtime
-   More platform support (Android is work-in-progress, RISC-V, etc)
-   Adapt unsupported types to supported types (if missing vk_int8 then promote to uint32_t)?
-   Better debugging utilities using the Vulkan debug hooks.
-   Allow debug symbols to be stripped from SPIR-V during codegen to reduce
    memory overhead for large kernels.
-   Investigate floating point rounding and precision (v1.3 adds more controls)
-   Investigate memory model usage (can Halide gain anything from these?)

