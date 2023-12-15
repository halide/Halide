import pybind11
from setuptools import find_packages
from skbuild import setup
from pathlib import Path
from .Halide import *

this_directory = Path(__file__).parent
long_description = (this_directory / "README_python.md").read_text()

# TODO: this is a pretty cheesy way to allow overriding version for nightlies
# (ie by writing an __init__.py into the root dir); surely there is a better way?
if not "__VERSION__" in globals():
    __VERSION__="17.0.0"

setup(
    name="halide",
    version=__VERSION__,
    author="The Halide team",
    author_email="halide-dev@lists.csail.mit.edu",
    description="Halide is a programming language designed to make it easier "
                "to write high-performance image and array processing code.",
    long_description=long_description,
    long_description_content_type="text/markdown",
    python_requires=">=3.8",
    packages=find_packages(where="python_bindings/src"),
    package_dir={"": "python_bindings/src"},
    cmake_source_dir="python_bindings",
    cmake_args=[
        f"-Dpybind11_ROOT={pybind11.get_cmake_dir()}",
        "-DCMAKE_REQUIRE_FIND_PACKAGE_pybind11=YES",
        "-DHalide_INSTALL_PYTHONDIR=python_bindings/src",
        "-DCMAKE_INSTALL_RPATH=$<IF:$<PLATFORM_ID:Darwin>,@loader_path,$ORIGIN>",
        "-DHalide_Python_INSTALL_IMPORTED_DEPS=ON",
        "-DWITH_TESTS=NO",
        "-DWITH_TUTORIALS=NO",
        "-DWITH_PYTHON_STUBS=NO",
        "-DCMAKE_PREFIX_PATH=$ENV{CMAKE_PREFIX_PATH}",
        "--no-warn-unused-cli",
    ],
)
