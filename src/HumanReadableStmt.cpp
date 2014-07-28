#include "HumanReadableStmt.h"

#include "IR.h"
#include "Func.h"
#include "Simplify.h"
#include "Substitute.h"
#include "Lower.h"

#include <map>
#include <string>

namespace Halide {
namespace Internal {

namespace {

class HumanReadableStmt {

    std::map<std::string, Expr> replacements;
    Stmt stmt;
    std::string name;
    typedef std::map<std::string, Expr>::iterator it_type;

public:
    static const std::map<std::string, Expr> default_map;

    HumanReadableStmt(std::string name, Stmt s, buffer_t *buft, std::map<std::string, Expr> additional_replacements = default_map) {
        this->stmt = s;
        this->name = name;
        add_replacements(generic_replacements(buft));
        add_replacements(additional_replacements);
    }

    HumanReadableStmt(std::string name, Stmt s, std::map<std::string, Expr> additional_replacements = default_map) {
        this->stmt = s;
        this->name = name;
        add_replacements(additional_replacements);
    }

    void add_replacements(std::map<std::string, Expr> m) {
        if (m.size() ==0) return;
        for (it_type iterator = m.begin(); iterator != m.end(); iterator++) {
            replacements[iterator->first] = iterator->second;
        }   
    }

    std::map<std::string, Expr> generic_replacements(buffer_t *buft) {
        std::map<std::string, Expr>temp;
        temp[name+".min.0"] = IntImm::make(buft->min[0]);
        temp[name+".min.1"] = IntImm::make(buft->min[1]);
        temp[name+".min.2"] = IntImm::make(buft->min[2]);
        temp[name+".min.3"] = IntImm::make(buft->min[3]);

        temp[name+".stride.0"] = IntImm::make(buft->stride[0]);
        temp[name+".stride.1"] = IntImm::make(buft->stride[1]);
        temp[name+".stride.2"] = IntImm::make(buft->stride[2]);
        temp[name+".stride.3"] = IntImm::make(buft->stride[3]);

        temp[name+".extent.0"] = IntImm::make(buft->extent[0]);
        temp[name+".extent.1"] = IntImm::make(buft->extent[1]);
        temp[name+".extent.2"] = IntImm::make(buft->extent[2]);
        temp[name+".extent.3"] = IntImm::make(buft->extent[3]);

        temp[name+".elem_size"] = IntImm::make(buft->elem_size);

        // To remove if null rewrite buffer stmt uncomment the following line.
        // temp[name+".host_and_dev_are_null"] = const_false(); 

        return temp;
    }

    Stmt execute() {
        stmt = substitute(replacements, stmt);
        return stmt;
    }   
};

const std::map<std::string, Expr> HumanReadableStmt::default_map;

}


Stmt human_readable_stmt(std::string name, Stmt s, buffer_t *buft) {
    return human_readable_stmt(name, s, buft, HumanReadableStmt::default_map);
}

Stmt human_readable_stmt(std::string name, Stmt s, buffer_t *buft, std::map<std::string, Expr> additional_replacements) {
    HumanReadableStmt hrs(name, s, buft, additional_replacements);
    debug(3) << "Generating Human Readable function " << name << " ...\n";
    Stmt s_new = simplify(hrs.execute()); 
    debug(3) << s_new;
    debug(3) << "Done outputting Human Readable function " << name << "\n";

    return s_new; 
}

}}
