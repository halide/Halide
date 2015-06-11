#include "Tuple.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>

#include "../../src/Tuple.h"

#include <vector>
#include <string>

void defineTuple()
{
    using Halide::Tuple;
    namespace h = Halide;
    namespace p = boost::python;
    using p::self;



    return;
}
