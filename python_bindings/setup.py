from skbuild import setup, cmaker, utils
from setuptools import find_packages
from pathlib import Path
import pybind11
from tempfile import TemporaryDirectory as mkdtemp_ctx
import textwrap


def get_version():
    """
    Builds a dummy project that prints the found Halide version. The "version"
    of these Halide bindings is whatever version of Halide they're building
    against.
    """

    cmakelists_txt = textwrap.dedent(
        """
        cmake_minimum_required(VERSION 3.22)
        project(dummy)
        find_package(Halide REQUIRED Halide)
        file(WRITE halide_version.txt "${Halide_VERSION}")
        """
    )

    with mkdtemp_ctx() as srcdir, mkdtemp_ctx() as dstdir:
        src, dst = Path(srcdir), Path(dstdir)
        (src / "CMakeLists.txt").write_text(cmakelists_txt)
        with utils.push_dir(dst):
            cmkr = cmaker.CMaker()
            cmkr.configure(cmake_source_dir=src, clargs=("--no-warn-unused-cli",))
            version = (src / "halide_version.txt").read_text().strip()
            return version


setup(
    name="halide",
    version=get_version(),
    author="The Halide team",
    author_email="",
    description="",
    long_description="",
    python_requires=">=3.6",
    packages=find_packages(where="src"),
    package_dir={"": "src"},
    cmake_args=[
        f"-Dpybind11_ROOT={pybind11.get_cmake_dir()}",
        "-DCMAKE_REQUIRE_FIND_PACKAGE_pybind11=YES",
        "-DHalide_INSTALL_PYTHONDIR=src",
        "-DCMAKE_INSTALL_RPATH=$<IF:$<PLATFORM_ID:Darwin>,@loader_path,$ORIGIN>",
        "-DHalide_Python_INSTALL_IMPORTED_DEPS=ON",
        "-DWITH_TESTS=NO",
        "-DWITH_TUTORIALS=NO",
        "--no-warn-unused-cli",
    ],
)
