from pathlib import Path

# halide-bin owns library discovery and loads its bundled libHalide before the
# compiler extension asks the dynamic loader to resolve it.
from . import bin as _bin  # noqa: F401

# Register the ABI-only Runtime types before loading the compiler extension.
# This lets methods such as Type.to_abi() return the actual
# halide.runtime.Type Python class.
from . import runtime as runtime
from .halide_ import *  # noqa: F403

# The implicit-argument placeholders are deliberately imported explicitly;
# `from .halide_ import *` skips them because they begin with an underscore.
from .halide_ import _, _0, _1, _2, _3, _4, _5, _6, _7, _8, _9  # noqa: F401
from ._generator_helpers import (  # noqa: F401
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


def install_dir():
    return str(Path(__file__).resolve().parent)
