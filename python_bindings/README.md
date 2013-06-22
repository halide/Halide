Python 2.x Bindings for Halide
------------------------------

By Connelly Barnes, 2012-2013.

These allow you to write Halide code in python. This code has currently only been tested on Mac OS,
against a Halide source build, and are experimental. Currently the library does not install into
the system package directory.

Installation
------------

### Prerequisites

Install SWIG 2.0.7+, and Python libraries Numpy, and Python Image Library (PIL).

On Mac:

  sudo easy_install pip
  brew install swig
  sudo pip install numpy
  sudo pip install pil

### Building

You can build Halide by using 'make' in the parent directory. Next, build the Python bindings with 'make' and run the test suite with 'make test'.

Usage
-----

Currently, there is no manual. The Python bindings are made by wrapping the C++ layer, so the syntax is thus quite similar.

Run the example applications in the apps/ subdirectory, and look at their code, to understand the library.




