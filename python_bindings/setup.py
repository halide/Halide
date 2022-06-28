from skbuild import setup
from setuptools import find_packages

with open('version.py', 'r') as f:
    version = f.read().strip().split(' ')[-1][1:-1]

with open("readme.md", "r") as fp:
    long_description = fp.read()

setup(
    name="halide",
    version=version,
    author="The Halide team",
    author_email="",
    description="",
    long_description=long_description,
    python_requires=">=3.6",
    packages=find_packages(where="src"),
    package_dir={"": "src"},
    cmake_install_dir="src/halide",
    include_package_data=True,
    extras_require={"test": ["pytest>=6.0"]},
    zip_safe=False
)
