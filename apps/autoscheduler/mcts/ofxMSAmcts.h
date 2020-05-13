/*
A very simple C++11 Templated MCTS (Monte Carlo Tree Search) implementation with examples for openFrameworks.

MCTS Code Based on the Java (Simon Lucas - University of Essex) and Python (Peter Cowling, Ed Powley, Daniel Whitehouse - University of York) impelementations at http://mcts.ai/code/index.html
*/

#pragma once

#include "TreeNodeT.h"
#include "MSALoopTimer.h"
#include <cfloat>
#include <assert.h>

namespace msa {
    namespace mcts {

		// State must comply with State Interface (see IState.h)
		// Action can be anything (which your State class knows how to handle)
        template <class State, typename Action>
        class UCT {
            typedef TreeNodeT<State, Action> TreeNode;

        private:
            LoopTimer timer;

        public:
            float uct_k;					// k value in UCT function. default = sqrt(2)
            unsigned max_iterations;	// do a maximum of this many iterations (0 to run till end)
            unsigned max_millis;		// run for a maximum of this many milliseconds (0 to run till end)
           ///* unsigned*/ int simulation_depth;	// how many ticks (frames) to run simulation for
            double father_value;
            bool use_father_value;
            //--------------------------------------------------------------
            UCT() :
                uct_k( sqrt(2) ),
                max_iterations( 100 ),
                max_millis( 0 ),
                father_value(0),// zero is optimal
                use_father_value(false)
                //simulation_depth( 10 )
            {}


            //--------------------------------------------------------------
            const LoopTimer & get_timer() const {
                return timer;
            }

            //--------------------------------------------------------------
            // get best (immediate) child for given TreeNode based on uct score
            TreeNode* get_best_uct_child(TreeNode* node, float uct_k) const {
                // sanity check
                if(!node->is_fully_expanded()) return NULL;

                float best_uct_score = -std::numeric_limits<float>::max();
                TreeNode* best_node = NULL;

                // iterate all immediate children and find best UTC score
                int num_children = node->get_num_children();
                //int best = 0;
                
                /*if (rand() % 20==0){
                    //with 1/10 chance pick randomly 
                    best_node = node->get_child(rand()%num_children);
                }
                else {*/
                    for(int i = 0; i < num_children; i++) {
                        TreeNode* child = node->get_child(i);
                        float uct_exploitation;
                        if(use_father_value)
                            uct_exploitation = (float)child->get_num_wins() / (child->get_num_visits() + FLT_EPSILON);
                        else
                            uct_exploitation = (float)child->get_average_value() / (child->get_num_visits() + FLT_EPSILON);
                        float uct_exploration = sqrt( log((float)node->get_num_visits() + 1) / (child->get_num_visits() + FLT_EPSILON) );
                        float uct_score = uct_exploitation + uct_k * uct_exploration;

                        if(uct_score > best_uct_score) {
                            best_uct_score = uct_score;
                            best_node = child;
                  //          best = i;
                        }
                    }
                   //std::cout << "uct_score of child: "<< best << " is "<<best_uct_score  << std::endl;
                //}
                //std::cout << "picking "<<best<<"/"<<num_children << std::endl; 
                return best_node;
            }

            TreeNode* get_best_value_child(TreeNode* node) const {
                TreeNode* best_node =node->get_child(0);
                // iterate all immediate children and find best
                int num_children = node->get_num_children();
                for(int i = 1; i < num_children; i++) {
                    TreeNode* child = node->get_child(i);
                    if(child->get_best_value()>best_node->get_best_value()) {
                        best_node = child;
                    }
                    //std::cout << "child "<<i<<" best "<<child->get_best_value() << " average " << child->get_average_value()/(child->get_num_visits()+FLT_EPSILON) <<"num_wins " << child->get_num_wins()<<" /"<<child->get_num_visits()<<std::endl;
                }
                /*if (best_node->get_action() == ){
                    std::cout << "null most visited "<<num_children << std::endl;
                }*/
                return best_node;
            }

            //--------------------------------------------------------------
            TreeNode* get_most_visited_child(TreeNode* node) const {
                int most_visits = -1;
                TreeNode* best_node = NULL;

                // iterate all immediate children and find most visited
                int num_children = node->get_num_children();
                for(int i = 0; i < num_children; i++) {
                    TreeNode* child = node->get_child(i);
                    if(child->get_num_visits() > most_visits) {
                        most_visits = child->get_num_visits();
                        best_node = child;
                    }
                }
                /*if (best_node->get_action() == ){
                    std::cout << "NULL most visited "<<num_children << std::endl;
                }*/
                return best_node;
            }



            //--------------------------------------------------------------
            Action run(const State& current_state, bool& valid, unsigned int seed = 1, std::vector<State>* explored_states = nullptr) {
                std::vector<Action> root_actions;

                //checking if terminal or has singule child
                current_state.get_actions(root_actions);
                // is terminal
                if(root_actions.size()== 0) return Action(-1);
                // has one child
                else if(root_actions.size() == 1) {
                    valid = true;
                    return root_actions[0];
                    }
                else {
                    valid = true;
                    // initialize timer
                    timer.init();

                    // initialize root TreeNode with current state
                    TreeNode root_node(current_state);

                    TreeNode* best_node = NULL;
                    for(unsigned iterations=0;
                        (max_iterations == 0 || iterations < max_iterations) &&
                        (max_millis == 0 || iterations == 0 || !timer.check_duration(max_millis)) //in the first iteration check_duration is garbage
                                               ; iterations++) {
                        //int depth = 0;
                        // indicate start of loop
                        timer.loop_start();
                        //int current_depth = 0;

                        // 1. SELECT. Start at root, dig down into tree using UCT on all fully expanded nodes
                        TreeNode* node = &root_node;
                        while(node->is_fully_expanded()) {
                            //int num_childrens = node->get_num_children();
                            //std::cout << "num_childres:  " << num_childrens <<std::endl;
                            node = get_best_uct_child(node, uct_k);
//                            depth++;
                            //std::cout<<node->get_state().inner.get() << std::endl;
                            //current_depth++;
    //						assert(node);	// sanity check
                        }
                        //std::cout << "operating at current_depth: " << current_depth <<std::endl;
                        // 2. EXPAND by adding a single child (if not terminal or not fully expanded)
                        if(!node->is_fully_expanded() && !node->is_terminal()) node = node->expand();

                        State state(node->get_state());

                        double bestReward = 0;

                        // 3. SIMULATE
  //                      std::vector<Action> backup_actions; 
  //                      backup_actions.clear(); 
                        while(true) {
                            bool finished = state.apply_best_action(bestReward);
  //                          depth++;
                            if (finished) break;
                        }
//                        state.apply_best_greedily(bestReward,backup_actions);
                        // add to history
                        if(explored_states) explored_states->push_back(state);

                        // 4. BACK PROPAGATION
                        while(node) {
                            node->update(-1.0 * bestReward,state,father_value);
                            node = node->get_parent();
                        }

                        // find most visited child
                        best_node = get_best_value_child(&root_node);

                        // indicate end of loop for timer
                        timer.loop_end();
                    }

                    // return best node's action
                    if(best_node) return best_node->get_action();
                    

//                    else return NULL;

                    // we shouldn't be here
                    assert(0 && "Error: could not find any action");
                    exit(1);

                }

            }


        };
    }
}
