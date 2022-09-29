#ifndef GETASSEMBLYINFOVIZ_H
#define GETASSEMBLYINFOVIZ_H

#include <fstream>
#include <map>
#include <regex>
#include <unordered_map>

#include "IROperator.h"
#include "IRVisitor.h"
#include "Module.h"

namespace Halide {

class Module;

namespace Internal {

struct AssemblyInfoForLoop {
    std::regex regex_start;  // regex to match the starting marker with
    std::regex regex_end;    // regex to match the ending marker
    const IRNode *node;      // node that the marker is associated with
};

struct AssemblyInfoProdCons {
    std::regex regex;    // regex to match the marker with
    const IRNode *node;  // node that the marker is associated with
};

struct ForLoopLineNumber {
    int start_line;  // line number of the start of the for loop
    int end_line;    // line number of the end of the for loop
};

class GetAssemblyInfoViz : public IRVisitor {

public:
    // generates the assembly info for the module
    void generate_assembly_information(const Module &m, const std::string &assembly_filename);

    // returns html content that contains the assembly code
    std::string get_assembly_html();

    // gets line numbers for producers/consumers + for loops
    int get_line_number_prod_cons(const IRNode *op);
    ForLoopLineNumber get_line_numbers_for_loops(const IRNode *op);

private:
    using IRVisitor::visit;

    // main html content
    std::stringstream assembly_HTML;

    // stores mapping of node to line number
    std::unordered_map<const IRNode *, int> node_to_line_number_prod_cons;
    std::unordered_map<const IRNode *, ForLoopLineNumber> node_to_line_numbers_for_loops;

    // stores the markers
    std::vector<AssemblyInfoForLoop> for_loop_markers;
    std::vector<AssemblyInfoProdCons> producer_consumer_markers;

    // for maping each node to unique marker in assembly
    int for_loop_count = 0;
    int producer_consumer_count = 0;

    // traverses the module to generate the assembly markers
    void traverse(const Module &m);

    // generates the assembly html and line numbers from the loaded assembly file
    // and generated markers
    void generate_assembly_html_and_line_numbers(const std::string &filename);

    // gets assembly file from stmt.viz.html file
    std::string get_assembly_filename(const std::string &filename);

    // checks if there is a marker that matches the assembly line, and if so, adds the line
    // number and node to map, signifying a match
    void add_line_number(std::string &assembly_line, int line_number);
    void add_line_number_for_loop(std::string &assembly_line, AssemblyInfoForLoop &marker,
                                  int line_number);
    void add_line_number_prod_cons(std::string &assembly_line, AssemblyInfoProdCons &marker,
                                   int line_number);

    void visit(const ProducerConsumer *op) override;
    void visit(const For *op) override;

    std::string print_node(const IRNode *node) const;
};

}  // namespace Internal
}  // namespace Halide

#endif
