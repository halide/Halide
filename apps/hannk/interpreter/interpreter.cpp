#include "interpreter/interpreter.h"
#include "interpreter/allocation_planner.h"
#include "interpreter/transforms.h"
#include "util/error_util.h"

#include "HalideRuntime.h"

#include <map>
#include <set>

// TODO: apparently not part of the public Halide API. Should it be?
extern "C" int halide_malloc_alignment();

namespace hannk {

Interpreter::Interpreter(std::unique_ptr<OpGroup> m, InterpreterOptions options)
    : model_(std::move(m)) {
    init(options);
}

Interpreter::~Interpreter() {
}

namespace {

// TODO: maybe move this to a separate file? Not sure if it's complex enough to be worthy or not.
class TensorVisitor : public OpVisitor {
    virtual void visit_tensor(const TensorPtr &t) = 0;

    void visit(OpGroup *g) override {
        for (int i = 0; i < g->op_count(); i++) {
            op_index_++;
            Op *op = g->op(i);
            for (int j = 0; j < op->input_count(); j++) {
                visit_tensor(op->input(j));
            }
            for (int j = 0; j < op->output_count(); j++) {
                visit_tensor(op->output(j));
            }
            op->accept(this);
        }
    }

    int op_index_ = -1;

public:
    int op_index() const {
        return op_index_;
    }
};

struct TensorAllocationInfo {
    size_t size_needed = 0;
    int first_use = std::numeric_limits<int>::max();
    int last_use = std::numeric_limits<int>::min();
    int block_index = -1;
    std::set<TensorPtr> tensors;
};

bool needs_arena_allocation(const TensorPtr &t) {
    if (!t || t->is_external() || t->is_dynamic() || t->is_allocated()) {
        return false;
    }
    return true;
}

class FindAllocatableTensors : public TensorVisitor {
    void visit_tensor(const TensorPtr &t) {
        if (!needs_arena_allocation(t)) {
            return;
        }
        TensorStoragePtr storage = t->storage();
        assert(storage != nullptr);

        auto &info = tensor_info[storage];
        assert(info.size_needed == 0 || info.size_needed == storage->storage_size());

        info.size_needed = storage->storage_size();
        info.first_use = std::min(info.first_use, op_index());
        info.last_use = std::max(info.last_use, op_index());
        // leave block_index as -1
        info.tensors.insert(t);
    }

public:
    // Iteration order matters, so don't use unordered_map without consideration.
    std::map<TensorStoragePtr, TensorAllocationInfo> tensor_info;
};

std::unique_ptr<char[]> allocate_tensors(OpGroup *root, const InterpreterOptions &options) {
    // Find the tensors that we want to allocate in an arena,
    // along the needed storage size and lifetime for each.
    FindAllocatableTensors find_tensors;
    root->accept(&find_tensors);

    if (options.verbosity >= 1) {
        for (const auto &it : find_tensors.tensor_info) {
            const auto &info = it.second;
            HLOG(INFO) << "TensorStorage of size " << info.size_needed << " life [" << info.first_use << " ... " << info.last_use << "]";
            for (const auto &t : info.tensors) {
                HLOG(INFO) << "  Tensor: " << t->name() << " size " << t->buffer().size_in_bytes();
            }
        }
    }

    // Feed this info to the allocation planner.
    // Let's assume that whatever alignment halide_malloc() needs is necessary here, too.
    // (Note that TFLite will complain if alignment is less than 64...)
    constexpr int kTfLiteDefaultTensorAlignment = 64;
    const size_t alignment = (size_t)std::max(halide_malloc_alignment(), kTfLiteDefaultTensorAlignment);
    AllocationPlanner planner(alignment);
    for (auto &it : find_tensors.tensor_info) {
        auto &info = it.second;
        info.block_index = planner.add_block(info.size_needed, info.first_use, info.last_use);
        assert(info.block_index >= 0);
    }
    planner.commit();

    if (options.verbosity >= 1) {
        std::ostringstream oss;
        oss << "Arena memory needed: " << planner.memory_needed() << '\n';
        oss << "    Offsets:";
        for (int i = 0; i < planner.block_count(); i++) {
            oss << ' ' << planner.get_block_offset(i);
        }
        if (options.verbosity >= 2) {
            oss << "\nUsage Map:\n";
            planner.dump(oss);
        }
        HLOG(INFO) << oss.str();
    }

    // Allocate the chunk we need. Be sure to over-allocate for alignment.
    std::unique_ptr<char[]> arena(new char[planner.memory_needed() + alignment]);
    assert(arena != nullptr);

    // Point all the tensors at the correct offsets.
    char *arena_base = arena.get();

    // Make sure that the 'base' we start from is aligned.
    arena_base = (char *)(((uintptr_t)arena_base + alignment - 1) & ~(alignment - 1));

    for (const auto &it : find_tensors.tensor_info) {
        const auto &info = it.second;
        char *new_host = arena_base + planner.get_block_offset(info.block_index);
        for (const auto &t : info.tensors) {
            t->allocate_from_arena_pointer(new_host);
        }
    }

    return arena;
}

class VerifyAllAllocated : public TensorVisitor {
    void visit_tensor(const TensorPtr &t) override {
        if (!needs_arena_allocation(t)) {
            return;
        }
        assert(t->is_allocated());
    }
};

}  // namespace

void Interpreter::init(InterpreterOptions options) {
    pad_for_ops(model_.get());
    in_place(model_.get());
    fold_constants(model_.get());
    remove_dead_ops(model_.get());

    assert(tensor_storage_arena_ == nullptr);
    tensor_storage_arena_ = allocate_tensors(model_.get(), options);

#ifndef NDEBUG
    VerifyAllAllocated verify_all;
    model_->accept(&verify_all);
#endif
}

void Interpreter::execute() {
    model_->execute();
}

TensorPtr Interpreter::get_tensor(const std::string &name) {
    for (int i = 0; i < model_->op_count(); i++) {
        Op *op = model_->op(i);
        for (int j = 0; j < op->input_count(); j++) {
            if (op->input(j)->name() == name) {
                return op->input(j);
            }
        }
        for (int j = 0; j < op->output_count(); j++) {
            if (op->output(j)->name() == name) {
                return op->output(j);
            }
        }
    }
    return nullptr;
}

std::vector<TensorPtr> Interpreter::inputs() {
    std::vector<TensorPtr> result;
    for (int i = 0; i < model_->input_count(); i++) {
        result.push_back(model_->input(i));
    }

    return result;
}

std::vector<TensorPtr> Interpreter::outputs() {
    std::vector<TensorPtr> result;
    for (int i = 0; i < model_->output_count(); i++) {
        result.push_back(model_->output(i));
    }

    return result;
}

}  // namespace hannk
