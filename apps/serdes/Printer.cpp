#include "Printer.h"
#include <iostream>

void Printer::print_pipeline(const Halide::Pipeline& pipeline) {
  std::cout << "Printing pipeline\n";
  std::cout << "outputs: [Func]\n";
  std::cout << pipeline.outputs().size() << "\n";
  for (const auto& func : pipeline.outputs()) {
    this->print_function(func.function());
  }
}

void Printer::print_function(const Halide::Internal::Function& function) {
  std::cout << "Printing function\n";
  std::cout << "name: " << function.name() << "\n";
  std::cout << "origin_name: " << function.origin_name() << "\n";
  std::cout << "output_types: [Type]\n";
  for (const auto& type : function.output_types()) {
    this->print_type(type);
  }
  std::cout << "required_types: [Type]\n";
  for (const auto& type : function.required_types()) {
    this->print_type(type);
  }
  std::cout << "required_dimensions: " << function.required_dimensions() << "\n";
  std::cout << "args: [string]\n";
  for (const auto& arg : function.args()) {
    std::cout << arg << "\n";
  }
}

void Printer::print_type(const Halide::Type& type) {
  std::cout << "Printing type\n";
  std::cout << "bits: " << type.bits() << "\n";
  std::cout << "lanes: " << type.lanes() << "\n";
  switch (type.code()) {
    case Halide::Type::Int:
      std::cout << "code: Int\n";
      break;
    case Halide::Type::UInt:
      std::cout << "code: UInt\n";
      break;
    case Halide::Type::Float:
      std::cout << "code: Float\n";
      break;
    case Halide::Type::Handle:
      std::cout << "code: Handle\n";
      break;
    case Halide::Type::BFloat:
      std::cout << "code: BFloat\n";
      break;
    default:
      std::cout << "code: Unknown\n";
      break;
  }

}