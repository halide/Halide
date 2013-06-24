Python 2.x Bindings for Halide
------------------------------

By Connelly Barnes, 2012-2013.

This library allows you to write Halide code in Python. The library has currently only been tested on Mac OS
and Linux (Debian), against a Halide source build, and is more experimental than the C++ Halide.

Installation
------------

### Prerequisites

Install SWIG 2.0.7+, and Python libraries Numpy, and Python Image Library (PIL).

On Mac:

    sudo easy_install pip
    brew install swig         % Assumes you have Homebrew package manager (use your preferred package manager)
    sudo pip install numpy
    sudo pip install pil

### Building and Installing the Python bindings

Quick install:

    sudo make install         % Install
    make test                 % Optional: run unit tests

Detailed installation:

You can build Halide by using 'make' in the parent directory. This is done automatically by the Makefile for the Python bindings. The following make options are then provided for the Python bindings:

    sudo make clean           % Clean in case any problems occurred
    make build                % Build in place
    make test                 % Run unit tests
    sudo make install         % Build and install
    make run_apps             % Run apps/*.py with GUI output
    make run_apps_headless    % Run apps; output to apps/out*.png

Usage
-----

Currently, there is no manual. The Python bindings are made by wrapping the C++ layer, so the syntax is thus quite similar.

Run the example applications in the apps/ subdirectory, and look at their code, to understand the library.

License
-------

The Python bindings use the same [MIT license](https://github.com/halide/Halide/blob/master/LICENSE.txt) as Halide.
