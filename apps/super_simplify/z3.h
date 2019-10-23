#ifndef Z3_H
#define Z3_H

#include "Halide.h"

// Wrapper to use Z3 to do satisfiability queries on Halide Exprs

enum class Z3Result {
    Sat, Unsat, Unknown
};

inline std::ostream &operator<<(std::ostream &s, Z3Result r) {
    switch (r) {
    case Z3Result::Sat:
        s << "Sat";
        return s;
    case Z3Result::Unsat:
        s << "Unsat";
        return s;
    case Z3Result::Unknown:
        s << "Unknown";
        return s;
    }
}

Z3Result satisfy(Halide::Expr constraint, std::map<std::string, Halide::Expr> *result);

#endif
