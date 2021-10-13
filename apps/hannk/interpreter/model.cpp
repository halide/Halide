#include "interpreter/model.h"
#include "interpreter/ops.h"
#include "util/error_util.h"

#include <cmath>
#include <list>

namespace hannk {

Op::Op(std::vector<TensorPtr> inputs, std::vector<TensorPtr> outputs)
    : inputs_(std::move(inputs)), outputs_(std::move(outputs)) {
    for (auto &i : inputs_) {
        if (!i) continue;
        i->add_consumer(this);
    }
    for (auto &i : outputs_) {
        if (!i) continue;
        i->add_producer(this);
    }
}

Op::~Op() {
    for (auto &i : inputs_) {
        if (!i) continue;
        i->remove_consumer(this);
    }
    for (auto &i : outputs_) {
        if (!i) continue;
        i->remove_producer(this);
    }
}

void Op::set_input(int idx, TensorPtr t) {
    if (inputs_[idx]) {
        inputs_[idx]->remove_consumer(this);
    }
    inputs_[idx] = std::move(t);
    if (inputs_[idx]) {
        inputs_[idx]->add_consumer(this);
    }
}

void Op::set_output(int idx, TensorPtr t) {
    if (outputs_[idx]) {
        outputs_[idx]->remove_producer(this);
    }
    outputs_[idx] = std::move(t);
    if (outputs_[idx]) {
        outputs_[idx]->add_producer(this);
    }
}

void Op::set_input(TensorPtr t) {
    set_input(0, std::move(t));
}

void Op::set_output(TensorPtr t) {
    set_output(0, std::move(t));
}

bool Op::is_input(const TensorPtr &t) const {
    for (auto &i : inputs_) {
        if (i == t) {
            return true;
        }
    }
    return false;
}

bool Op::is_output(const TensorPtr &t) const {
    for (auto &o : outputs_) {
        if (o == t) {
            return true;
        }
    }
    return false;
}

void Op::dump(std::ostream &os, int indent) const {
    const std::string spaces(indent, ' ');

    os << spaces << "OP:" << this->name() << "\n";
    for (auto t : this->inputs_) {
        os << spaces << "  (i:) ";
        t->dump(os);
    }
    for (auto t : this->outputs_) {
        os << spaces << "  (o:) ";
        t->dump(os);
    }
    os << "\n";
}

bool Op::consumes_output_of(const Op *op) const {
    for (const auto &o : op->outputs_) {
        for (const auto &i : this->inputs_) {
            if (i == o) {
                // i consumes an output of op.
                return true;
            }
        }
    }
    return false;
}

void OpGroup::execute() {
    for (int i = 0; i < op_count(); i++) {
        op(i)->execute();
    }
}

BoundsMap OpGroup::map_bounds(int input_idx, int output_idx) const {
    BoundsMap result(input(input_idx)->rank(), output(output_idx)->rank());
    // TODO
    return result;
}

OpPtr OpGroup::add(OpPtr to_add) {
    assert(to_add != nullptr);
    for (auto it = ops_.begin(); it != ops_.end(); ++it) {
        if ((*it)->consumes_output_of(to_add.get())) {
            // i directly consumes the output of to_add.
            ops_.insert(it, std::move(to_add));
            return nullptr;
        }
        to_add = (*it)->add(std::move(to_add));
        if (to_add == nullptr) {
            // there is a nested Op that consumes the output to_add.
            return nullptr;
        }
    }
    // Generally an error case, but caller should deal with it
    return to_add;
}

bool OpGroup::remove(const Op *to_remove) {
    for (auto it = ops_.begin(); it != ops_.end(); ++it) {
        if (it->get() == to_remove) {
            ops_.erase(it);
            return true;
        }
        if ((*it)->remove(to_remove)) {
            return true;
        }
    }
    // Generally an error case, but caller should deal with it
    return false;
}

void OpGroup::accept(OpVisitor *v) {
    v->visit(this);
}

void OpGroup::dump(std::ostream &os, int indent) const {
    Op::dump(os, indent);
    for (const auto &i : ops_) {
        i->dump(os, indent + 4);
    }
    os << "\n";
}

}  // namespace hannk
