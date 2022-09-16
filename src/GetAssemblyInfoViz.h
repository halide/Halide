#ifndef GETASSEMBLYINFOVIZ_H
#define GETASSEMBLYINFOVIZ_H

#include <fstream>
#include <map>
#include <regex>
#include <unordered_map>

#include "IROperator.h"
#include "IRVisitor.h"
#include "Module.h"

using namespace std;
using namespace Halide;
using namespace Internal;

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
    void generate_assembly_information(const Module &m, const string &assembly_filename);

    string get_assembly_html();

    // gets line numbers for producers and consumers + for loops
    int get_line_number_prod_cons(const IRNode *op);
    ForLoopLineNumber get_line_numbers_for_loops(const IRNode *op);

private:
    unordered_map<const IRNode *, int> node_to_line_number_prod_cons;
    unordered_map<const IRNode *, ForLoopLineNumber> node_to_line_numbers_for_loops;
    vector<AssemblyInfoForLoop> for_loop_markers;
    vector<AssemblyInfoProdCons> producer_consumer_markers;
    stringstream assembly_HTML;

    // for maping each node to unique marker in assembly
    int for_loop_count = 0;
    int producer_consumer_count = 0;

    // traverses the module to generate the assembly markers
    void traverse(const Module &m);

    // generates the assembly html and line numbers from the loaded assembly file
    // and generated markers
    void generate_assembly_html_and_line_numbers(const string &filename);

    // gets assembly file from stmt.viz.html file
    string get_assembly_filename(const string &filename);

    //  checks if there is a marker that matches the assembly line, and if so, adds the line
    // number and node to map, signifying a match
    void add_line_number(string &assembly_line, int line_number);
    void add_line_number_for_loop(string &assembly_line, AssemblyInfoForLoop &marker,
                                  int line_number);
    void add_line_number_prod_cons(string &assembly_line, AssemblyInfoProdCons &marker,
                                   int line_number);

    void visit(const ProducerConsumer *op) override;
    void visit(const For *op) override;

    string print_node(const IRNode *node) const;
};

#endif
