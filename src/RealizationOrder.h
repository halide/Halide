#ifndef HALIDE_INTERNAL_REALIZATION_ORDER_H
#define HALIDE_INTERNAL_REALIZATION_ORDER_H

/** \file
 *
 * Defines the lowering pass that determines the order in which
 * realizations are injected and groups functions with fused
 * computation loops.
 */

#include <map>
#include <string>
#include <vector>

namespace Halide {
namespace Internal {

class Function;
struct FusedStageContents;
struct FusedGroupContents;
struct PipelineGraphContents;

class FusedStage {
    IntrusivePtr<FusedStageContents> contents;

public:
    FusedStage();

    FusedStage(const FusedStage &other);

    FusedStage(FusedStage &&other) noexcept;

    explicit FusedStage(IntrusivePtr <FusedStageContents> ptr) : contents(std::move(ptr)) {}

    friend struct std::hash<Halide::Internal::FusedStage>;

    bool operator==(const FusedStage &other) const;
};

class FusedGroup {
    IntrusivePtr<FusedGroupContents> contents;

public:
    FusedGroup();

    FusedGroup(const FusedGroup &other) = default;

    FusedGroup(FusedGroup &&other) noexcept = default;

    explicit FusedGroup(IntrusivePtr <FusedGroupContents> ptr) : contents(std::move(ptr)) {}

    friend struct std::hash<Halide::Internal::FusedGroup>;

    FusedGroup &operator=(const FusedGroup &other) = default;
    bool operator==(const FusedGroup &other) const;

    void add_stage(const FusedStage &stage);

    void add_function(const Function &function);

    const std::vector<Function> &functions() const;

    std::string repr() const;
};

class PipelineGraph {
    IntrusivePtr <PipelineGraphContents> contents;

public:
    PipelineGraph();

    PipelineGraph(const PipelineGraph &other) = default;

    PipelineGraph(PipelineGraph &&other) noexcept = default;

    PipelineGraph(IntrusivePtr <PipelineGraphContents> ptr) : contents(std::move(ptr)) {}

    bool operator==(const PipelineGraph &other);

    void add_fused_group(const FusedGroup &group);

    std::vector<FusedGroup> get_fused_groups() const;

    void add_edge(const FusedGroup &src, const FusedGroup &dst);
};

/** Given a bunch of functions that call each other, determine an
 * order in which to do the scheduling. This in turn influences the
 * order in which stages are computed when there's no strict
 * dependency between them. Currently just some arbitrary depth-first
 * traversal of the call graph. In addition, determine grouping of functions
 * with fused computation loops. The functions within the fused groups
 * are sorted based on realization order. There should not be any dependencies
 * among functions within a fused group. This pass will also populate the
 * 'fused_pairs' list in the function's schedule. Return a pair of
 * the realization order and the fused groups in that order.
 */
std::pair<std::vector<std::string>, std::vector<std::vector<std::string>>> realization_order(
    const std::vector<Function> &outputs, std::map<std::string, Function> &env);

/** Given a bunch of functions that call each other, determine a
 * topological order which stays constant regardless of the schedule.
 * This ordering adheres to the producer-consumer dependencies, i.e. producer
 * will come before its consumers in that order */
std::vector<std::string> topological_order(
        const std::vector<Function> &outputs, const std::map<std::string, Function> &env);

}  // namespace Internal
}  // namespace Halide

namespace std {
template <> struct hash<Halide::Internal::FusedStage>;
template <> struct hash<Halide::Internal::FusedGroup>;
}

#endif
