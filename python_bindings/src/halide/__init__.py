def patch_dll_dirs():
    import os
    if hasattr(os, 'add_dll_directory'):
        from pathlib import Path
        bin_dir = Path(__file__).parent / 'bin'
        if bin_dir.exists():
            os.add_dll_directory(str(bin_dir))


patch_dll_dirs()
del patch_dll_dirs

from .halide_ import *
# noinspection PyUnresolvedReferences
from .halide_ import _, _0, _1, _2, _3, _4, _5, _6, _7, _8, _9


def install_dir():
    import os
    return os.path.dirname(__file__)


from ._generator_helpers import (
    _create_python_generator,
    _generatorcontext_enter,
    _generatorcontext_exit,
    _get_python_generator_names,
    active_generator_context,
    alias,
    funcs,
    Generator,
    generator,
    GeneratorParam,
    InputBuffer,
    InputScalar,
    OutputBuffer,
    OutputScalar,
    vars,
)
