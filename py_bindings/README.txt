Highly experimental Python 2.x bindings for Halide

By Connelly Barnes

--------------------------------------------
Install and Build
--------------------------------------------

Pre-requisites: SWIG 2.0.7+, Numpy, Python Image Library (PIL), psutil.

You can build with 'make' and run the test suite with 'make test'.

There is currently no install option although you can presumably copy the relevant shared object and py files
into your Python lib/site-packages/ directory or modify setup.py to do the installation.

You can change to the examples directory and run some examples.

There is no documentation as such but there is a plethora of examples so you can probably start from those.
The main caveat is that Halide functions (Func instances) should be called using square brackets, e.g.
f[x, y] = 2.0*g[x, y]+1.0.
