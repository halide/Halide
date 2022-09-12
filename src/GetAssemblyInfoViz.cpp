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
    void generate_assembly_information(const Module &m, const string &assemblyFileName) {
        // traverse the module to get the assembly labels
        traverse(m);

        generate_assembly_html_and_line_numbers(assemblyFileName);
    }

    string get_assembly_html() {
        return assemblyHTML;
    }

    int get_line_number(const IRNode *op) {
        auto it = nodeToLineNumber.find(op);
        if (it != nodeToLineNumber.end()) {
            return it->second;
        } else {
            return -1;
        }
    }

private:
    unordered_map<const IRNode *, int> nodeToLineNumber;
    vector<AssemblyInfo> labels;
    string assemblyHTML;
    vector<const IRNode *> claimedNodes;

    string get_assembly_file_name(const string &filename) {
        string assemblyFileName = "./" + filename;
        assemblyFileName.replace(assemblyFileName.find(".stmt.viz.html"), 15, ".s");
        return assemblyFileName;
    }
    void generate_assembly_html_and_line_numbers(const string &filename) {
        assemblyHTML += "<div id='assemblyContent' style='display: none;'>\n";
        assemblyHTML += "<pre>\n";

        ifstream assemblyFile;
        string assemblyFileName = get_assembly_file_name(filename);
        assemblyFile.open(assemblyFileName, ios::in);

        if (assemblyFile.is_open()) {
            string assemblyLine;
            int lineNumber = 0;
            while (getline(assemblyFile, assemblyLine)) {
                lineNumber++;
                assemblyHTML += assemblyLine + "\n";
                add_line_number(assemblyLine, lineNumber);
            }
            assemblyFile.close();  // close the file object.
        }
        assemblyHTML += "</pre>\n";
        assemblyHTML += "</div>\n";
    }
    void add_line_number(string &assemblyLine, int lineNumber) {
        for (auto &label : labels) {
            if (std::regex_search(assemblyLine, label.regex)) {
                // only add if the label.op isn't already claimed by another line
                if (std::find(claimedNodes.begin(), claimedNodes.end(), label.node) ==
                    claimedNodes.end()) {
                    claimedNodes.push_back(label.node);
                    nodeToLineNumber[label.node] = lineNumber;
                    return;
                }
            }
        }
    }

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
    void visit(const ProducerConsumer *op) override {
        string codeString;
        codeString += op->is_producer ? "produce " : "consume ";
        codeString += op->name;

        // replace all $ with \$
        std::regex dollar("\\$");
        codeString = std::regex_replace(codeString, dollar, "\\$");

        std::regex regex("(\")" + codeString + "[0-9]*\"");
        string regexString = codeString + "[0-9]*\"";

        AssemblyInfo info;
        info.regex = regex;
        info.node = op;

        labels.push_back(info);

        op->body.accept(this);
    }
    void visit(const For *op) override {
        string codeString = "%\"for " + op->name;

        // replace all $ with \$
        std::regex dollar("\\$");
        codeString = std::regex_replace(codeString, dollar, "\\$");

        std::regex regex(codeString + "(.preheader)*[0-9]*\"");
        string regexString = codeString + "(.preheader)*[0-9]*\"";

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
