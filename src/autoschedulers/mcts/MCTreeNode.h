#ifndef HL_MC_TREE_NODE_H
#define HL_MC_TREE_NODE_H

// TODO(rootjalex): figure out includes.
//                  should this be in the Halide namespace??
#include <_types/_uint32_t.h>
#include <algorithm>    // std::sort
#include <cstdint>      // uint32_t
#include <iostream>     // std::cerr
#include <memory>       // std::shared_ptr
#include <random>       // std::mt19937
#include <utility>
#include <vector>

/*

TODO(rootjalex): add more details

*/


namespace MCTS {

    size_t state_count;

    template<class Action>
    struct CostsLess {
        bool operator()(const Action &a, const Action &b) const {
            return a.get_cost() < b.get_cost();
        }
    };

    uint32_t get_dropout_threshold();
    bool random_dropout(std::mt19937 &rng, size_t num_decisions);

    // TODO(rootjalex): refactor this as needed.
    template<class State, class Action>
    struct TreeNode {
        typedef std::shared_ptr<TreeNode<State, Action> > SharedPtr;
        typedef TreeNode<State, Action>* BarePtr;

        // State of this TreeNode.
        State state;
        // Action that led to this TreeNode.
        const Action action;
        // parent of this node. null if root. BarePtr to avoid loops of smart pointers.
        BarePtr parent;
        const uint32_t depth;

        // Data for this node (to be updated)
        uint32_t num_visits;

        // State stores value.

        // Children (only includes those generated via actions).
        std::vector<SharedPtr> children;
        // Possible actions to take from this state.
        std::vector<Action> possible_actions;

        std::mt19937 &rng;

        TreeNode(const State &_state, const Action &_action, BarePtr _parent, std::mt19937 &_rng) :
            state(_state), action(_action), parent(_parent),
            depth(_parent ? _parent->depth + 1 : 0), num_visits(0), rng(_rng) {
            
            // The state should be capable of generating it's own actions.
            possible_actions = state.generate_possible_actions();
            // std::cerr << "Generated: " << possible_actions.size() << " at depth " << depth << std::endl;
            sort_actions();
            state_count = possible_actions.size();
        }

        TreeNode(State &&_state, const Action &_action, BarePtr _parent, std::mt19937 &_rng) :
            state(_state), action(_action), parent(_parent),
            depth(_parent ? _parent->depth + 1 : 0), num_visits(0), rng(_rng) {
            // The state should be capable of generating it's own actions.
            possible_actions = state.generate_possible_actions();
            sort_actions();
            // std::cerr << "Generated: " << possible_actions.size() << " at depth " << depth << std::endl;
            state_count += possible_actions.size();
        }

        void sort_actions() {
            // TODO: is there any faster way?
            static CostsLess<Action> costsLess;
            // std::cerr << "here(0)" << std::endl;
            std::for_each(possible_actions.begin(), possible_actions.end(), [this](const Action &_action) {
                _action.cache_cost(this->state);
            });
            // std::cerr << "here(1)" << std::endl;
            state.model_ptr->evaluate_costs();
            // std::cerr << "here(2)" << std::endl;
            // TODO: make costs evaluate in batches.
            // std::cerr << "here(3)" << std::endl;
            // std::cerr << "sorting for:" << possible_actions.size() << std::endl;
            std::sort(possible_actions.begin(), possible_actions.end(), costsLess);
            // std::cerr << "here(4)" << std::endl;
        }
        
        SharedPtr add_child_with_action(const Action &child_action) {
            // This should throw an error if action is not valid.
            State new_state = state.take_action(child_action);
            // std::cerr << "\tGenerated new state" << std::endl;

            BarePtr child_node = new TreeNode<State, Action>(std::move(new_state), child_action, this, rng);
            // std::cerr << "\tCreated child node:" << child_node << std::endl;

            // TODO(rootjalex): deviates from sample code.
            SharedPtr child_sptr = SharedPtr(child_node);

            // Add to children.
            children.push_back(child_sptr);

            // std::cerr << "\tAdded " << child_sptr << "to children, now have size: " << children.size() << std::endl;

            return child_sptr;
        }

        SharedPtr evaluate_action(Action &_action) {
            if (_action.explored) {
                // Find the child that this corresponds to.
                return children[_action.index];
            } else {
                _action.explored = true;
                _action.index = children.size();
                return add_child_with_action(_action);
            }
        }

        SharedPtr choose_any_random_child() {
            // TODO(rootjalex): this might need to be specialized.
            if (possible_actions.empty()) {
                std::cerr << "No possible actions for choose_any_random_child" << std::endl;
                assert(false); // TODO(rootjalex): better assert
                return nullptr;
            }

            uint32_t random_index = rng() % possible_actions.size();

            Action &random_action = possible_actions[random_index];

            return evaluate_action(random_action);
        }

        SharedPtr choose_weighted_random_child() {
            // TODO(rootjalex): this might need to be specialized.
            if (possible_actions.empty()) {
                std::cerr << "No possible actions for choose_any_random_child" << std::endl;
                assert(false); // TODO(rootjalex): better assert
                return nullptr;
            }

            const size_t n_actions = possible_actions.size();

            if (n_actions == 1) {
                Action &only_action = possible_actions[0];
                return evaluate_action(only_action);
            } else {
                // Evaluate everything except for the last, then return that as a default.
                for (size_t ind = 0; ind < (n_actions - 1); ind++) {
                    // TODO(rootjalex): better probability sampling or something...
                    // Talk to Andrew about this.
                    if (!random_dropout(rng, n_actions)) {
                        return evaluate_action(possible_actions[ind]);
                    }
                }
                // Didn't return in the for loop above, so return the last action.
                return evaluate_action(possible_actions[(n_actions - 1)]);
            }
        }

        SharedPtr choose_new_random_child() {
            // TODO(rootjalex): this might need to be specialized.
            if (possible_actions.empty()) {
                std::cerr << "No possible actions for choose_new_random_child" << std::endl;
                assert(false); // TODO(rootjalex): better assert
                return nullptr;
            }
            const uint32_t n_possibles = possible_actions.size();
            const uint32_t n_children = children.size();
            // std::cerr << "\tPossible:" << n_possibles << std::endl;
            // std::cerr << "\tChildren:" << n_children << std::endl;
            if (n_possibles == n_children) {
                // This is bad, can't expand.
                std::cerr << "choose_new_random_child has no options.\n";
                assert(false);
                return nullptr;
            }
            // TODO(rootjalex): there's gotta be a more efficient way of doing this.
            std::vector<Action *> untaken_actions;

            for (uint32_t index = 0; index < n_possibles; index++) {
                Action *possible_action = &possible_actions[index];
                if (!possible_action->explored) {
                    // std::cerr << "\tAdding new possibility: " << possible_action << std::endl;
                    untaken_actions.push_back(possible_action);
                }
            }

            // std::cerr << "\tChoosing with possibilities: " << untaken_actions.size() << std::endl;

            // TODO(rootjalex): what do we do if no possible actions?

            uint32_t random_index = rng() % untaken_actions.size();

            // std::cerr << "\tChose random index: " << random_index << std::endl;

            Action *random_action = untaken_actions[random_index];

            // std::cerr << "\tChose random action: " << random_action << std::endl;

            // TODO(rootjalex): this should work, I think?
            random_action->explored = true;
            random_action->index = n_children;
            // random_action->dump();

            // std::cerr << "\tAdding random child" << std::endl;

            auto chosen_action = *random_action;

            // std::cerr << "\tAccessed action" << std::endl;

            return add_child_with_action(chosen_action);
        }

        // This child should not have been explored before.
        SharedPtr choose_specific_child(uint32_t index) {
            assert(index < possible_actions.size());
            return evaluate_action(possible_actions[index]);
        }

        SharedPtr choose_only_random_child() {
            assert(possible_actions.size() == 1);
            return evaluate_action(possible_actions[0]);
        }

        // (potentially) update the state's value.
        bool update(const double cost_value) {
            return state.update(cost_value);
        }

        bool update(const double cost_value, const uint32_t _depth) {
            return state.update(cost_value, _depth);
        }

        // Refer to the state for an exploration value.
        // This might be an average or a minimum, depending
        // on the policy being executed.
        double get_exploitation_value() {
            // Pass in the number of visits to the state,
            // in case it needs to calculate an average.
            return state.get_exploitation_value(num_visits);
        }

        SharedPtr get_child(int i) const {
            return children[i];
        }

        const State &get_state() const {
            return state;
        }

        const Action &get_action() const {
            return action;
        }

        double get_value() const {
            return state.get_value();
        }

        uint32_t get_state_depth() const {
            return state.get_stored_depth();
        }

        uint32_t get_depth() const {
            return depth;
        }

        uint32_t get_num_visits() const {
            return num_visits;
        }

        bool is_leaf() const {
            // TODO: um, this seems very bad.
            return possible_actions.empty();
        }

        bool is_terminal() const {
            return state.is_terminal();
        }

        bool is_fully_expanded() const {
            return children.size() == possible_actions.size();
        }

        int get_num_children() const {
            return children.size();
        }

        BarePtr get_parent() const {
            return parent;
        }

        bool is_valid() const {
            return state.is_valid();
        }

        void increment_visits() {
            num_visits++;
            if (parent) {
                parent->increment_visits();
            }
        }

        void clear_parent() {
            parent = nullptr;
        }

        size_t get_n_branches() const {
            return possible_actions.size();
        }

        std::pair<uint32_t, double> get_min_available() {
            if (is_terminal()) {
                return {depth, action.get_cost()};
            } else {
                const uint32_t stored_depth = state.get_stored_depth();
                const double stored_cost = state.get_value();

                internal_assert(!possible_actions.empty()) << "get_min_available had no children but is not terminal.\n";

                const uint32_t next_depth = depth + 1;
                const uint32_t next_cost = possible_actions[0].get_cost();

                if (stored_depth == 0 || next_depth > stored_depth) {
                    // No stored depth yet or next_depth is deeper.
                    return {next_depth, next_cost};
                } else if (next_depth == stored_depth) {
                    return {next_depth, std::min(stored_depth, next_depth)};
                } else {
                    return {stored_depth, stored_cost};
                }
            }
        }
    };

} // namespace MCTS

#endif // HL_MC_TREE_NODE_H
