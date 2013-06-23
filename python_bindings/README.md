Python 2.x Bindings for Halide
------------------------------

By Connelly Barnes, 2012-2013.

This library allows you to write Halide code in Python. The library has currently only been tested on Mac OS,
against a Halide source build, and is experimental.

Installation
------------

### Prerequisites

Install SWIG 2.0.7+, and Python libraries Numpy, and Python Image Library (PIL).

On Mac:

    sudo easy_install pip
    brew install swig
    sudo pip install numpy
    sudo pip install pil

### Installation

Quick install:

    sudo make install   % Install
    make test           % Optional: run unit tests

Detailed installation:

You can build Halide by using 'make' in the parent directory. This is implicitly done by the Makefile for the Python bindings. The following make options are then provided:

    make clean          % Clean in case any problems occurred
    make                % Build in place
    sudo make install   % Build and install
    make test           % Run unit tests
    make run_apps       % Run apps/*.py

Usage
-----

Currently, there is no manual. The Python bindings are made by wrapping the C++ layer, so the syntax is thus quite similar.

Run the example applications in the apps/ subdirectory, and look at their code, to understand the library.




