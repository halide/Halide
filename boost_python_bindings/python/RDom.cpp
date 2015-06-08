#include "RDom.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>
#include "add_operators.h"

#include "../../src/RDom.h"

#include <string>


void defineRDom()
{

    /*

    class RDom(object):
        """
        A multi-dimensional domain over which to iterate. Used when
        defining functions as reductions. See apps/bilateral_grid.py for an
        example of a reduction.

        Constructors::

          RDom(Expr min, Expr extent, name="")                             -- 1D reduction
          RDom(Expr min0, Expr extent0, Expr min1, Expr extent1, name="")  -- 2D reduction
          (Similar for 3D and 4D reductions)
          RDom(Buffer|ImageParam)                    -- Iterate over all points in the domain

        The following global functions can be used for inline reductions::

            minimum, maximum, product, sum
        """
        def __new__(cls, *args):
            args = [wrap(x) if not isinstance(x,str) else x for x in args]
            return RDomType(*args)

        def defined(self):
            """
            Check if reduction domain is non-NULL.
            """

        def same_as(self, other):
            """
            Check if two reduction domains are the same.
            """

        def dimensions(self):
            """
            Number of dimensions.
            """

        x = property(doc="Access dimension 1 of reduction domain.")
        y = property(doc="Access dimension 2 of reduction domain.")
        z = property(doc="Access dimension 3 of reduction domain.")
        w = property(doc="Access dimension 4 of reduction domain.")

      */
    return;
}
