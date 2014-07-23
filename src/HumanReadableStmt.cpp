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
    Stmt s;
    std::string name;
    typedef std::map<std::string, Expr>::iterator it_type;


    public:
        // since making true and false seems pretty roundabout 
        // i'll just create static member variables people can use
        static const Expr True, False;
        static const std::map<std::string, Expr> default_map;

        HumanReadableStmt(std::string name, Stmt s, buffer_t *buft, std::map<std::string, Expr> additional_replacements = default_map) {
            this->s = s;
            this->name = name;
            add_replacements(generic_replacements(buft));
            add_replacements(additional_replacements);
        }

        HumanReadableStmt(std::string name, Stmt s, std::map<std::string, Expr> additional_replacements = default_map) {
            this->s = s;
            this->name = name;
            add_replacements(additional_replacements);
        }

        void add_replacements(std::map<std::string, Expr> m) {
            if (m.size() ==0) return;
            for(it_type iterator = m.begin(); iterator != m.end(); iterator++) {
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

            // this removes the annoying if null rewrite buffer stmt
            // temp[name+".host_and_dev_are_null"] = False; 

            return temp;
        }

        Stmt execute() {
            s = substitute(replacements, s);
            return s;
        }   
};

const Expr HumanReadableStmt::True  = Cast::make(Bool(1), IntImm::make(1));
const Expr HumanReadableStmt::False = Cast::make(Bool(1), IntImm::make(0));
const std::map<std::string, Expr> HumanReadableStmt::default_map;

}


Stmt human_readable_stmt(std::string name, Stmt s, buffer_t *buft) {
    return human_readable_stmt(name, s, buft, HumanReadableStmt::default_map);
}

Stmt human_readable_stmt(std::string name, Stmt s, buffer_t *buft, std::map<std::string, Expr> additional_replacements) {
    HumanReadableStmt hrs(name, s, buft, additional_replacements);
    debug(3) << "Genererating Human Readable function " << name << " ...\n";
    Stmt s_new = simplify(hrs.execute()); 
    debug(3) << s_new;
    debug(3) << "Done ouputing Human Readable function " << name << "\n";

    return s_new; 
}

Stmt human_readable_stmt(Func f,  Realization dst, std::map<std::string, Expr> additional_replacements, const Target &t) {
    _halide_user_assert(f.outputs() == 1) << "Handling Multiple Outputs hasn't been built in yet\n";
    // i purposfully don't use f.lowered because i want a copy of the STMT since it will later get modified 
    Stmt s = Halide::Internal::lower(f.function(), t);

    return human_readable_stmt(f.name(), s, (buffer_t *)dst[0].raw_buffer(), additional_replacements);
}

Stmt human_readable_stmt(Func f,  Realization dst, const Target &t) {
    return human_readable_stmt(f, dst, HumanReadableStmt::default_map, t);
}

template<typename T>
Stmt human_readable_stmt(Func f, Image<T> dst, std::map<std::string, Expr> additional_replacements, const Target &target) {
    return human_readable_stmt(f, Realization(vec<Buffer>(Buffer(dst))), additional_replacements, target);
}

template<typename T> 
Stmt human_readable_stmt(Func f, Image<T> dst, const Target &target) {
    return human_readable_stmt(f, dst, HumanReadableStmt::default_map, target);
}

Stmt human_readable_stmt(Func f, int x_size, int y_size, int z_size, int w_size, std::map<std::string, Expr> additional_replacements, const Target &t) {
    _halide_user_assert(f.defined()) << "Can't realize undefined Func.\n";
    std::vector<Buffer> outputs(f.outputs());
    for (size_t i = 0; i < outputs.size(); i++) {
        outputs[i] = Buffer(f.output_types()[i], x_size, y_size, z_size, w_size);
    }
    Realization r(outputs);
    return human_readable_stmt(f, r, additional_replacements, t);
}

Stmt human_readable_stmt(Func f, int x_size, int y_size, int z_size, int w_size, const Target &t) {
    return human_readable_stmt(f, x_size, y_size, z_size, w_size, HumanReadableStmt::default_map, t);
}

Stmt human_readable_stmt(Func f, int x_size, int y_size, int z_size, std::map<std::string, Expr> additional_replacements, const Target &t) {
    return human_readable_stmt(f, x_size, y_size, z_size, 0, additional_replacements, t);
}

Stmt human_readable_stmt(Func f, int x_size, int y_size, int z_size,  const Target &t) {
    return human_readable_stmt(f, x_size, y_size, z_size, 0, HumanReadableStmt::default_map, t);
}

Stmt human_readable_stmt(Func f, int x_size, int y_size, std::map<std::string, Expr> additional_replacements, const Target &t) {
    return human_readable_stmt(f, x_size, y_size, 0, 0, additional_replacements, t);
}

Stmt human_readable_stmt(Func f, int x_size, int y_size, const Target &t) {
    return human_readable_stmt(f, x_size, y_size, 0, 0, HumanReadableStmt::default_map, t);
}

Stmt human_readable_stmt(Func f, int x_size, std::map<std::string, Expr> additional_replacements, const Target &t) {
    return human_readable_stmt(f, x_size, 0, 0, 0, additional_replacements, t);
}

Stmt human_readable_stmt(Func f, int x_size, const Target &t) {
    return human_readable_stmt(f, x_size, 0, 0, 0, HumanReadableStmt::default_map, t);
}

}}