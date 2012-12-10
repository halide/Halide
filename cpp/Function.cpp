#include "Function.h"

namespace Halide {
    namespace Internal {

        template<>
        int &ref_count<Function>(const Function *f) {return f->ref_count;}

        template<>
        void destroy<Function>(const Function *f) {delete f;}

    }
}
