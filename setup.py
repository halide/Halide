import pybind11
from setuptools import find_packages
from skbuild import setup

setup(
    name="halide",
    version='15.0.0',
    author="The Halide team",
    author_email="",
    description="",
    long_description="",
    python_requires=">=3.6",
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
        "--no-warn-unused-cli",
    ],
)
