#include "interpreter/allocation_planner.h"

#include <algorithm>
#include <cassert>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <list>

#ifndef HANNK_USE_TRIVIAL_ALLOCATION_PLANNER
#define HANNK_USE_TRIVIAL_ALLOCATION_PLANNER 0
#endif

namespace hannk {

namespace {

size_t align_up(size_t p, size_t alignment) {
    return (p + alignment - 1) & ~(alignment - 1);
}

constexpr size_t kInvalidOffset = std::numeric_limits<size_t>::max();

}  // namespace

AllocationPlanner::AllocationPlanner(size_t alignment)
    : alignment_(alignment) {
}

int AllocationPlanner::add_block(size_t size, int first_use, int last_use) {
    assert(!committed_);
    int block_id = (int)block_requirements_.size();
    block_requirements_.push_back({kInvalidOffset, size, first_use, last_use});
    return block_id;
}

int AllocationPlanner::block_count() const {
    return (int)block_requirements_.size();
}

void AllocationPlanner::commit() {
    assert(!committed_);
    committed_ = true;

    // This happens in some unusual cases
    if (block_requirements_.empty()) {
        return;
    }

#if HANNK_USE_TRIVIAL_ALLOCATION_PLANNER

    // Use a simpleminded implementation that never overlaps anything.
    // This is useful mainly for debugging purposes.
    size_t next_offset = 0;
    for (auto &r : block_requirements_) {
        r.calculated_offset = next_offset;
        next_offset += align_up(r.size_needed, alignment_);
    }

#else

    // Use a basic greedy algorithm to lay out the buffers;
    // the basic idea here is to start with the largest block,
    // then progress into smaller blocks, picking out the first large-enough
    // gap we find that has no overlap in the time domain. If there is
    // no such gap, add the block to the end. This isn't perfect, of course,
    // but pretty good in practice. (Algorithm inspired by TFMicro's greedy allocator.)

    // Make a list of all the block requirements, sorted by descending size.
    // (This list will just point to the entries in the block_requirements_ list.)
    std::vector<BlockRequirements *> block_requirements_sorted;
    block_requirements_sorted.reserve(block_requirements_.size());
    for (auto &r : block_requirements_) {
        block_requirements_sorted.push_back(&r);
    }

    std::sort(block_requirements_sorted.begin(), block_requirements_sorted.end(),
              [](BlockRequirements *a, BlockRequirements *b) -> bool {
                  // Sort in decreasing (well, really non-increasing) order by size.
                  if (a->size_needed != b->size_needed) {
                      return a->size_needed > b->size_needed;
                  }

                  // If sizes are equal, sort by increasing time of first use.
                  return a->first_use < b->first_use;
              });

    // This is a list that we keep sorted (by offset) as we go along.
    // It just points to entries in block_requirements_sorted, so it's crucial
    // that block_requirements_sorted be kept in the same order after this.
    std::list<BlockRequirements *> offsets;

    using OffsetsIterator = std::list<BlockRequirements *>::iterator;

    // Put the first (largest) block at offset 0.
    block_requirements_sorted[0]->calculated_offset = 0;
    offsets.push_back(block_requirements_sorted[0]);

    // Process the rest in descending order, trying to find a gap that fits.
    for (size_t i = 1; i < block_requirements_sorted.size(); ++i) {
        BlockRequirements *req = block_requirements_sorted[i];
        size_t candidate_offset = 0;

        OffsetsIterator prior = offsets.end();
        for (;;) {
            // Find the first block after 'prior' that's active at the same time as 'req'.
            OffsetsIterator next = (prior != offsets.end()) ? std::next(prior) : offsets.begin();
            for (; next != offsets.end(); ++next) {
                const bool has_time_overlap = !((*next)->first_use > req->last_use || req->first_use > (*next)->last_use);
                if (has_time_overlap) {
                    // Can't safely insert between prior and next -- advance and try again until
                    // we find a block we have no time overlap with (or run out of blocks).
                    break;
                }
            }

            // If there's a prior block, the candidate_offset begins just past prior's end.
            if (prior != offsets.end()) {
                const size_t prior_end_offset = (*prior)->calculated_offset + (*prior)->size_needed;
                if (prior_end_offset > candidate_offset) {
                    candidate_offset = align_up(prior_end_offset, alignment_);
                }
            }

            if (next == offsets.end()) {
                // There is no next block, so we're just going to append after the last one.
                break;
            }

            // There is a next block -- let's see if there's a gap between prior and next,
            // and if so, if it's large enough to use here.
            if ((*next)->calculated_offset >= candidate_offset) {
                const size_t gap = (*next)->calculated_offset - candidate_offset;
                if (gap >= req->size_needed) {
                    // Note that we take a first-fit approach here, rather than a best-fit.
                    // (Experimentation on our standard suite of models showed literally
                    // *no* size difference in arena size needed for a best-fit algorithm,
                    // and no meaningful performance difference.)
                    break;
                }
            }

            // Not enough space, keep trying
            prior = next;
        }

        // OK, so we've found an offset to use (either in an existing gap that's
        // not in use for this block's timeframe, or by implicitly extending
        // the memory arena size). Save it in the requirements, and then
        // update the offsets list so that it remains in order.
        assert(req->calculated_offset == kInvalidOffset);
        req->calculated_offset = candidate_offset;

        bool inserted = false;
        for (OffsetsIterator it = offsets.begin(); it != offsets.end(); ++it) {
            if (candidate_offset < (*it)->calculated_offset) {
                offsets.insert(it, req);
                inserted = true;
                break;
            }
        }
        if (!inserted) {
            assert(candidate_offset >= offsets.back()->calculated_offset);
            offsets.push_back(req);
        }
    }
#endif  // HANNK_USE_TRIVIAL_ALLOCATION_PLANNER

#ifndef NDEBUG
    check_overlap();
#endif
}

size_t AllocationPlanner::memory_needed() const {
    assert(committed_);
    size_t needed = 0;
    for (const auto &br : block_requirements_) {
        assert(br.calculated_offset != kInvalidOffset);
        needed = std::max(needed, br.calculated_offset + br.size_needed);
    }
    return needed;
}

size_t AllocationPlanner::get_block_offset(int block_id) const {
    assert(committed_);
    assert(block_id >= 0 && block_id < (int)block_requirements_.size());
    const auto &br = block_requirements_.at(block_id);
    assert(br.calculated_offset != kInvalidOffset);
    return br.calculated_offset;
}

void AllocationPlanner::dump(std::ostream &o) {
    assert(committed_);

    // Implementation based on similar code from TFMicro's greedy allocator.

    const auto char_for = [](int block_id) -> char {
        if (block_id < 10) {
            return '0' + block_id;
        } else if (block_id < 36) {
            return 'a' + (block_id - 10);
        } else if (block_id < 62) {
            return 'A' + (block_id - 36);
        } else {
            return '*';
        }
    };

    o << "\nBlock Info:\n";
    size_t max_size = 0;
    int max_time = 0;
    int block_id = 0;
    for (auto it : block_requirements_) {
        o << "BlockID: " << block_id
          << " Offset: " << it.calculated_offset
          << " Size: " << it.size_needed
          << " FirstUse: " << it.first_use
          << " LastUse: " << it.last_use
          << " MapChar: " << char_for(block_id)
          << "\n";
        max_size = std::max(max_size, it.calculated_offset + it.size_needed);
        max_time = std::max(max_time, it.last_use);
        ++block_id;
    }

    o << "\nUsage Map:\n";
    constexpr int kLineWidth = 80;
    char line[kLineWidth + 1];
    for (int t = 0; t <= max_time; ++t) {
        for (int c = 0; c < kLineWidth; ++c) {
            line[c] = '.';
        }
        for (size_t i = 0; i < block_requirements_.size(); ++i) {
            const auto &br = block_requirements_[i];
            if (t < br.first_use || t > br.last_use) {
                continue;
            }
            assert(br.calculated_offset != kInvalidOffset);
            // Approximate the lifespan along the horizontal axis
            const int line_start = (br.calculated_offset * kLineWidth) / max_size;
            const int line_end = ((br.calculated_offset + br.size_needed) * kLineWidth) / max_size;
            for (int n = line_start; n < line_end; ++n) {
                if (line[n] == '.') {
                    line[n] = char_for(i);
                } else {
                    // The map is imprecise, so we have a collision that is too fine to represent
                    line[n] = '!';
                }
            }
        }
        line[kLineWidth] = 0;
        o << "t=" << std::setfill('0') << std::setw(3) << t << ": " << line << "\n";
    }
}

void AllocationPlanner::check_overlap() {
#ifndef NDEBUG
    assert(committed_);
    bool ok = true;
    for (size_t i = 0; i < block_requirements_.size(); ++i) {
        const auto &a = &block_requirements_[i];
        for (size_t j = 0; j < i; ++j) {
            const auto &b = &block_requirements_[j];
            if (a->first_use > b->last_use || b->first_use > a->last_use) {
                continue;
            }
            const size_t a_start = a->calculated_offset;
            const size_t a_end = a_start + a->size_needed;
            const size_t b_start = b->calculated_offset;
            const size_t b_end = b_start + b->size_needed;
            if (a_start >= b_end || b_start >= a_end) {
                continue;
            }
            std::cerr << "Overlap found!\n"
                      << "  block_id " << i << " time " << a->first_use << ".." << a->last_use << " space " << a_start << ".." << a_end << "\n"
                      << "  block_id " << j << " time " << b->first_use << ".." << b->last_use << " space " << b_start << ".." << b_end << "\n";
            ok = false;
        }
    }
    if (!ok) {
        abort();
    }
#endif
}

}  // namespace hannk
