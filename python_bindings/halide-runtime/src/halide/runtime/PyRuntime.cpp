// The `halide.runtime` extension module.
//
// This module deliberately depends ONLY on the header-only Halide runtime
// (HalideRuntime.h / HalideBuffer.h) and pybind11 -- it must NOT depend on
// libHalide. It provides just enough to load a precompiled AOT Halide kernel
// (a shared object exporting `<name>_argv` and `<name>_metadata`) and call it
// with buffer-protocol objects (e.g. NumPy arrays) and Python scalars.
//
// It shares the buffer-protocol <-> halide_buffer_t marshalling core with the
// Python extensions emitted by PythonExtensionGen, via the single source of
// truth in src/PythonExtensionRuntime.template.cpp (included below). Interop
// with `halide.Buffer` (from the compiler module) and with objects produced by
// generated extensions flows through a duck-typed, named-capsule protocol, so
// no libHalide types cross the boundary.

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "HalideRuntime.h"
#include "PyRuntimeBuffer.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// Pull in the shared marshalling core: Halide::PythonRuntime::unpack_buffer()
// and PyHalideBuffer<>. `unpack_buffer` is `inline` and PyHalideBuffer lives in
// an anonymous namespace, so including this translation unit here (rather than
// compiling it separately) is well-formed and keeps a single implementation.
// NOLINTNEXTLINE(bugprone-suspicious-include): intentionally shared source.
#include "PythonExtensionRuntime.template.cpp"

namespace py = pybind11;

namespace {

// ---------------------------------------------------------------------------
// Cross-platform dynamic library handling
// ---------------------------------------------------------------------------

#ifdef _WIN32
using LibHandle = HMODULE;
LibHandle open_library(const std::string &path) {
    return LoadLibraryA(path.c_str());
}
void *find_symbol(LibHandle handle, const std::string &name) {
    return reinterpret_cast<void *>(GetProcAddress(handle, name.c_str()));
}
void close_library(LibHandle handle) {
    FreeLibrary(handle);
}
std::string library_error() {
    return "LoadLibrary/GetProcAddress failed (error " + std::to_string(GetLastError()) + ")";
}
#else
using LibHandle = void *;
LibHandle open_library(const std::string &path) {
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
}
void *find_symbol(LibHandle handle, const std::string &name) {
    return dlsym(handle, name.c_str());
}
void close_library(LibHandle handle) {
    dlclose(handle);
}
std::string library_error() {
    const char *e = dlerror();
    return e ? std::string(e) : std::string("unknown dynamic-linker error");
}
#endif

// ---------------------------------------------------------------------------
// Error handling
//
// By default the Halide runtime prints an error to stderr and aborts the
// process. That is unacceptable in a library: a bad argument or a missing GPU
// driver would take down the interpreter. Each AOT artifact carries its own
// copy of the runtime, so load() installs a handler into that runtime which
// records the message for Kernel::call to raise as a Python exception.
// ---------------------------------------------------------------------------

std::string &last_error() {
    static thread_local std::string s;
    return s;
}

extern "C" void runtime_error_handler(void * /*user_context*/, const char *msg) {
    last_error() = msg ? msg : "";
}

std::string take_last_error() {
    std::string s = last_error();
    last_error().clear();
    return s;
}

using SetErrorHandlerFn = void (*)(void (*)(void *, const char *));

// Device allocations belong to the runtime in the loaded AOT module, so device
// operations dispatch through the interface stored on each buffer.
void device_free(halide_buffer_t *buf) {
    if (buf->device_interface) {
        (void)buf->device_interface->device_free(nullptr, buf);
    }
}

int copy_to_host(halide_buffer_t *buf) {
    if (!buf->device_dirty()) {
        return halide_error_code_success;
    }
    if (!buf->device_interface || !buf->device_interface->copy_to_host) {
        return halide_error_code_no_device_interface;
    }
    return buf->device_interface->copy_to_host(nullptr, buf);
}

// Best-effort default filter name from a library path: strip the directory,
// any file extension, and a leading "lib". e.g. "/x/libfoo.so" -> "foo".
std::string default_filter_name(const std::string &path) {
    size_t slash = path.find_last_of("/\\");
    std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = base.find('.');
    if (dot != std::string::npos) {
        base = base.substr(0, dot);
    }
    if (base.rfind("lib", 0) == 0 && base.size() > 3) {
        base = base.substr(3);
    }
    return base;
}

using ArgvCallFn = int (*)(void **);
using MetadataFn = const halide_filter_metadata_t *(*)();

// ---------------------------------------------------------------------------
// A single loaded AOT filter.
// ---------------------------------------------------------------------------

std::string type_to_string(halide_type_t t);  // defined below

// The three halide_argument_kind_t values, as readable strings.
std::string kind_to_string(int kind) {
    switch (kind) {
    case halide_argument_kind_input_scalar:
        return "input_scalar";
    case halide_argument_kind_input_buffer:
        return "input_buffer";
    case halide_argument_kind_output_buffer:
        return "output_buffer";
    default:
        return "unknown";
    }
}

class Kernel {
public:
    Kernel(LibHandle handle, ArgvCallFn argv_fn, const halide_filter_metadata_t *md)
        : handle_(handle), argv_fn_(argv_fn), md_(md) {
    }

    ~Kernel() {
        if (handle_) {
            close_library(handle_);
        }
    }

    Kernel(const Kernel &) = delete;
    Kernel &operator=(const Kernel &) = delete;

    std::string name() const {
        return md_->name ? md_->name : "";
    }

    std::string target() const {
        return md_->target ? md_->target : "";
    }

    // The argument names, in argv order.
    std::vector<std::string> argument_names() const {
        std::vector<std::string> names;
        names.reserve(md_->num_arguments);
        for (int i = 0; i < md_->num_arguments; i++) {
            names.emplace_back(md_->arguments[i].name);
        }
        return names;
    }

    // Full per-argument metadata, in argv order: a dict per argument describing
    // the calling convention -- its name, kind (input_scalar / input_buffer /
    // output_buffer), element type (e.g. "uint8"), and dimensions (0 for scalars).
    py::list arguments() const {
        py::list result;
        for (int i = 0; i < md_->num_arguments; i++) {
            const halide_filter_argument_t &a = md_->arguments[i];
            py::dict d;
            d["name"] = std::string(a.name);
            d["kind"] = kind_to_string(a.kind);
            d["type"] = type_to_string(a.type);
            d["dimensions"] = a.dimensions;
            result.append(std::move(d));
        }
        return result;
    }

    // Per-argument scratch storage that must outlive the argv_fn call.
    struct ArgSlot {
        halide_scalar_value_t scalar{};
        halide_buffer_t buffer{};
        std::vector<halide_dimension_t> dims;
        Py_buffer py_buf{};
        py::object capsule;
        bool py_buf_valid = false;
        bool needs_device_free = false;
    };

    void call(const py::args &args, const py::kwargs &kwargs) {
        const int argc = md_->num_arguments;

        std::vector<ArgSlot> slots(argc);
        std::vector<void *> argv(argc, nullptr);

        // Release any Python buffers / device allocations on scope exit.
        struct Cleanup {
            std::vector<ArgSlot> &slots;
            ~Cleanup() {
                for (auto &s : slots) {
                    if (s.needs_device_free) {
                        device_free(&s.buffer);
                    }
                    if (s.py_buf_valid) {
                        PyBuffer_Release(&s.py_buf);
                    }
                }
            }
        } cleanup{slots};

        // Map user arguments (positional + keyword) onto metadata slots,
        // auto-filling the implicit __user_context argument if present.
        std::vector<py::handle> values(argc);
        std::vector<bool> filled(argc, false);

        size_t next_positional = 0;
        for (int i = 0; i < argc; i++) {
            if (is_user_context(md_->arguments[i])) {
                filled[i] = true;  // handled specially below; consumes no user arg
            }
        }

        for (auto item : args) {
            // Advance to the next slot the user is expected to fill.
            while (next_positional < (size_t)argc && filled[next_positional]) {
                next_positional++;
            }
            if (next_positional >= (size_t)argc) {
                throw std::runtime_error("Too many positional arguments for kernel '" + name() + "'.");
            }
            values[next_positional] = item;
            filled[next_positional] = true;
            next_positional++;
        }

        for (auto kw : kwargs) {
            const std::string key = py::cast<std::string>(kw.first);
            int slot = -1;
            for (int i = 0; i < argc; i++) {
                if (!is_user_context(md_->arguments[i]) && key == md_->arguments[i].name) {
                    slot = i;
                    break;
                }
            }
            if (slot < 0) {
                throw std::runtime_error("Unknown argument '" + key + "' for kernel '" + name() + "'.");
            }
            if (values[slot]) {
                throw std::runtime_error("Argument '" + key + "' specified more than once.");
            }
            values[slot] = kw.second;
            filled[slot] = true;
        }

        for (int i = 0; i < argc; i++) {
            const halide_filter_argument_t &a = md_->arguments[i];

            ArgSlot &slot = slots[i];

            if (is_user_context(a)) {
                slot.scalar.u.u64 = 0;  // null user context
                argv[i] = &slot.scalar;
                continue;
            }

            if (!values[i]) {
                throw std::runtime_error("Missing argument '" + std::string(a.name) +
                                         "' for kernel '" + name() + "'.");
            }

            if (a.kind == halide_argument_kind_input_scalar) {
                store_scalar(a, values[i], &slot.scalar);
                argv[i] = &slot.scalar;
            } else {
                // Input or output buffer.
                const bool writable = (a.kind == halide_argument_kind_output_buffer);
                halide_buffer_t *raw = unpack_buffer_arg(a, values[i], writable, slot);
                if (a.kind == halide_argument_kind_input_buffer) {
                    raw->set_host_dirty();
                }
                argv[i] = raw;
            }
        }

        take_last_error();  // clear any stale message

        int result;
        {
            py::gil_scoped_release release;
            result = argv_fn_(argv.data());
        }
        if (result != 0) {
            const std::string msg = take_last_error();
            throw std::runtime_error(
                msg.empty() ? ("Halide kernel '" + name() + "' returned error " + std::to_string(result)) : ("Halide kernel '" + name() + "': " + msg));
        }

        // Flush any device-side outputs back to host (host-only buffer protocol).
        for (int i = 0; i < argc; i++) {
            const halide_filter_argument_t &a = md_->arguments[i];
            if (a.kind == halide_argument_kind_output_buffer) {
                auto *buf = static_cast<halide_buffer_t *>(argv[i]);
                const int copy_result = copy_to_host(buf);
                if (copy_result != halide_error_code_success) {
                    const std::string msg = take_last_error();
                    throw std::runtime_error(
                        msg.empty() ? ("Halide kernel '" + name() +
                                       "' failed to copy output '" + a.name +
                                       "' to host with error " + std::to_string(copy_result)) :
                                      ("Halide kernel '" + name() + "': " + msg));
                }
            }
        }
    }

private:
    static bool is_user_context(const halide_filter_argument_t &a) {
        return a.kind == halide_argument_kind_input_scalar &&
               std::strcmp(a.name, "__user_context") == 0;
    }

    // Convert one buffer-protocol / Halide-buffer argument into a halide_buffer_t.
    // Uses the shared named-capsule fast path first, then falls back
    // to the shared unpack_buffer() implementation.
    halide_buffer_t *unpack_buffer_arg(const halide_filter_argument_t &a,
                                       py::handle value,
                                       bool writable,
                                       ArgSlot &slot) {
        // Fast path: an object that already exposes a halide_buffer_t capsule
        // (halide.Buffer, halide.runtime.Buffer, or another generated result).
        py::object get_capsule = py::getattr(value, "_get_halide_buffer_t_capsule", py::none());
        if (!get_capsule.is_none()) {
            py::object capsule = get_capsule();
            if (!py::isinstance<py::capsule>(capsule)) {
                throw py::type_error("_get_halide_buffer_t_capsule() must return a PyCapsule");
            }
            void *ptr = PyCapsule_GetPointer(
                capsule.ptr(), Halide::PythonRuntimeBindings::halide_buffer_capsule_name);
            if (!ptr) {
                throw py::error_already_set();
            }
            slot.capsule = std::move(capsule);
            return static_cast<halide_buffer_t *>(ptr);
        }

        // General path: buffer-protocol object (e.g. NumPy array).
        slot.dims.resize(a.dimensions > 0 ? a.dimensions : 1);
        bool ok = Halide::PythonRuntime::unpack_buffer(
            value.ptr(), writable ? PyBUF_WRITABLE : 0, a.name, a.dimensions,
            slot.py_buf, slot.dims.data(), slot.buffer, slot.py_buf_valid,
            slot.needs_device_free);
        if (!ok) {
            throw py::error_already_set();
        }
        return &slot.buffer;
    }

    void store_scalar(const halide_filter_argument_t &a, py::handle value,
                      halide_scalar_value_t *out) {
        const halide_type_t t = a.type;

#define HALIDE_RUNTIME_SCALAR_CASE(CODE, BITS, CTYPE, FIELD) \
    if (t.code == (CODE) && t.bits == (BITS)) {              \
        out->u.FIELD = py::cast<CTYPE>(value);               \
        return;                                              \
    }

        HALIDE_RUNTIME_SCALAR_CASE(halide_type_float, 32, float, f32)
        HALIDE_RUNTIME_SCALAR_CASE(halide_type_float, 64, double, f64)
        HALIDE_RUNTIME_SCALAR_CASE(halide_type_int, 8, int8_t, i8)
        HALIDE_RUNTIME_SCALAR_CASE(halide_type_int, 16, int16_t, i16)
        HALIDE_RUNTIME_SCALAR_CASE(halide_type_int, 32, int32_t, i32)
        HALIDE_RUNTIME_SCALAR_CASE(halide_type_int, 64, int64_t, i64)
        HALIDE_RUNTIME_SCALAR_CASE(halide_type_uint, 1, bool, b)
        HALIDE_RUNTIME_SCALAR_CASE(halide_type_uint, 8, uint8_t, u8)
        HALIDE_RUNTIME_SCALAR_CASE(halide_type_uint, 16, uint16_t, u16)
        HALIDE_RUNTIME_SCALAR_CASE(halide_type_uint, 32, uint32_t, u32)
        HALIDE_RUNTIME_SCALAR_CASE(halide_type_uint, 64, uint64_t, u64)
        HALIDE_RUNTIME_SCALAR_CASE(halide_type_handle, 64, uint64_t, u64)

#undef HALIDE_RUNTIME_SCALAR_CASE

        throw std::runtime_error("Unsupported scalar type for argument '" +
                                 std::string(a.name) + "'.");
    }

    LibHandle handle_;
    ArgvCallFn argv_fn_;
    const halide_filter_metadata_t *md_;
};

std::string type_to_string(halide_type_t t) {
    std::ostringstream out;
    out << t;
    return out.str();
}

std::shared_ptr<Kernel> load(const std::filesystem::path &path_obj, const py::object &name_obj) {
    const std::string path = path_obj.string();
    LibHandle handle = open_library(path);
    if (!handle) {
        throw std::runtime_error("Could not load '" + path + "': " + library_error());
    }

    // Redirect the kernel runtime's errors to our handler so a runtime failure
    // (e.g. a missing GPU driver) raises a Python exception instead of aborting.
    if (void *set_handler = find_symbol(handle, "halide_set_error_handler")) {
        reinterpret_cast<SetErrorHandlerFn>(set_handler)(&runtime_error_handler);
    }

    std::vector<std::string> candidates;
    if (!name_obj.is_none()) {
        candidates.push_back(py::cast<std::string>(name_obj));
    } else {
        candidates.push_back(default_filter_name(path));
    }

    for (const std::string &name : candidates) {
        auto argv_fn = reinterpret_cast<ArgvCallFn>(find_symbol(handle, name + "_argv"));
        auto meta_fn = reinterpret_cast<MetadataFn>(find_symbol(handle, name + "_metadata"));
        if (argv_fn && meta_fn) {
            const halide_filter_metadata_t *md = meta_fn();
            if (!md) {
                continue;
            }
            if (md->version != halide_filter_metadata_t::VERSION) {
                close_library(handle);
                throw std::runtime_error("Kernel '" + name + "' has metadata version " +
                                         std::to_string(md->version) + ", expected " +
                                         std::to_string(halide_filter_metadata_t::VERSION));
            }
            return std::make_shared<Kernel>(handle, argv_fn, md);
        }
    }

    close_library(handle);
    std::string tried = candidates.empty() ? "" : candidates.front();
    throw std::runtime_error(
        "Could not find Halide filter symbols '" + tried + "_argv' / '" + tried +
        "_metadata' in '" + path + "'. Pass name=... if the function name differs from the file name.");
}

}  // namespace

PYBIND11_MODULE(_runtime, m) {
    m.doc() = "Standalone Halide runtime: load and call precompiled AOT kernels "
              "without depending on libHalide.";

    Halide::PythonRuntimeBindings::define_buffer(m);

    py::class_<Kernel, std::shared_ptr<Kernel>>(m, "Kernel")
        .def("__call__", &Kernel::call)
        .def_property_readonly("name", &Kernel::name)
        .def_property_readonly("target", &Kernel::target)
        .def_property_readonly("argument_names", &Kernel::argument_names)
        .def_property_readonly("arguments", &Kernel::arguments)
        .def("__repr__", [](const Kernel &k) {
            return "<halide.runtime.Kernel '" + k.name() + "'>";
        });

    m.def("load", &load, py::arg("path"), py::arg("name") = py::none(),
          "Load a precompiled Halide AOT kernel from a shared library.\n\n"
          "`path` is the shared object to dlopen; `name` is the Halide function\n"
          "name (defaults to the library's file name). Returns a callable Kernel.");
}
