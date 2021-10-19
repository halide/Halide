#include "interpreter/model.h"
#include "interpreter/ops.h"
#include "interpreter/transforms.h"
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

namespace {

int index_of_tensor(const std::vector<TensorPtr> &v, const TensorPtr &t) {
    for (size_t i = 0; i < v.size(); i++) {
        if (t == v[i]) {
            return (int)i;
        }
    }
    return -1;
}

}  // namespace

int Op::index_of_input(const TensorPtr &t) const {
    return index_of_tensor(inputs_, t);
}

int Op::index_of_output(const TensorPtr &t) const {
    return index_of_tensor(outputs_, t);
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
        Op *sub_op = it->get();
        for (int output_idx = 0; output_idx < to_add->output_count(); output_idx++) {
            const TensorPtr &output = to_add->output(output_idx);
            if (sub_op->is_input(output)) {
                // sub_op directly consumes at least one output of to_add.
                ops_.insert(it, std::move(to_add));
                return nullptr;
            }
        }
        if (OpGroup *group = cast_op<OpGroup>(sub_op)) {
            to_add = group->add(std::move(to_add));
            if (to_add == nullptr) {
                // there is a nested OpGroup that consumes the output to_add.
                return nullptr;
            }
        }
    }
    // Generally an error case, but caller should deal with it
    return to_add;
}

bool OpGroup::remove(const Op *to_remove) {
    for (auto it = ops_.begin(); it != ops_.end(); ++it) {
        Op *sub_op = it->get();
        if (sub_op == to_remove) {
            ops_.erase(it);
            return true;
        }
        if (OpGroup *group = cast_op<OpGroup>(sub_op)) {
            if (group->remove(to_remove)) {
                return true;
            }
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
