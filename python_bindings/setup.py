from skbuild import setup
from setuptools import find_packages
from pathlib import Path
import pybind11


def get_version():
    return "1.0.0"


setup(
    name="halide",
    version=get_version(),
    author="The Halide team",
    author_email="",
    description="",
    long_description=Path("readme.md").read_text(),
    python_requires=">=3.6",
    packages=find_packages(where="src"),
    package_dir={"": "src"},
    cmake_args=[
        f"-Dpybind11_ROOT={pybind11.get_cmake_dir()}",
        "-DCMAKE_REQUIRE_FIND_PACKAGE_pybind11=YES",
        "-DHalide_INSTALL_PYTHONDIR=src/halide",
        "-DCMAKE_INSTALL_RPATH=$ORIGIN",
    ],
    include_package_data=True,
    extras_require={"test": ["pytest>=6.0"]},
    zip_safe=False,
)
