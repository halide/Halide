#include "interpreter/model.h"
#include "util/error_util.h"

#include <cmath>
#include <list>

namespace hannk {

Tensor *apply(const TensorMap &map, const Tensor *t) {
    auto i = map.find(t);
    if (i != map.end()) {
        return i->second;
    }
    // TODO: Try to do this without const_cast?
    return const_cast<Tensor *>(t);
}

Model::Model(const Model &copy) {
    // First, just copy all the tensors (shared pointers).
    tensors = copy.tensors;

    // Next, clone the non-allocated tensors. These might get intermediate state
    // while being executed.
    TensorMap map;
    for (auto &i : tensors) {
        if (!i->is_allocated()) {
            auto cloned = std::make_shared<Tensor>(*i);
            map[i.get()] = cloned.get();
            i = cloned;
        }
    }

    // Now copy the ops, using the tensor map we made above.
    for (const auto &i : copy.ops) {
        ops.push_back(i->clone(map));
    }
}

void Model::dump(std::ostream &os) {
    os << "Tensors: " << std::endl;
    for (const auto &i : tensors) {
        i->dump(os);
    }

    os << "Ops: " << std::endl;
    for (const auto &i : ops) {
        i->dump(os);
    }
    os << std::endl;
}

void Tensor::dump(std::ostream &os) const {
    os << "  \"" << name() << "\" : "
       << "  " << buffer_.type() << " x ";

    const auto *b = buffer_.raw_buffer();
    os << '{';
    for (int i = 0; i < b->dimensions; i++) {
        if (i > 0) {
            os << ", ";
        }
        os << b->dim[i];
    }
    os << '}';

    os << (is_allocated() ? " allocated " : " ") << name() << std::endl;
}

}  // namespace hannk
