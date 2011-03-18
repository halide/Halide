#include "Compiler.h"
#include "base.h"

void Compiler::compile(FImage *im) {

    compilePrologue();

    // Compile a chunk of code that just runs the definitions in order
    for (int i = 0; i < (int)im->definitions.size(); i++) {
        compileDefinition(im, i);
    }
    
    compileEpilogue();
}

void Compiler::collectInputs(IRNode::Ptr node, OpCode op, IRNode::PtrSet &nodes) {
    if (node->op == op) nodes.insert(node);
    for (size_t i = 0; i < node->inputs.size(); i++) {
        collectInputs(node->inputs[i], op, nodes);
    }
}

void Compiler::compileDefinition(FImage *im, int definition)
{
    printf("Compiling definition %d/%d.\n",
           definition+1, (int)im->definitions.size());    
    
    IRNode::Ptr def = im->definitions[definition].node;
    
    def->printExp();
    printf("\n");
    
    // It should be a store or storeVector node.
    assert(def->op == Store || def->op == StoreVector, "Definitions of images should be Store nodes\n");
    
    IRNode::Ptr lhs = def->inputs[0];
    IRNode::Ptr rhs = def->inputs[1];
        
    // Find the variables we need to iterate over by digging into the lhs and rhs
    printf("Collecting free variables\n");
    IRNode::PtrSet varSet;
    collectInputs(def, Variable, varSet);
    
    assert(varSet.size() < 256, "FImage can't cope with more than 255 variables\n");
    
    // put them in a vector
    vars = vector<IRNode::Ptr>(varSet.size());
    int i = 0;
    for (IRNode::PtrSet::iterator iter = varSet.begin();
         iter != varSet.end(); iter++) {
        vars[i] = *iter;
        i++;
    }
    
    // Differentiate the store address w.r.t each var to resolve
    // ambiguous loop nestings.
    vector<int64_t> storeDelta(vars.size());
    for (size_t i = 0; i < vars.size(); i++) {
        IRNode::Ptr next = IRNode::make(PlusImm, vars[i], 1);
        IRNode::Ptr nextLhs = lhs->substitute(vars[i], next);
        if (nextLhs == lhs) {
            storeDelta[i] = 0;
        } else {
            IRNode::Ptr delta = IRNode::make(Minus, nextLhs, lhs)->optimize();
            printf("delta = "); delta->printExp(); printf("\n");
            if (delta->op == Const && delta->type == IRNode::Int) {
                storeDelta[i] = delta->ival;
            } else {
                // Unknown store delta
                storeDelta[i] = 0x7fffffffffffffff;
            }
        }
    }
    
    // TODO BUG: this only generates acceptable code (with the load/store addresses not passed as illegal 64-bit immediates) for vars.size() >= 2
    // Stable sort loop levels by the requested loop nesting and store
    // delta and assign loop levels
    for (size_t i = 0; i < vars.size(); i++) {
        for (size_t j = i+1; j < vars.size(); j++) {
            int nj = vars[j]->data<Variable>()->loopNesting;
            int64_t sj = abs(storeDelta[j]);
            int ni = vars[i]->data<Variable>()->loopNesting;        
            int64_t si = abs(storeDelta[i]);
            
            if (ni > nj || (ni == nj && si < sj)) {
                IRNode::Ptr v = vars[i];
                vars[i] = vars[j];
                vars[j] = v;
                int64_t d = storeDelta[i];
                storeDelta[i] = storeDelta[j];
                storeDelta[j] = d;                
            }            
        }
        vars[i]->assignLevel((unsigned char)(i+1));
    }
    
    /*
     Replaced by varData(i) convenience method
    vector<NodeData<Variable> *> varData(vars.size());
    for (size_t i = 0; i < vars.size(); i++) {
        varData[i] = vars[i]->data<Variable>();
    }
    */
    
    // Check all the vars have sane bounds.
    for (size_t i = 0; i < vars.size(); i++) {
        printf("Var %d : [%lld %lld]\n", (int)i,
               vars[i]->interval.min(), vars[i]->interval.max());
        assert(vars[i]->interval.bounded(), "Variable %d has undefined bounds\n");
    }
    
    // Find a var to vectorize across. For right now we just pick the
    // innermost var flagged as vectorizable.
    bool vectorize = false;
    size_t vectorDim = 0;
    vectorWidth = vector<int>(vars.size(), 1);
    for (size_t i = 0; i < vars.size() && !vectorize; i++) {       
        int v = varData(i)->vectorize; 
        if (v > 1) {
            if (v != 4) {
                printf("Warning: Current implementation can only vectorize with a width of 4. Switching to four. Resulting code may crash.\n");                
            }
            vectorize = true;
            vectorDim = i;
            vectorWidth[vectorDim] = 4;
            const SteppedInterval &in = vars[vectorDim]->interval;
            if (((in.max()+1) & 3) || (in.min() & 3)) {
                printf("Warning: Can only vectorize across variables with a min and max that are multiples of four. Resulting code may crash.\n");
            }
            vars[vectorDim]->interval.setCongruence(0, 4);
        }
    }
    
    // Now that bounds and congruences are set, do static analysis
    def->analyze();
    
    printf("Compiling: ");
    def->printExp();
    printf("\n");
    
    // Do a final optimization pass now that levels are assigned and
    // static analysis is done.
    IRNode::saveDot("before.dot");
    
    def = def->optimize();
    
    if (vectorize) {
        // vectorize across some variable
        // TODO: we're assuming its bounds are suitable for this - a multiple of four
        def = IRNode::make(Vector, def, 
                           def->substitute(vars[vectorDim], 
                                           IRNode::make(PlusImm, vars[vectorDim], 1)),
                           def->substitute(vars[vectorDim], 
                                           IRNode::make(PlusImm, vars[vectorDim], 2)),
                           def->substitute(vars[vectorDim], 
                                           IRNode::make(PlusImm, vars[vectorDim], 3)));
    }
    
    // Unroll across some vars
    roots = vector<IRNode::Ptr>(1);
    unroll = vector<int>(vars.size(), 1);
    roots[0] = def;
    
    for (size_t i = 0; i < vars.size(); i++) {
        int u = unroll[i] = varData(i)->unroll;
        const SteppedInterval &in = vars[i]->interval;        
        if (u > 1) {
            // sanity check
            if ((in.max() - in.min() + 1) % (u*vectorWidth[i])) {
                printf("Warning: Unrolling across a variable by an amount that is not a divisor of the range of the variable. Resulting code may crash.\n"); 
            }
            if (varData(i)->order != Parallel) {
                printf("Warning: Unrolling may reorder loads and stores between unrolled iterations, so it probably won't work for non-parallel variables.\n");
            }
            vector<IRNode::Ptr> newRoots;
            for (size_t k = 0; k < roots.size(); k++) {
                newRoots.push_back(roots[k]);
                for (int j = 1; j < u; j++) {                
                    IRNode::Ptr vNew = IRNode::make(PlusImm, vars[i], j*vectorWidth[i]);
                    newRoots.push_back(roots[k]->substitute(vars[i], vNew));
                }
            }
            newRoots.swap(roots);
        }
    }
    
    IRNode::saveDot("after.dot");
    
    // TODO: respect parallelize by generating different roots per thread ID?
    
    printf("Done transforming code\n");

    // Look for loads that are possibly aliased with the store and
    // increase their loop level to the same as the store
    IRNode::PtrSet loadSet, storeSet;
    for (size_t i = 0; i < roots.size(); i++) {
        collectInputs(roots[i], Load, loadSet);
        collectInputs(roots[i], LoadVector, loadSet);
        collectInputs(roots[i], Store, storeSet);
        collectInputs(roots[i], StoreVector, storeSet);
    }
    
    for (IRNode::PtrSet::iterator storeIter = storeSet.begin();
         storeIter != storeSet.end(); storeIter++) {
        IRNode::Ptr store = *storeIter;        
        SteppedInterval storeRange = lhs->interval + store->ival;
        printf("Store address bounds: %lld %lld\n", storeRange.min(), storeRange.max());
        for (IRNode::PtrSet::iterator loadIter = loadSet.begin();
             loadIter != loadSet.end(); loadIter++) {
            IRNode::Ptr load = *loadIter;
            IRNode::Ptr loadAddr = load->inputs[0];
            SteppedInterval loadRange = loadAddr->interval + load->ival;
            printf("Load address bounds: %lld : %lld\n", loadRange.min(), loadRange.max());
            
            int64_t distance = abs(loadRange - storeRange).min();
            if (distance == 0) {
                printf("Possible aliasing detected\n");
                printf("Promoting load at loop level %d to loop level %d\n", load->level, store->level);
                load->assignLevel(def->level);
            } else {
                printf("Load and store come within %lld of each other\n", distance);
            }
        }
    }
    
    // Clear the tag on all nodes
    for (size_t i = 0; i < IRNode::allNodes.size(); i++) {
        IRNode::Ptr n = IRNode::allNodes[i].lock();
        if (!n) continue;
        n->tag = 0;
    }
    
    // Then compute the order of evaluation (sets tag to 2)
    printf("Doing instruction scheduling\n");
    order.clear();
    doInstructionScheduling();
    printf("Done instruction scheduling\n");
}


void Compiler::doInstructionScheduling() {
    
    // Gather the nodes in a depth-first manner, and resize order to
    // be big enough. Also tag each node with the minimum depth to a root plus 100.
    for (size_t i = 0; i < roots.size(); i++) {
        if ((int)order.size() <= roots[i]->level)
            order.resize(roots[i]->level+1);
        gatherDescendents(roots[i], order, 100);
    }       
    
    // Stable sort the nodes from deepest to shallowest without
    // breaking any data dependencies. Also retag everything 2.
    for (size_t l = 0; l < order.size(); l++) {
        for (size_t i = 0; i < order[l].size(); i++) {
            IRNode::Ptr ni = order[l][i];
            for (size_t j = i+1; j < order[l].size(); j++) {
                IRNode::Ptr nj = order[l][j];
                if (ni->tag < nj->tag &&
                    find(nj->inputs.begin(), nj->inputs.end(), ni) == nj->inputs.end()) {
                    order[l][j] = ni;
                    order[l][j-1] = nj;
                } else {
                    break;
                }
            }
            ni->tag = 2;
        }
        
        for (size_t i = 0; i < order[l].size(); i++) {
            IRNode::Ptr ni = order[l][i];
            
            // Which node should get evaluated next? We'd like to be
            // able to clobber an input. Rate each node's input
            // according to how many unevaluated outputs it
            // has. Choose the node with the input with the lowest
            // rating. 1 is ideal, because it means we can clobber
            // that input. 2 or 3 is still good because we're getting
            // closer to being able to clobber that input.
            
            int bestRating = 0;
            IRNode::Ptr np;
            size_t location = 0;
            for (size_t j = i; j < order[l].size(); j++) {
                IRNode::Ptr nj = order[l][j];
                bool ready = true;
                for (size_t k = 0; k < nj->inputs.size(); k++) {
                    IRNode::Ptr nk = nj->inputs[k];
                    // If all inputs aren't evaluated yet, it's game
                    // over for this node
                    if (nk->tag != 3) ready = false;
                }
                if (!ready) continue;
                
                for (size_t k = 0; k < nj->inputs.size(); k++) {
                    IRNode::Ptr nk = nj->inputs[k];
                    
                    // Can't clobber inputs from a higher level
                    if (nk->level != (int)l) continue;
                    
                    // Can't clobber external vars
                    if (nk->op == Variable) continue;
                    
                    // Can't clobber inputs of a different width
                    if (nk->width != nj->width) continue;
                    
                    // Count how many outputs of this input are yet to be evaluated.
                    int remainingOutputs = 0;
                    for (size_t m = 0; m < nk->outputs.size(); m++) {
                        IRNode::Ptr nm = nk->outputs[m].lock();
                        
                        // Ignore those not participating in this computation
                        if (!nm || !nm->tag) continue;
                        
                        // Unevaluated outputs that aren't nj means you can't clobber
                        if (nm->tag != 3) remainingOutputs++;
                    }                    
                    
                    if (remainingOutputs < bestRating || !np) {
                        bestRating = remainingOutputs;
                        np = nj;
                        location = j;
                    }
                }
            }
            
            // Did we find a node to promote?
            if (np) {
                // Then bubble it up to just before ni
                while(location > i) {
                    order[l][location] = order[l][location-1];
                    location--;
                }
                order[l][i] = np;
                np->tag = 3;
            } else {
                ni->tag = 3;
            }
        }
    }
    
}

void Compiler::gatherDescendents(IRNode::Ptr node, vector<vector<IRNode::Ptr> > &output, int d) {
    // If I'm already in the output, bail
    if (node->tag > 1) {
        return;
    }    
    node->tag = d;
    for (size_t j = 0; j < node->inputs.size(); j++) {
        gatherDescendents(node->inputs[j], output, d+1);
    }
    output[node->level].push_back(node);
}
