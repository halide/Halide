Python Bindings for Halide
--------------------------

By Connelly Barnes, 2012-2013.

This library allows you to write Halide code in Python 2.7. The library has currently only been tested on Mac OS
and Linux (Debian, Ubuntu), against a Halide source build, and is more experimental than the C++ Halide.

Installation
------------

### Prerequisites

Install SWIG 2.0.4+, and Python libraries Numpy, and Python Image Library (PIL).

On Mac:

    brew install swig         % Assumes you have Homebrew package manager (use your preferred package manager)
    sudo easy_install pip
    sudo pip install numpy pil

On Ubuntu Linux:

    sudo apt-get install python-dev libpng12-dev python-imaging python-numpy swig

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

Documentation and Examples
--------------------------

Consult the [module documentation](http://connellybarnes.com/documents/halide/).

Check out the code for the example applications in the apps/ subdirectory. You can run them individually or as a batch:

    make run_apps             % Run apps/*.py with GUI output 
    make run_apps_headless    % Run apps; output to apps/out*.png

License
-------

The Python bindings use the same [MIT license](https://github.com/halide/Halide/blob/master/LICENSE.txt) as Halide.
