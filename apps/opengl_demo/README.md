# Halide OpenGL Demo


This demo contains an OpenGL desktop app that displays an input image side by side with the result of running a sample halide filter in three different ways:

1. On the CPU, not using OpenGL.

2. In OpenGL, with Halide transfering the input data from the host and transferring the result data back to the host.

3. In OpenGL, with Halide accepting input data that's in an OpenGL texture, and leaving the result in an OpenGL texture.

The display reports the timing for each.  You should expect to see that #3 is fastest as it runs entirely on the GPU, while #2 is slowest because of the data transfer times.

In this example we use AOT compilation twice:  Once with `target=host` to produce the filter that runs on the CPU; and once with `target=host-opengl` to produce the filter that runs in OpenGL (which we call twice).

The sample filter inverts the RGB channels of the input image.

*This demo is known to work on OS X 10.11 and Ubuntu Linux 14.04 & 16.04. Windows has not yet been tested.*

### Instructions:

Build and run the app by simply running `make`.  It should open a window showing the input and the three (identical) filtering results.  You can close the window and exit by pressing ESCAPE.

The `Makefile` has variables to specify where to find Halide, how to link OpenGL, and so forth.  You may need to tweak them for your platform.

See the Makefile for details on how the filter gets AOT-compiled for CPU and OpenGL.  Note that the `Makefile` actually specifies `target=host-opengl-debug` when AOT-compiling the opengl filter; that enables tracing of Halide's management of its OpenGL pipeline.


#### Dependencies:

This app depends on:

* [GLFW 3](http://www.glfw.org)
* [libpng](http://www.libpng.org)
* [libdrawtext](http://nuclear.mutantstargoat.com/sw/libdrawtext/)

On OS X, all three can be installed using [homebrew](http://brew.sh)

```sh
brew install glfw3
brew install libpng
brew install libdrawtext
```

Halide itself can be installed on OS X via

```sh
brew tap halide/halide
brew install halide
```

On Ubuntu Linux, everything but libdrawtext can be installed via system packages:

```sh
sudo apt-get install libglfw3-dev libx11-dev freeglut3-dev libfreetype6-dev libgl-dev libpng
```

### Files:

* `sample_filter.cpp`

   The Halide filter generator source.

* `main.cpp`

   Contains all the Halide client code.

   Note that it `#include`s the generated files `build/sample_filter_cpu.h` and `build/sample_filter_opengl.h`.

* `layout.{h,cpp}`

    A minimal rendering framework for this example app.

* `timer.{h,cpp}`

    A minimal timing & reporting library.

* `{glfw,opengl,png}_helpers.{cpp,h}`

    Conveniences that hide the dirty details of the low-level packages.
