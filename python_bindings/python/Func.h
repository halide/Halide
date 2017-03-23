#ifndef FUNC_H
#define FUNC_H

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>

#include <boost/python/tuple.hpp>
#include <string>
#include <vector>

#include "Halide.h"

void defineFunc();

namespace func_and_stage_implementation_details {
// These are methods shared with Stage

// we use hh and bp to avoid colisions with h, b used in the rest of the code
namespace hh = Halide;
namespace bp = boost::python;

template <typename FuncOrStage>
FuncOrStage &func_parallel0(FuncOrStage &that, hh::VarOrRVar var) {
    return that.parallel(var);
}

template <typename FuncOrStage>
FuncOrStage &func_parallel1(FuncOrStage &that, hh::VarOrRVar var, int factor) {
    return that.parallel(var, factor);
}

template <typename FuncOrStage>
FuncOrStage &func_split(FuncOrStage &that, hh::VarOrRVar var, hh::VarOrRVar outer, hh::VarOrRVar inner, int factor) {
    return that.split(var, outer, inner, factor);
}

template <typename FuncOrStage>
FuncOrStage &func_vectorize0(FuncOrStage &that, hh::VarOrRVar var) {
    return that.vectorize(var);
}

template <typename FuncOrStage>
FuncOrStage &func_vectorize1(FuncOrStage &that, hh::VarOrRVar var, int factor) {
    return that.vectorize(var, factor);
}

template <typename FuncOrStage>
FuncOrStage &func_unroll0(FuncOrStage &that, hh::VarOrRVar var) {
    return that.unroll(var);
}

template <typename FuncOrStage>
FuncOrStage &func_unroll1(FuncOrStage &that, hh::VarOrRVar var, int factor) {
    return that.unroll(var, factor);
}

template <typename FuncOrStage>
FuncOrStage &func_tile0(FuncOrStage &that, hh::VarOrRVar x, hh::VarOrRVar y,
                        hh::VarOrRVar xo, hh::VarOrRVar yo,
                        hh::VarOrRVar xi, hh::VarOrRVar yi,
                        hh::Expr xfactor, hh::Expr yfactor) {
    return that.tile(x, y, xo, yo, xi, yi, xfactor, yfactor);
}

template <typename FuncOrStage>
FuncOrStage &func_tile1(FuncOrStage &that, hh::VarOrRVar x, hh::VarOrRVar y,
                        hh::VarOrRVar xi, hh::VarOrRVar yi,
                        hh::Expr xfactor, hh::Expr yfactor) {
    return that.tile(x, y, xi, yi, xfactor, yfactor);
}

template <typename FuncOrStage, typename PythonIterable>
FuncOrStage &func_reorder0(FuncOrStage &that, PythonIterable args_passed) {
    std::vector<hh::VarOrRVar> var_or_rvar_args;

    const size_t args_len = bp::len(args_passed);
    for (size_t i = 0; i < args_len; i += 1) {
        bp::object o = args_passed[i];
        bp::extract<hh::VarOrRVar> var_or_rvar_extract(o);

        if (var_or_rvar_extract.check()) {
            var_or_rvar_args.push_back(var_or_rvar_extract());
        } else {
            for (size_t j = 0; j < args_len; j += 1) {
                bp::object o = args_passed[j];
                const std::string o_str = bp::extract<std::string>(bp::str(o));
                printf("Func::reorder args_passed[%lu] == %s\n", j, o_str.c_str());
            }
            throw std::invalid_argument("Func::reorder() only handles a list of (convertible to) VarOrRVar.");
        }
    }

    return that.reorder(var_or_rvar_args);
}

template <typename FuncOrStage>
FuncOrStage &func_reorder1(FuncOrStage &that, bp::object v0,
                           bp::object v1, bp::object v2, bp::object v3, bp::object v4, bp::object v5) {
    bp::list args_list;
    for (const bp::object &v : { v0, v1, v2, v3, v4, v5 }) {
        if (not v.is_none()) {
            args_list.append(v);
        }
    }

    return func_reorder0<FuncOrStage, bp::list>(that, args_list);
}

template <typename FuncOrStage, typename PythonIterable>
FuncOrStage &func_reorder_storage0(FuncOrStage &that, PythonIterable args_passed) {
    std::vector<hh::Var> var_args;

    const size_t args_len = bp::len(args_passed);
    for (size_t i = 0; i < args_len; i += 1) {
        bp::object o = args_passed[i];
        bp::extract<hh::Var &> var_extract(o);

        if (var_extract.check()) {
            var_args.push_back(var_extract());
        } else {
            for (size_t j = 0; j < args_len; j += 1) {
                bp::object o = args_passed[j];
                const std::string o_str = bp::extract<std::string>(bp::str(o));
                printf("Func::reorder_storage args_passed[%lu] == %s\n", j, o_str.c_str());
            }
            throw std::invalid_argument("Func::reorder_storage() only handles a list of (convertible to) Var.");
        }
    }

    return that.reorder_storage(var_args);
}

template <typename FuncOrStage>
FuncOrStage &func_reorder_storage1(FuncOrStage &that, bp::object v0,
                                   bp::object v1, bp::object v2,
                                   bp::object v3, bp::object v4, bp::object v5) {
    bp::list args_list;
    for (const bp::object &v : { v0, v1, v2, v3, v4, v5 }) {
        if (not v.is_none()) {
            args_list.append(v);
        }
    }

    return func_reorder_storage0<FuncOrStage, bp::list>(that, args_list);
}

}  // end of namespace func_and_stage_implementation_details

#endif  // FUNC_H
