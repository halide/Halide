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

struct AssemblyInfo {
    std::regex regex;    // regex to match the label
    const IRNode *node;  // node that the label is associated with
};

class GetAssemblyInfoViz : public IRVisitor {

public:
    void generate_assembly_information(const Module &m, const string &assembly_filename) {
        // traverse the module to get the assembly labels
        traverse(m);

        generate_assembly_html_and_line_numbers(assembly_filename);
    }

    string get_assembly_html() {
        return assembly_HTML.str();
    }

    int get_line_number(const IRNode *op) {
        auto it = node_to_line_number.find(op);
        if (it != node_to_line_number.end()) {
            return it->second;
        } else {
            return -1;
        }
    }

private:
    unordered_map<const IRNode *, int> node_to_line_number;
    vector<AssemblyInfo> labels;
    stringstream assembly_HTML;
    vector<const IRNode *> claimed_nodes;

    // traverses the module to generate the assembly labels
    void traverse(const Module &m) {
        // recursively traverse all submodules
        for (const auto &s : m.submodules()) {
            traverse(s);
        }

        // traverse all functions
        for (const auto &f : m.functions()) {
            f.body.accept(this);
        }
    }

    // generates the assembly html and line numbers from the loaded assembly file
    // and generated labels
    void generate_assembly_html_and_line_numbers(const string &filename) {
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

    // gets assembly file from stmt.viz.html file
    string get_assembly_filename(const string &filename) {
        string assembly_filename = "./" + filename;
        assembly_filename.replace(assembly_filename.find(".stmt.viz.html"), 15, ".s");
        return assembly_filename;
    }

    //  checks if there is a label that matches the assembly line, and if so, adds the line
    // number and node to map, signifying a match
    void add_line_number(string &assembly_line, int line_number) {
        for (auto &label : labels) {
            if (std::regex_search(assembly_line, label.regex)) {
                // only add if the label.op isn't already claimed by another line
                if (std::find(claimed_nodes.begin(), claimed_nodes.end(), label.node) ==
                    claimed_nodes.end()) {
                    claimed_nodes.push_back(label.node);
                    node_to_line_number[label.node] = line_number;
                    return;
                }
            }
        }
    }

    void visit(const ProducerConsumer *op) override {
        string code_string;
        code_string += op->is_producer ? "produce " : "consume ";
        code_string += op->name;

        // replace all $ with \$
        std::regex dollar("\\$");
        code_string = std::regex_replace(code_string, dollar, "\\$");

        std::regex regex("(\")" + code_string + "[0-9]*\"");

        AssemblyInfo info;
        info.regex = regex;
        info.node = op;

        labels.push_back(info);

        op->body.accept(this);
    }
    void visit(const For *op) override {
        string code_string = "%\"for " + op->name;

        // replace all $ with \$
        std::regex dollar("\\$");
        code_string = std::regex_replace(code_string, dollar, "\\$");

        std::regex regex(code_string + "(.preheader)*[0-9]*\"");

        AssemblyInfo info;
        info.regex = regex;
        info.node = op;

        labels.push_back(info);

        op->body.accept(this);
    }

    string print_node(const IRNode *node) const {
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
};

#endif
