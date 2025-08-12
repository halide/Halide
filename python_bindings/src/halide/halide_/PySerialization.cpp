#include "PySerialization.h"

namespace Halide {
namespace PythonBindings {

void define_serialization(py::module &m) {
    // Serialize pipeline functions
    m.def("serialize_pipeline",  //
          [](const Pipeline &pipeline, const std::string &filename, std::optional<bool> get_params) -> std::optional<std::map<std::string, Parameter>> {
              if (get_params.value_or(false)) {
                  std::map<std::string, Parameter> params;
                  serialize_pipeline(pipeline, filename, params);
                  return params;
              }
              serialize_pipeline(pipeline, filename);
              return {};  //
          },
          py::arg("pipeline"),                   //
          py::arg("filename"),                   //
          py::kw_only(),                         //
          py::arg("get_params") = std::nullopt,  //
          "Serialize a Halide pipeline to a file, optionally returning external parameters.");

    m.def("serialize_pipeline",  //
          [](const Pipeline &pipeline, std::optional<bool> get_params) -> std::variant<std::tuple<py::bytes, std::map<std::string, Parameter>>, py::bytes> {
              std::vector<uint8_t> data;
              if (get_params.value_or(false)) {
                  std::map<std::string, Parameter> params;
                  serialize_pipeline(pipeline, data, params);
                  py::bytes bytes_data = py::bytes(reinterpret_cast<const char *>(data.data()), data.size());
                  return std::make_tuple(bytes_data, params);
              }
              serialize_pipeline(pipeline, data);
              return py::bytes(reinterpret_cast<const char *>(data.data()), data.size());  //
          },
          py::arg("pipeline"),                   //
          py::kw_only(),                         //
          py::arg("get_params") = std::nullopt,  //
          "Serialize a Halide pipeline to bytes, optionally returning external parameters as a tuple.");

    // Deserialize pipeline functions
    m.def("deserialize_pipeline",  //
          [](const py::bytes &data, const std::map<std::string, Parameter> &user_params) -> Pipeline {
              // TODO: rework API in serialize_pipeline to take a std::span<> in C++20
              std::string_view view{data};
              std::vector<uint8_t> span{view.begin(), view.end()};
              return deserialize_pipeline(span, user_params);  //
          },
          py::arg("data"),                                              //
          py::arg("user_params") = std::map<std::string, Parameter>{},  //
          "Deserialize a Halide pipeline from bytes.");

    m.def("deserialize_pipeline",  //
          [](const std::string &filename, const std::map<std::string, Parameter> &user_params) -> Pipeline {
              return deserialize_pipeline(filename, user_params);  //
          },
          py::arg("filename"),                                          //
          py::arg("user_params") = std::map<std::string, Parameter>{},  //
          "Deserialize a Halide pipeline from a file.");

    // Deserialize parameters functions
    m.def("deserialize_parameters",  //
          [](const py::bytes &data) -> std::map<std::string, Parameter> {
              // TODO: rework API in serialize_pipeline to take a std::span<> in C++20
              std::string_view view{data};
              std::vector<uint8_t> span{view.begin(), view.end()};
              return deserialize_parameters(span);  //
          },
          py::arg("data"),  //
          "Deserialize external parameters from serialized pipeline bytes.");

    m.def("deserialize_parameters",  //
          [](const std::string &filename) -> std::map<std::string, Parameter> {
              return deserialize_parameters(filename);  //
          },
          py::arg("filename"),  //
          "Deserialize external parameters from a serialized pipeline file.");
}

}  // namespace PythonBindings
}  // namespace Halide
