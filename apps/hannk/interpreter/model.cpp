#include "interpreter/model.h"
#include "interpreter/ops.h"
#include "util/error_util.h"

#include <cmath>
#include <list>

#if HANNK_PROFILER

#define WEAK __attribute__((weak))

// Weak symbol functions for Op profiling.
extern "C" WEAK void HannkOpInvokeStart() {
}

extern "C" WEAK void HannkOpInvokeEnd(const char *name, const int node_idx) {
}

#endif

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

bool OpGroup::prepare() {
    for (int i = 0; i < op_count(); i++) {
        if (!op(i)->prepare()) {
            return false;
        }
    }
    return true;
}

void OpGroup::execute() {
    for (int i = 0; i < op_count(); i++) {
#if HANNK_PROFILER
        HannkOpInvokeStart();
#endif
        op(i)->execute();
#if HANNK_PROFILER
        HannkOpInvokeEnd(op(i)->name().c_str(), i);
#endif
    }
}

BoundsMap OpGroup::map_bounds(int input_idx, int output_idx) const {
    BoundsMap result(input(input_idx)->rank(), output(output_idx)->rank());
    // TODO
    return result;
}

void OpGroup::dump(std::ostream &os, int indent) const {
    Op::dump(os, indent);
    for (const auto &i : ops_) {
        i->dump(os, indent + 4);
    }
    os << "\n";
}

}  // namespace hannk
