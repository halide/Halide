#ifndef HL_MC_TREE_NODE_H
#define HL_MC_TREE_NODE_H

// TODO(rootjalex): figure out includes.
//                  should this be in the Halide namespace??
#include <cstdint>      // uint32_t
#include <iostream>     // std::cerr
#include <memory>       // std::shared_ptr
#include <random>       // std::mt19937
#include <vector>

/*

TODO(rootjalex): add more details

*/


namespace MCTS {

    // TODO(rootjalex): refactor this as needed.
    template<class State, class Action>
    class TreeNode {
        typedef std::shared_ptr<TreeNode<State, Action> > SharedPtr;
        typedef TreeNode<State, Action>* BarePtr;

        // State of this TreeNode.
        const State state;
        // Action that led to this TreeNode.
        const Action action;
        // parent of this node. null if root. BarePtr to avoid loops of smart pointers.
        const TreeNode<State, Action>* parent;
        const uint32_t depth;

        // Data for this node (to be updated)
        uint32_t num_visits;

        // State stores value.

        // Children (only includes those generated via actions).
        std::vector<SharedPtr> children;
        // Possible actions to take from this state.
        std::vector<Action> possible_actions;

        std::mt19937 &rng;

    public:
        TreeNode(const State &_state, const Action &_action, BarePtr _parent, std::mt19937 &_rng) :
            state(_state), action(_action), parent(_parent), rng(_rng),
            num_visits(0), depth(_parent ? _parent->depth + 1: 0) {
            
            // The state should be capable of generating it's own actions.
            possible_actions = state.generate_possible_actions();
        }

        TreeNode(State &&_state, const Action &_action, BarePtr _parent, std::mt19937 &_rng) :
            state(_state), action(_action), parent(_parent), rng(_rng),
            num_visits(0), depth(_parent ? _parent->depth + 1: 0) {
            
            // The state should be capable of generating it's own actions.
            possible_actions = state.generate_possible_actions();
        }
        
        SharedPtr add_child_with_action(const Action &child_action) {
            // This should throw an error if action is not valid.
            State new_state = state.take_action(child_action);

            BarePtr child_node = new TreeNode<State, Action>(std::move(new_state), child_action, this, rng);

            // TODO(rootjalex): deviates from sample code.
            SharedPtr child_sptr = SharedPtr(child_node);

            // Add to children.
            children.push_back(child_sptr);

            return child_sptr;
        }

        SharedPtr choose_new_random_child() {
            // TODO(rootjalex): this might need to be specialized.
            if (possible_actions.empty()) {
                assert(false); // TODO(rootjalex): better assert
                return nullptr;
            }
            const uint32_t n_possibles = possible_actions.size();
            const uint32_t n_children = children.size();
            if (n_possibles == n_children) {
                // This is bad, can't expand.
                std::cerr << "choose_new_random_child has no options.\n";
                assert(false);
                return nullptr;
            }
            // TODO(rootjalex): there's gotta be a more efficient way of doing this.
            std::vector<Action *> untaken_actions;

            for (size_t index = 0; index <= possible_actions.size(); index++) {
                Action *possible_action = &possible_actions[index];
                if (!possible_action->explored) {
                    untaken_actions.push_back(possible_action);
                }
            }

            uint32_t random_index = rng() & untaken_actions.size();

            Action *random_action = untaken_actions[random_index];

            // TODO(rootjalex): this should work, I think?
            random_action->explored = true;

            return add_child_with_action(*random_action);
        }

        // (potentially) update the state's value.
        bool update(double &cost_value) {
            return state.update(cost_value);
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

        uint32_t get_depth() const {
            return depth;
        }

        uint32_t get_num_visits() const {
            return num_visits;
        }

        bool is_terminal() const {
            return possible_actions.empty();
        }

        bool is_fully_expanded() const {
            return children.size() == possible_actions.size();
        }


    };

} // namespace MCTS

#endif // HL_MC_TREE_NODE_H
