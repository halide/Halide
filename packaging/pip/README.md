Halide is a programming language designed to make it easier to write
high-performance image and array processing code on modern machines. Halide
currently targets:

- CPU architectures: X86, ARM, Hexagon, PowerPC, RISC-V
- Operating systems: Linux, Windows, macOS, Android, iOS, Qualcomm QuRT
- GPU Compute APIs: CUDA, OpenCL, Apple Metal, Microsoft Direct X 12, Vulkan

Rather than being a standalone programming language, Halide is embedded in
Python. This means you write Python code that builds an in-memory representation
of a Halide pipeline using Halide's Python API. You can then compile this
representation to an object file, or JIT-compile it and run it in the same
process.

## Using Halide from C++
Halide is also available as a C++ library. This package provides the development
files necessary to use Halide from C++, including a CMake package. On Linux and
macOS, CMake's `find_package` command should find Halide as long as you're in
the same virtual environment you installed it in. On Windows, you will need to
add the virtual environment root directory to `CMAKE_PREFIX_PATH`. This can be
done by running `set CMAKE_PREFIX_PATH=%VIRTUAL_ENV%` in `cmd`.

Other build systems can find the Halide root path by running `python -c 
"import halide; print(halide.install_dir())"`.