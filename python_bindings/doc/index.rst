
Overview of Halide Python bindings
========================================

This is documentation for the `Python bindings`_ of the Halide_ language.

For install and license information see the README_.

The `example applications`_ and tutorials_ demonstrate how to use the bindings.

.. _README: https://github.com/halide/Halide/blob/master/python_bindings/README.md
.. _tutorials: https://github.com/halide/Halide/blob/master/python_bindings/tutorial
.. _example applications: https://github.com/halide/Halide/blob/master/python_bindings/apps/
.. _Python bindings: https://github.com/halide/Halide/blob/master/python_bindings/
.. _Halide: http://halide-lang.org/

.. This listing was manually transfered from what pydoc reports

================================    ================================
Class                               Description
================================    ================================
:py:class:`halide.Argument`         A struct representing an argument to a halide-generated function
:py:class:`halide.Buffer`           The internal representation of a dense array data
:py:class:`halide.Expr`             Expression or fragment of Halide code
:py:class:`halide.Func`             Function or stage in imaging pipeline
:py:class:`halide.Image`            Image stored in-memory and/or on GPU device
:py:class:`halide.ImageBase`
:py:class:`halide.Image_float32`
:py:class:`halide.Image_uint8`
:py:class:`halide.ImageParam`       Image parameter to an imaging pipeline
:py:class:`halide.Param`            Scalar parameter to an imaging pipeline
:py:class:`halide.Param_float32`
:py:class:`halide.Param_uint8`
:py:class:`halide.RDom`             Reduction domain, used for sum, product, min, max
:py:class:`halide.RVar`             Reduction variable
:py:class:`halide.Realization`      Set of Buffers (Tuples are to Exprs as Realizations are to Buffers)
:py:class:`halide.Target`           A struct representing a target machine and os to generate code for.
:py:class:`halide.Tuple`            Create a small array of Exprs for defining and calling functions with multiple outputs.
:py:class:`halide.Type`             Image data type, e.g. Int(8), Float(32)
:py:class:`halide.Var`              Variable, defined on a regular grid domain
================================    ================================


====================================    ================================
Helper Classes                          Description
====================================    ================================
:py:class:`halide.buffer_t`             The raw representation of an image passed around by generated Halide code.
:py:class:`halide.FuncRefExpr`
:py:class:`halide.InternalFunction`
:py:class:`halide.FuncRefVar`
:py:class:`halide.Stage`                A single definition of a Func.
:py:class:`halide.VarOrRVar`
====================================    ================================


====================================    ================================
Enums                                   Description
====================================    ================================
:py:class:`halide.ArgumentKind`         See :py:class:`halide.Argument`
:py:class:`halide.DeviceAPI`            See :py:class:`halide.Func.gpu`
:py:class:`halide.StmtOutputFormat`
:py:class:`halide.TargetArch`           See :py:class:`halide.Target`
:py:class:`halide.TargetFeature`        See :py:class:`halide.Target`
:py:class:`halide.TargetOS`             See :py:class:`halide.Target`
====================================    ================================


Functions:

================================    ================================
Function                            Description
================================    ================================
:py:func:`halide.builtin_image`     One of the built-in images
:py:func:`halide.filter_image`      Helper function for filtering an image
:py:func:`halide.flip_xy`           Convert between Halide and Numpy notation
Reduction functions                 See :py:class:`halide.RDom` and :py:class:`halide.minimum`
Math functions                      See for example :py:class:`halide.sqrt`
================================    ================================

.. toctree::
   :maxdepth: 2

Module Documentation for 'halide'
=================================

.. automodule:: halide
   :members:
   :undoc-members:

Indices and tables
==================

* :ref:`genindex`
* :ref:`modindex`
* :ref:`search`

