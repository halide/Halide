#ifndef HALIDE_PRINTER_H
#define HALIDE_PRINTER_H

#include <vector>
#include <string>

#include <Halide.h>
using namespace Halide;

class Printer {
public:
    Printer() = default;

    void print_pipeline(const Halide::Pipeline& pipeline);

    void print_function(const Halide::Internal::Function& function);

    void print_type(const Halide::Type& type);

    void print_stmt(const Halide::Internal::Stmt& stmt);
};


#endif
