#include "Func_Stage.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>
//#include "add_operators.h"

#include "../../src/Func.h"

#include <vector>
#include <string>

namespace h = Halide;
namespace p = boost::python;
using p::self;

void defineStage()
{
    using Halide::Stage;

    // only defined so that boost::python knows about these classes,
    // not (yet) meant to be created or manipulated by the user
    p::class_<Stage>("Stage", p::no_init);

    return;
}
