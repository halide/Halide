#include "PyFuncTypeChanging.h"

namespace Halide {
namespace PythonBindings {

namespace {

inline Func to_func(const Buffer<> &b) {
    return lambda(_, b(_));
}

}  // namespace

void define_func_type_changing(py::module &m) {
    using namespace FuncTypeChanging;

    py::module bc = m.def_submodule("FuncTypeChanging");

    py::enum_<ChunkOrder>(bc, "ArgumentKind")
        .value("LowestFirst", ChunkOrder::LowestFirst)
        .value("HighestFirst", ChunkOrder::HighestFirst)
        .value("Default", ChunkOrder::Default);

    bc.def(
        "change_type",
        [](const ImageParam &im, const Type &dst_type, const Var &dim,
           const std::string &name, ChunkOrder chunk_order) -> Func {
            return change_type(im, dst_type, dim, name, chunk_order);
        },
        py::arg("f"), py::arg("dst_type"), py::arg("dim"), py::arg("name"),
        py::arg("chunk_order"));

    bc.def(
        "change_type",
        [](const Buffer<> &b, const Type &dst_type, const Var &dim,
           const std::string &name, ChunkOrder chunk_order) -> Func {
            return change_type(b, dst_type, dim, name, chunk_order);
        },
        py::arg("f"), py::arg("dst_type"), py::arg("dim"), py::arg("name"),
        py::arg("chunk_order"));

    bc.def(
        "change_type",
        [](const py::object &target, const Type &dst_type, const Var &dim,
           const std::string &name, ChunkOrder chunk_order) -> Func {
            try {
                return change_type(target.cast<Func>(), dst_type, dim, name,
                                   chunk_order);
            } catch (...) {
                // fall thru
            }
            try {
                return change_type(to_func(target.cast<Buffer<>>()), dst_type,
                                   dim, name, chunk_order);
            } catch (...) {
                // fall thru
            }
            throw py::value_error("Invalid arguments to change_type");
            return Func();
        },
        py::arg("f"), py::arg("dst_type"), py::arg("dim"), py::arg("name"),
        py::arg("chunk_order"));
}

}  // namespace PythonBindings
}  // namespace Halide
