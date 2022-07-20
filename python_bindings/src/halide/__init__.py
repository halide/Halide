# TODO(#6870): The following three lines are a stop-gap. This file should just
# contain the last two imports, at least until the pure-Python part of the
# library grows.
#
# There are three main reasons why this exists:
#
# 1. relative imports cannot be overloaded with sys.path
# 2. for a variety of reasons, copying the python sources next to the
#    halide_.*.so module is difficult to make work in 100% of cases in CMake
# 3. even if we could copy them reliably, copying these files into the build
#    folder seems inelegant
#
# Fortunately, there are apparently other hooks besides sys.path that we could
# use to redirect a failing relative import.
#
# https://docs.python.org/3/reference/import.html#finders-and-loaders
# https://github.com/halide/Halide/issues/6870

import sys
from pathlib import Path
sys.path.append(str(Path(__file__).parent.resolve(strict=True)))

from halide_ import *
from halide_ import _, _1, _2, _3, _4, _5, _6, _7, _8, _9
