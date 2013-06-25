.. halide documentation master file, created by
   sphinx-quickstart on Sun Jun 23 17:20:17 2013.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

Overview of Halide Python bindings
========================================

This is documentation for the `Python bindings`_ of the Halide_ language.

For install and license information see the README_.

The example_ applications demonstrate how to use the bindings.

.. _README: https://github.com/halide/Halide/blob/master/python_bindings/README.md
.. _example: https://github.com/halide/Halide/blob/master/python_bindings/apps/
.. _Python bindings: https://github.com/halide/Halide/blob/master/python_bindings/
.. _Halide: http://halide-lang.org/

Classes:

================================    ================================
Class                               Description
================================    ================================
:py:class:`halide.Expr`             Expression or fragment of Halide code
:py:class:`halide.Func`             Function or stage in imaging pipeline
:py:class:`halide.Image`            Image stored in-memory and/or on GPU device
:py:class:`halide.ImageParam`       Image parameter to an imaging pipeline
:py:class:`halide.Param`            Scalar parameter to an imaging pipeline
:py:class:`halide.RDom`             Reduction domain, used for sum, product, min, max
:py:class:`halide.RVar`             Reduction variable
:py:class:`halide.Type`             Image data type, e.g. Int(8), Float(32)
:py:class:`halide.Var`              Variable, defined on a regular grid domain
================================    ================================

Functions:

================================    ================================
Function                            Description
================================    ================================
:py:func:`halide.builtin_image`     One of the built-in images
:py:func:`halide.filter_image`      Helper function for filtering an image
:py:func:`halide.flip_xy`           Convert between Halide and Numpy notation
Reduction functions                 See :py:class:`halide.RDom`
Math functions                      See :py:class:`halide.Expr`
================================    ================================

.. toctree::
   :maxdepth: 2

Module Documentation for 'halide'
=================================

.. automodule:: halide
   :members:

Indices and tables
==================

* :ref:`genindex`
* :ref:`modindex`
* :ref:`search`

