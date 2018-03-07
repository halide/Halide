#include "PyOutputs.h"

namespace Halide {
namespace PythonBindings {

void define_outputs(py::module &m) {
    auto outputs_class = py::class_<Outputs>(m, "Outputs")
        .def(py::init<>())
        .def(py::init([](const std::string &object_name,
                         const std::string &assembly_name,
                         const std::string &bitcode_name,
                         const std::string &llvm_assembly_name,
                         const std::string &c_header_name,
                         const std::string &c_source_name,
                         const std::string &stmt_name,
                         const std::string &stmt_html_name,
                         const std::string &static_library_name,
                         const std::string &schedule_name) -> Outputs {
            Outputs o;
            o.object_name = object_name;
            o.assembly_name = assembly_name;
            o.bitcode_name = bitcode_name;
            o.llvm_assembly_name = llvm_assembly_name;
            o.c_header_name = c_header_name;
            o.c_source_name = c_source_name;
            o.stmt_name = stmt_name;
            o.stmt_html_name = stmt_html_name;
            o.static_library_name = static_library_name;
            o.schedule_name = schedule_name;
            return o;
        }),
            py::arg("object_name") = "",
            py::arg("assembly_name") = "",
            py::arg("bitcode_name") = "",
            py::arg("llvm_assembly_name") = "",
            py::arg("c_header_name") = "",
            py::arg("c_source_name") = "",
            py::arg("stmt_name") = "",
            py::arg("stmt_html_name") = "",
            py::arg("static_library_name") = "",
            py::arg("schedule_name") = ""
        )
        .def_readwrite("object_name", &Outputs::object_name)
        .def_readwrite("assembly_name", &Outputs::assembly_name)
        .def_readwrite("bitcode_name", &Outputs::bitcode_name)
        .def_readwrite("llvm_assembly_name", &Outputs::llvm_assembly_name)
        .def_readwrite("c_header_name", &Outputs::c_header_name)
        .def_readwrite("c_source_name", &Outputs::c_source_name)
        .def_readwrite("stmt_name", &Outputs::stmt_name)
        .def_readwrite("stmt_html_name", &Outputs::stmt_html_name)
        .def_readwrite("static_library_name", &Outputs::static_library_name)
        .def_readwrite("schedule_name", &Outputs::schedule_name)
        .def("__repr__", [](const Outputs &o) -> std::string {
            return "<halide.Outputs>";
        })
    ;

}

}  // namespace PythonBindings
}  // namespace Halide
