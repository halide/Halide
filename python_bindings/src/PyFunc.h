#ifndef HALIDE_PYTHON_BINDINGS_PYFUNC_H
#define HALIDE_PYTHON_BINDINGS_PYFUNC_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_func();

template <typename FuncOrStage>
FuncOrStage &func_parallel0(FuncOrStage &that, VarOrRVar var) {
    return that.parallel(var);
}

template <typename FuncOrStage>
FuncOrStage &func_parallel1(FuncOrStage &that, VarOrRVar var, int factor) {
    return that.parallel(var, factor);
}

template <typename FuncOrStage>
FuncOrStage &func_split(FuncOrStage &that, VarOrRVar var, VarOrRVar outer, VarOrRVar inner, int factor) {
    return that.split(var, outer, inner, factor);
}

template <typename FuncOrStage>
FuncOrStage &func_vectorize0(FuncOrStage &that, VarOrRVar var) {
    return that.vectorize(var);
}

template <typename FuncOrStage>
FuncOrStage &func_vectorize1(FuncOrStage &that, VarOrRVar var, int factor) {
    return that.vectorize(var, factor);
}

template <typename FuncOrStage>
FuncOrStage &func_unroll0(FuncOrStage &that, VarOrRVar var) {
    return that.unroll(var);
}

template <typename FuncOrStage>
FuncOrStage &func_unroll1(FuncOrStage &that, VarOrRVar var, int factor) {
    return that.unroll(var, factor);
}

template <typename FuncOrStage>
FuncOrStage &func_tile0(FuncOrStage &that, VarOrRVar x, VarOrRVar y,
                        VarOrRVar xo, VarOrRVar yo,
                        VarOrRVar xi, VarOrRVar yi,
                        Expr xfactor, Expr yfactor) {
    return that.tile(x, y, xo, yo, xi, yi, xfactor, yfactor);
}

template <typename FuncOrStage>
FuncOrStage &func_tile1(FuncOrStage &that, VarOrRVar x, VarOrRVar y,
                        VarOrRVar xi, VarOrRVar yi,
                        Expr xfactor, Expr yfactor) {
    return that.tile(x, y, xi, yi, xfactor, yfactor);
}

template <typename FuncOrStage, typename PythonIterable>
FuncOrStage &func_reorder0(FuncOrStage &that, PythonIterable args_passed) {
    std::vector<VarOrRVar> var_or_rvar_args;

    const size_t args_len = py::len(args_passed);
    for (size_t i = 0; i < args_len; i += 1) {
        py::object o = args_passed[i];
        py::extract<VarOrRVar> var_or_rvar_extract(o);

        if (var_or_rvar_extract.check()) {
            var_or_rvar_args.push_back(var_or_rvar_extract());
        } else {
            for (size_t j = 0; j < args_len; j += 1) {
                py::object o = args_passed[j];
                const std::string o_str = py::extract<std::string>(py::str(o));
                printf("Func::reorder args_passed[%lu] == %s\n", j, o_str.c_str());
            }
            throw std::invalid_argument("Func::reorder() only handles a list of (convertible to) VarOrRVar.");
        }
    }

    return that.reorder(var_or_rvar_args);
}

template <typename FuncOrStage>
FuncOrStage &func_reorder1(FuncOrStage &that, py::object v0,
                           py::object v1, py::object v2, py::object v3, py::object v4, py::object v5) {
    py::list args_list;
    for (const py::object &v : { v0, v1, v2, v3, v4, v5 }) {
        if (not v.is_none()) {
            args_list.append(v);
        }
    }

    return func_reorder0<FuncOrStage, py::list>(that, args_list);
}

template <typename FuncOrStage, typename PythonIterable>
FuncOrStage &func_reorder_storage0(FuncOrStage &that, PythonIterable args_passed) {
    std::vector<Var> var_args;

    const size_t args_len = py::len(args_passed);
    for (size_t i = 0; i < args_len; i += 1) {
        py::object o = args_passed[i];
        py::extract<Var &> var_extract(o);

        if (var_extract.check()) {
            var_args.push_back(var_extract());
        } else {
            for (size_t j = 0; j < args_len; j += 1) {
                py::object o = args_passed[j];
                const std::string o_str = py::extract<std::string>(py::str(o));
                printf("Func::reorder_storage args_passed[%lu] == %s\n", j, o_str.c_str());
            }
            throw std::invalid_argument("Func::reorder_storage() only handles a list of (convertible to) Var.");
        }
    }

    return that.reorder_storage(var_args);
}

template <typename FuncOrStage>
FuncOrStage &func_reorder_storage1(FuncOrStage &that, py::object v0,
                                   py::object v1, py::object v2,
                                   py::object v3, py::object v4, py::object v5) {
    py::list args_list;
    for (const py::object &v : { v0, v1, v2, v3, v4, v5 }) {
        if (not v.is_none()) {
            args_list.append(v);
        }
    }

    return func_reorder_storage0<FuncOrStage, py::list>(that, args_list);
}

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYFUNC_H
