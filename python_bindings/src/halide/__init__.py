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

    root = Path(__file__).parent

    bin_dir = root / "bin"
    if hasattr(os, "add_dll_directory") and bin_dir.is_dir():
        os.add_dll_directory(str(bin_dir))

    for relpath in ("bin/Halide.dll", "lib/libHalide.dylib", "lib64/libHalide.so", "lib/libHalide.so"):
        lib_path = root / relpath
        if lib_path.exists():
            ctypes.CDLL(str(lib_path))
            return


_preload_bundled_halide_library()
del _preload_bundled_halide_library

from .halide_ import *  # noqa: E402, F403

# noinspection PyUnresolvedReferences, PyProtectedMember
from .halide_ import _, _0, _1, _2, _3, _4, _5, _6, _7, _8, _9  # noqa: E402, F401


def install_dir():
    import os

    return os.path.dirname(__file__)


from ._generator_helpers import (  # noqa: E402, F401
    _create_python_generator,
    _generatorcontext_enter,
    _generatorcontext_exit,
    _get_python_generator_names,
    active_generator_context,
    alias,
    funcs,
    generator,
    main,
    Generator,
    GeneratorParam,
    InputBuffer,
    InputScalar,
    OutputBuffer,
    OutputScalar,
    vars,
)
