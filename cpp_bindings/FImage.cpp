#include "FImage.h"

// declare the functions that live on the ml side

ML_FUNC1(makeIntImm);
ML_FUNC2(makeAdd);
ML_FUNC1(doPrint);
ML_FUNC1(makeVar);

namespace FImage {
    Expr Add(const Expr &a, const Expr &b) {
        return makeAdd(a, b);
    }

    Expr IntImm(int a) {
        return makeIntImm(MLVal::fromInt(a));
    }

    Expr Var(const char *a) {
        return makeVar(MLVal::fromString(a));
    }

    void print(const Expr &a) {
        doPrint(a);
    }
}
