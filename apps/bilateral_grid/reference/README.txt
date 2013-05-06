 Table of content
##################

1- How to install
2- How to use
3- The algorithm
4- The code
5- Improving performances





 1- How to install
###################

a) Requirements
---------------

This code has been made and tested under Linux. It should work as well
under Windows.


b) Compiling
------------

Running 'make' should compile everything out of the box.









 2- How to use
###############

On the command line, type:
./truncated_kernel_bf input.ppm output.ppm 16.0 0.1

The parameters are:

- 'input.ppm': It is an image file in the PPM format. 'convert' from
the ImageMagick package makes it easy to produce such files.

- 'output.ppm': It is the image that will be created. The file format
is PPM. Software such as Photoshop, Gimp, and xv can read this format.

- 16.0: The 1st number is the space sigma in pixels.

- 0.1: The 2nd number is the range sigma. The intensity is considered
in the [0:1] range.










 3- The algorithm
##################

Here is the pseudo-code for the algorithm:
(a) load a PPM image
(b) compute gray levels
(c) apply the fast bilateral filter
(d) save the result image










 4- The code
#############

The code is in C++.

The algorithm described above is in 'truncated_bilateral_bf.cc'. This
is the file that you may want to edit. The code follows the
description exactly. Comments precede each part. Variable names are
long and self-explanatory.

You should not need to edit the other files. But in case you want to,
here is a short description of each of them.


In the 'include' directory:
---------------------------

- array.h: Classes for 2D and 3D arrays of values.

- array_n.h: Class for nD arrays of values.

- fast_lbf.h: Provides a fast color bilateral filter.

- math_tools.h: Several useful simple functions.

- mixed_vector.h: Provide a class to extend vectors. Useful to add a
  homongeneous coordinate.

- msg_stream.h: C++ streams for warnings and errors.





5- Improving performances
#########################

Compiler optimizations can speed up the process a lot. However, the
provided makefile does not use any optimization to maximize the
chances of successful compilation. For instance, with 'gcc' you may
try options such as '-O3' and '-march=pentium4'.


