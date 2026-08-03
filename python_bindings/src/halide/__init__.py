def _halide_install_dir():
    # `halide-bin` is only present when this package was built in split mode
    # (see pyproject.toml's HALIDE_SPLIT_BUILD override); a plain, monolithic
    # build has no such dependency and bundles everything under this package's
    # own directory instead, exactly as before the split.
    try:
        import halide_bin
    except ImportError:
        import os

        return os.path.dirname(__file__)
    else:
        return halide_bin.install_dir()


def _preload_bundled_halide_library():
    # Force-load our own copy of the Halide runtime library by absolute path before
    # importing halide_, so that halide_'s implicit load of the same library (by
    # soname/module name) resolves to this already-loaded instance instead of
    # searching LD_LIBRARY_PATH / PATH / the default dynamic linker paths, where a
    # foreign, incompatible libHalide could shadow ours.
    # See: https://github.com/halide/Halide/issues/8866
    import ctypes
    import os

    from pathlib import Path

    root = Path(_halide_install_dir())

    bin_dir = root / "bin"
    if hasattr(os, "add_dll_directory") and bin_dir.is_dir():
        os.add_dll_directory(str(bin_dir))

    for relpath in (
        "bin/Halide.dll",
        "lib/libHalide.dylib",
        "lib64/libHalide.so",
        "lib/libHalide.so",
    ):
        lib_path = root / relpath
        if lib_path.exists():
            ctypes.CDLL(str(lib_path))
            return


def install_dir():
    return _halide_install_dir()


# ---------------------------------------------------------------------------
# Lazy loading of the compiler extension.
#
# `import halide.runtime` must not pull in libHalide, but importing any submodule
# runs this parent package's __init__ first. So instead of eagerly importing the
# compiler extension (halide_) here, we defer it until a compiler attribute is
# actually accessed on the `halide` module (PEP 562 module __getattr__). A
# runtime-only deployment can therefore `import halide.runtime` with no compiler
# and no libHalide present.
# ---------------------------------------------------------------------------

# The implicit-argument placeholders, which `from .halide_ import *` would skip
# because they begin with an underscore.
_PLACEHOLDER_ARG_NAMES = ("_", "_0", "_1", "_2", "_3", "_4", "_5", "_6", "_7", "_8", "_9")

_GENERATOR_HELPER_NAMES = (
    "_create_python_generator",
    "_generatorcontext_enter",
    "_generatorcontext_exit",
    "_get_python_generator_names",
    "active_generator_context",
    "alias",
    "funcs",
    "generator",
    "main",
    "Generator",
    "GeneratorParam",
    "InputBuffer",
    "InputScalar",
    "OutputBuffer",
    "OutputScalar",
    "vars",
)

_compiler_loaded = False
_compiler_loading = False


def _load_compiler():
    """Import the Halide compiler extension and hoist its names into this module.

    Idempotent, and safe to call repeatedly. Raises ImportError if the compiler
    extension is unavailable (e.g. a libHalide-free, runtime-only install); the
    "loaded" flag is only latched on success so that later accesses re-raise that
    same clear error rather than a confusing AttributeError."""
    global _compiler_loaded, _compiler_loading
    if _compiler_loaded or _compiler_loading:
        # `_compiler_loading` guards re-entrant access (e.g. from the generator
        # helpers imported below) while names are still being populated.
        return
    _compiler_loading = True
    try:
        _preload_bundled_halide_library()

        try:
            from . import halide_
        except ImportError as e:
            raise ImportError(
                "The Halide compiler is not available in this installation. This "
                "looks like a runtime-only install, which provides `halide.runtime` "
                "(for calling precompiled AOT kernels) without libHalide. Install "
                "the full `halide` package to use the compiler/JIT API."
            ) from e

        g = globals()
        _populate_from_compiler(g, halide_)
        _compiler_loaded = True
    finally:
        _compiler_loading = False


def _populate_from_compiler(g, halide_):
    # `from .halide_ import *` semantics: all public (non-underscore) names ...
    for name in dir(halide_):
        if not name.startswith("_"):
            g.setdefault(name, getattr(halide_, name))
    # ... plus the implicit-argument placeholders imported explicitly.
    for name in _PLACEHOLDER_ARG_NAMES:
        g[name] = getattr(halide_, name)

    from . import _generator_helpers

    for name in _GENERATOR_HELPER_NAMES:
        g[name] = getattr(_generator_helpers, name)


def __getattr__(name):
    # PEP 562: invoked only for attributes not already found in globals(), so once
    # _load_compiler() has hoisted the compiler names this is no longer hit for them.
    if name == "__all__":
        _load_compiler()
        return sorted(n for n in globals() if not n.startswith("_"))
    _load_compiler()
    try:
        return globals()[name]
    except KeyError:
        raise AttributeError(f"module {__name__!r} has no attribute {name!r}") from None


def __dir__():
    _load_compiler()
    return sorted(globals())
