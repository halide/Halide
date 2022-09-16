

#include "GetAssemblyInfoViz.h"

using namespace std;
using namespace Halide;
using namespace Internal;

void GetAssemblyInfoViz::generate_assembly_information(const Module &m,
                                                       const string &assembly_filename) {
    // traverse the module to get the assembly markers
    traverse(m);

    generate_assembly_html_and_line_numbers(assembly_filename);
}

string GetAssemblyInfoViz::get_assembly_html() {
    return assembly_HTML.str();
}

int GetAssemblyInfoViz::get_line_number_prod_cons(const IRNode *op) {
    auto it = node_to_line_number_prod_cons.find(op);
    if (it != node_to_line_number_prod_cons.end()) {
        return it->second;
    } else {
        return -1;
    }
}

ForLoopLineNumber GetAssemblyInfoViz::get_line_numbers_for_loops(const IRNode *op) {
    auto it = node_to_line_numbers_for_loops.find(op);
    if (it != node_to_line_numbers_for_loops.end()) {
        return it->second;
    } else {
        return {-1, -1};
    }
}

void GetAssemblyInfoViz::traverse(const Module &m) {

    // traverse all functions
    for (const auto &f : m.functions()) {
        f.body.accept(this);
    }
}

void GetAssemblyInfoViz::generate_assembly_html_and_line_numbers(const string &filename) {
    assembly_HTML << "<div id='assemblyContent' style='display: none;'>\n";
    assembly_HTML << "<pre>\n";

    ifstream assembly_file;
    string assembly_filename = get_assembly_filename(filename);
    assembly_file.open(assembly_filename, ios::in);

    if (assembly_file.is_open()) {
        string assembly_line;
        int line_number = 0;
        while (getline(assembly_file, assembly_line)) {
            line_number++;
            assembly_HTML << assembly_line << "\n";
            add_line_number(assembly_line, line_number);
        }
        assembly_file.close();
    }

    assembly_HTML << "</pre>\n";
    assembly_HTML << "</div>\n";
}

string GetAssemblyInfoViz::get_assembly_filename(const string &filename) {
    string assembly_filename = "./" + filename;
    assembly_filename.replace(assembly_filename.find(".stmt.viz.html"), 15, ".s");
    return assembly_filename;
}

void GetAssemblyInfoViz::add_line_number(string &assembly_line, int line_number) {
    for (auto &marker : for_loop_markers) {
        add_line_number_for_loop(assembly_line, marker, line_number);
    }
    for (auto &marker : producer_consumer_markers) {
        add_line_number_prod_cons(assembly_line, marker, line_number);
    }
}

void GetAssemblyInfoViz::add_line_number_for_loop(string &assembly_line,
                                                  AssemblyInfoForLoop &marker, int line_number) {
    // start of for loop
    if (std::regex_search(assembly_line, marker.regex_start)) {

        // check if marker is already present
        auto it = node_to_line_numbers_for_loops.find(marker.node);
        if (it == node_to_line_numbers_for_loops.end()) {
            ForLoopLineNumber for_loop_line_number;
            for_loop_line_number.start_line = line_number;
            node_to_line_numbers_for_loops[marker.node] = for_loop_line_number;
        } else {
            it->second.start_line = line_number;
        }
    }

    // end of for loop
    if (std::regex_search(assembly_line, marker.regex_end)) {

        // check if marker is already present
        auto it = node_to_line_numbers_for_loops.find(marker.node);
        if (it == node_to_line_numbers_for_loops.end()) {
            ForLoopLineNumber for_loop_line_number;
            for_loop_line_number.end_line = line_number;
            node_to_line_numbers_for_loops[marker.node] = for_loop_line_number;
        } else {
            it->second.end_line = line_number;
        }
    }
}
void GetAssemblyInfoViz::add_line_number_prod_cons(string &assembly_line,
                                                   AssemblyInfoProdCons &marker, int line_number) {
    if (std::regex_search(assembly_line, marker.regex)) {
        node_to_line_number_prod_cons[marker.node] = line_number;
    }
}

void GetAssemblyInfoViz::visit(const ProducerConsumer *op) {
    producer_consumer_count++;

    string assembly_marker = "%\"";
    assembly_marker += to_string(producer_consumer_count);
    assembly_marker += op->is_producer ? "_produce " : "_consume ";
    assembly_marker += op->name;

    // replace all $ with \$
    std::regex dollar("\\$");
    assembly_marker = std::regex_replace(assembly_marker, dollar, "\\$");

    std::regex regex(assembly_marker);

    AssemblyInfoProdCons info;
    info.regex = regex;
    info.node = op;

    producer_consumer_markers.push_back(info);

    op->body.accept(this);
}
void GetAssemblyInfoViz::visit(const For *op) {
    for_loop_count++;

    // start of for loop
    string assembly_marker_start = "%\"";
    assembly_marker_start += std::to_string(for_loop_count);
    assembly_marker_start += "_for " + op->name;

    // replace all $ with \$
    std::regex dollar("\\$");
    assembly_marker_start = std::regex_replace(assembly_marker_start, dollar, "\\$");

    std::regex regex_start(assembly_marker_start);

    // end of for loop
    string assembly_marker_end = "%\"";
    assembly_marker_end += std::to_string(for_loop_count);
    assembly_marker_end += "_end for " + op->name;

    // replace all $ with \$
    assembly_marker_end = std::regex_replace(assembly_marker_end, dollar, "\\$");

    std::regex regex_end(assembly_marker_end);

    AssemblyInfoForLoop info;
    info.regex_start = regex_start;
    info.regex_end = regex_end;
    info.node = op;

    for_loop_markers.push_back(info);

    op->body.accept(this);
}

string GetAssemblyInfoViz::print_node(const IRNode *node) const {
    stringstream s;
    IRNodeType type = node->node_type;
    if (type == IRNodeType::ProducerConsumer) {
        s << "ProducerConsumer";
        auto node1 = dynamic_cast<const ProducerConsumer *>(node);
        s << " " << node1->name;
    } else if (type == IRNodeType::For) {
        s << "For";
        auto node1 = dynamic_cast<const For *>(node);
        s << " " << node1->name;
    } else {
        s << "Unknown type ";
    }

    return s.str();
}
