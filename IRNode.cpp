#include "IRNode.h"
#include <stdarg.h>

void panic(const char *fmt, ...) {
    char message[1024];
    va_list arglist;
    va_start(arglist, fmt);
    vsnprintf(message, 1024, fmt, arglist);
    va_end(arglist);
    printf("%s", message);
    exit(-1);
}

void assert(bool cond, const char *fmt, ...) {
    if (cond) return;
    char message[1024];
    va_list arglist;
    va_start(arglist, fmt);
    vsnprintf(message, 1024, fmt, arglist);
    va_end(arglist);
    printf("%s", message);
    exit(-1);
}


map<float, IRNode::WeakPtr> IRNode::floatInstances;
map<int64_t, IRNode::WeakPtr> IRNode::intInstances;
vector<IRNode::WeakPtr> IRNode::allNodes;

IRNode::Ptr IRNode::make(float v) {
    if (floatInstances[v].expired()) {
        Ptr p = makeNew(v);
        floatInstances[v] = p;
        return p;
    }
    return floatInstances[v].lock();
};

IRNode::Ptr IRNode::make(int64_t v) {
    if (intInstances[v].expired()) {
        Ptr p = makeNew(v);
        intInstances[v] = p;
        return p;
    }
    return intInstances[v].lock();
};

IRNode::Ptr IRNode::make(OpCode opcode, 
                         IRNode::Ptr input1,
                         IRNode::Ptr input2,
                         IRNode::Ptr input3,
                         IRNode::Ptr input4,
                         int64_t ival,
                         float fval) {
    
    // collect the inputs into a vector
    vector<IRNode::Ptr> inputs;
    if (input1) {
        inputs.push_back(input1);
    }
    if (input2) {
        inputs.push_back(input2);
    }
    if (input3) {
        inputs.push_back(input3);
    }
    if (input4) {
        inputs.push_back(input4);
    }
    return make(opcode, inputs, ival, fval);
}

IRNode::Ptr IRNode::make(OpCode opcode,
                         vector<IRNode::Ptr> inputs,
                         int64_t ival, float fval) {
    
    // We will progressively modify the inputs to finally return a
    // node that is equivalent to the requested node on the
    // requested inputs. 
    
    /*
    printf("Making %s\n", opname[opcode]);
    for (size_t i = 0; i < inputs.size(); i++) {
        printf(" from: "); 
        inputs[i]->printExp();
        printf(" %d \n", inputs[i]->width);
    }
    */

    // Type inference and coercion
    Type t;
    int w;
    switch(opcode) {
    case Const:
        panic("Shouldn't make Consts using this make function\n");
    case NoOp:
        assert(inputs.size() == 1, "Wrong number of inputs for opcode: %s %d\n",
               opname[opcode], inputs.size());
        t = inputs[0]->type;
        w = inputs[0]->width;
        break;
    case Var:
        assert(inputs.size() == 0, "Wrong number of inputs for opcode: %s %d\n",
               opname[opcode], inputs.size());
        t = Int;
        w = 1;
        break;
    case Plus:
    case Minus:
    case Times:
    case Power:
    case Mod:
        assert(inputs.size() == 2, "Wrong number of inputs for opcode: %s %d\n",
               opname[opcode], inputs.size());
        // the output is either int or float
        if (inputs[0]->type == Float ||
            inputs[1]->type == Float) t = Float;
        else t = Int;
        
        // upgrade everything to the output type
        inputs[0] = inputs[0]->as(t);
        inputs[1] = inputs[1]->as(t);

        w = inputs[0]->width;
        assert(inputs[1]->width == w, "Inputs must have same vector width\n");
        break;
    case Divide:
    case ATan2:
        t = Float;
        inputs[0] = inputs[0]->as(Float);
        inputs[1] = inputs[1]->as(Float);
        w = inputs[0]->width;
        assert(inputs[1]->width == w, "Inputs must have same vector width\n");
        break;
    case Sin:
    case Cos:
    case Tan:
    case ASin:
    case ACos:
    case ATan:
    case Exp:
    case Log:
        assert(inputs.size() == 1, "Wrong number of inputs for opcode: %s %d\n", 
               opname[opcode], inputs.size());
        t = Float;
        inputs[0] = inputs[0]->as(Float);
        w = inputs[0]->width;
        break;
    case Abs:
        assert(inputs.size() == 1, "Wrong number of inputs for opcode: %s %d\n", 
               opname[opcode], inputs.size());
        if (inputs[0]->type == Bool) return inputs[0];
        t = inputs[0]->type;
        w = inputs[0]->width;
        break;
    case Floor:
    case Ceil:
    case Round:
        assert(inputs.size() == 1, "Wrong number of inputs for opcode: %s %d\n", 
               opname[opcode], inputs.size());    
        if (inputs[0]->type != Float) return inputs[0];
        t = Float; // TODO: perhaps Int?
        w = inputs[0]->width;
        break;
    case LT:
    case GT:
    case LTE:
    case GTE:
    case EQ:
    case NEQ:
        assert(inputs.size() == 2, "Wrong number of inputs for opcode: %s %d\n",
               opname[opcode], inputs.size());    
        if (inputs[0]->type == Float || inputs[1]->type == Float) {
            t = Float;                    
        } else { // TODO: compare ints?
            t = Bool;
        }
        inputs[0] = inputs[0]->as(t);
        inputs[1] = inputs[1]->as(t);
        t = Bool;
        w = inputs[0]->width;
        assert(inputs[1]->width == w, "Inputs must have same vector width\n");
        break;
    case And:
    case Nand:
        assert(inputs.size() == 2, "Wrong number of inputs for opcode: %s %d\n",
               opname[opcode], inputs.size());    
        // first arg is always bool
        inputs[0] = inputs[0]->as(Bool);
        t = inputs[1]->type;
        w = inputs[0]->width;
        assert(inputs[1]->width == w, "Inputs must have same vector width\n");
        break;
    case Or:               
        assert(inputs.size() == 2, "Wrong number of inputs for opcode: %s %d\n",
               opname[opcode], inputs.size());    
        if (inputs[0]->type == Float || inputs[1]->type == Float) {
            t = Float;
        } else if (inputs[0]->type == Int || inputs[0]->type == Int) {
            t = Int;
        } else {
            t = Bool;
        }
        inputs[0] = inputs[0]->as(t);
        inputs[1] = inputs[1]->as(t);
        w = inputs[0]->width;
        assert(inputs[1]->width == w, "Inputs must have same vector width\n");
        break;
    case IntToFloat:
        assert(inputs.size() == 1, "Wrong number of inputs for opcode: %s %d\n",
               opname[opcode], inputs.size());
        assert(inputs[0]->type == Int, "IntToFloat can only take integers\n");
        t = Float;
        w = inputs[0]->width;
        break;
    case FloatToInt:
        assert(inputs.size() == 1, "Wrong number of inputs for opcode: %s %d\n",
               opname[opcode], inputs.size());
        assert(inputs[0]->type == Float, "FloatToInt can only take floats\n");
        t = Int;
        w = inputs[0]->width;
        break;
    case PlusImm:
    case TimesImm:
        assert(inputs.size() == 1,
               "Wrong number of inputs for opcode: %s %d\n",
               opname[opcode], inputs.size());
        t = Int;
        w = inputs[0]->width;
        break;
    case LoadVector:
    case Load:
        assert(inputs.size() == 1,
               "Wrong number of inputs for opcode: %s %d\n",
               opname[opcode], inputs.size());
        inputs[0] = inputs[0]->as(Int);
        assert(inputs[0]->width == 1, "Can only load scalar addresses\n");
        w = (opcode == Load ? 1 : 4);
        t = Float;
        break;

    case SelectVector:
        assert(inputs.size() == 2, 
               "Wrong number of inputs for opcode: %s %d\n",
               opname[opcode], inputs.size());
        assert(inputs[0]->width == inputs[1]->width && inputs[0]->width > 1, 
               "SelectVector requires vector arguments\n");
        assert(ival > 0 && ival < inputs[0]->width, 
               "SelectVector requires an int immediate greater than zero and less than the vector width\n");
        w = inputs[0]->width;
        t = inputs[0]->type;
        break;
    case Vector:
        assert(inputs.size() == 4, 
               "Wrong number of inputs for opcode: %s %d\n",
               opname[opcode], inputs.size());

        for (size_t i = 0; i < inputs.size(); i++) {
            assert(inputs[i]->width == 1, "Components of Vector must be scalar\n");
        }
        
        // Force all inputs to the same type
        t = Int;
        for (size_t i = 0; i < inputs.size(); i++) {
            if (inputs[i]->type == Float) t = Float;
        }
        for (size_t i = 0; i < inputs.size(); i++) {
            inputs[i] = inputs[i]->as(t);
        }
        w = (int)inputs.size();
        break;
    }
    
    // Constant folding
    bool allInputsConst = true;
    for (size_t i = 0; i < inputs.size() && allInputsConst; i++) {
        if (!inputs[i]->constant) allInputsConst = false;
        // TODO: make constant folding work with vector types
        if (inputs[i]->width != 1) allInputsConst = false;
    }
    if (allInputsConst && inputs.size()) {
        switch(opcode) {

        case Plus:
            if (t == Float) {
                return make(inputs[0]->fval + inputs[1]->fval);
            } else {
                return make(inputs[0]->ival + inputs[1]->ival);
            }
        case Minus:
            if (t == Float) {
                return make(inputs[0]->fval - inputs[1]->fval);
            } else {
                return make(inputs[0]->ival - inputs[1]->ival);
            }
        case Times:
            if (t == Float) {
                return make(inputs[0]->fval * inputs[1]->fval);
            } else {
                return make(inputs[0]->ival * inputs[1]->ival);
            }
        case PlusImm:
            return make(inputs[0]->ival + ival);
        case TimesImm:
            return make(inputs[0]->ival * ival);
        case Divide:
            printf("Folding constant: %f / %f\n", inputs[0]->fval, inputs[1]->fval);
            return make(inputs[0]->fval / inputs[1]->fval);
        case And:
            if (t == Float) {
                return make(inputs[0]->ival ? inputs[1]->fval : 0.0f);
            } else {
                return make(inputs[0]->ival ? inputs[1]->ival : 0ll);
            }
        case Or:
            if (t == Float) {
                return make(inputs[0]->fval + inputs[1]->fval);
            } else {
                return make(inputs[0]->ival | inputs[1]->ival);
            }
        case Nand:
            if (t == Float) {
                return make(!inputs[0]->ival ? inputs[1]->fval : 0.0f);
            } else {
                return make(!inputs[0]->ival ? inputs[1]->ival : 0ll);
            }
        case IntToFloat:
            assert(inputs[0]->ival>>32 == 0, "IntToFloat on 64bit value 0x%llx will lose data\n", inputs[0]->ival);
            return make((float)inputs[0]->ival);
        case FloatToInt:
            return make((int64_t)inputs[0]->fval);
        default:
            // TODO: transcendentals, pow, floor, comparisons, etc
            break;
        }
    }

    // Strength reduction rules.
    if (opcode == NoOp) {
        return inputs[0];
    }

    // Push vectors downwards
    if (opcode == Vector) {
        bool allChildrenSameOp = true;
        for (size_t i = 1; i < inputs.size(); i++) {
            if (inputs[0]->op != inputs[i]->op || 
                inputs[0]->fval != inputs[i]->fval ||
                inputs[0]->ival != inputs[i]->ival) allChildrenSameOp = false;            
        }
        if (allChildrenSameOp && 
            inputs[0]->op != Const &&
            inputs[0]->op != Var &&            
            inputs[0]->op != Load) {
            vector<IRNode::Ptr> childInputs(inputs[0]->inputs.size());
            for (size_t j = 0; j < childInputs.size(); j++) {
                childInputs[j] = make(Vector, 
                                      inputs[0]->inputs[j], inputs[1]->inputs[j],
                                      inputs[2]->inputs[j], inputs[3]->inputs[j]);
            }
            return make(inputs[0]->op, childInputs, inputs[0]->ival, inputs[0]->fval);
        }

        // Special case a vector of four loadimms with offsets incrementing by 4 bytes
        bool vectorLoad = true;
        for (size_t i = 0; i < inputs.size() && vectorLoad; i++) {
            if (inputs[i]->op != Load) vectorLoad = false;
            if (vectorLoad && inputs[i]->inputs[0] != inputs[0]->inputs[0]) vectorLoad = false;
            if (vectorLoad && inputs[i]->ival != inputs[0]->ival + i*4) vectorLoad = false;
        }
        if (vectorLoad) {
            IRNode::Ptr v = make(LoadVector, inputs[0]->inputs[0], NULL_IRNODE_PTR, NULL_IRNODE_PTR, NULL_IRNODE_PTR, inputs[0]->ival);
            return v;
        }
    }


    // Replace division of a lower level node with multiplication by its inverse
    if (opcode == Divide && inputs[1]->level < inputs[0]->level) {
        // x/y = x*(1/y)
        return make(Times, inputs[0], 
                    make(Divide, make(1.0f), inputs[1]));
    }

    // (x+a)*b = x*b + a*b (where a and b are both constant integers)
    if (opcode == TimesImm && inputs[0]->op == PlusImm) {
        return make(PlusImm, 
                    make(TimesImm, inputs[0]->inputs[0], NULL_IRNODE_PTR, NULL_IRNODE_PTR, NULL_IRNODE_PTR, ival),
                    NULL_IRNODE_PTR, NULL_IRNODE_PTR, NULL_IRNODE_PTR, ival * inputs[0]->ival);
    }

    // (x*a)*b = x*(a*b) where a and b are more const than x. This
    // should be replaced with generic product rebalancing,
    // similar to the sum rebalancing.
    if (opcode == Times) {
        IRNode::Ptr x, a, b;
        if (inputs[0]->op == Times) {
            x = inputs[0]->inputs[0];
            a = inputs[0]->inputs[1];
            b = inputs[1];
        } else if (inputs[1]->op == Times) {
            x = inputs[1]->inputs[0];
            a = inputs[1]->inputs[1];
            b = inputs[0];
        }

        if (x) {
            // x is the higher level of x and a
            if (x->level < a->level) {
                IRNode::Ptr tmp = x;
                x = a;
                a = tmp;
            }
                    
            // it's only worth rebalancing if a and b are both
            // lower level than x (e.g. (x+y)*3)
            if (x->level > a->level && x->level > b->level) {
                return make(Times, 
                            x,
                            make(Times, a, b));
            }
        }
    }

    // Some more strength reduction rules that need to be updated for the type system
    /*
      if (opcode == Times) {
      // 0*x = 0
      if (inputs[0]->op == Const &&
      inputs[0]->type == Float &&
      inputs[0]->fval == 0.0f) {
      return make(0.0f);
      } 

      // x*0 = 0
      if (inputs[1]->op == ConstFloat && inputs[1]->val == 0.0f) {
      return make(0);
      }

      // 1*x = x
      if (inputs[0]->op == ConstFloat && inputs[0]->val == 1.0f) {
      return inputs[1];
      } 
      // x*1 = x
      if (inputs[1]->op == ConstFloat && inputs[1]->val == 1.0f) {
      return inputs[0];
      }

      // 2*x = x+x
      if (inputs[0]->op == ConstFloat && inputs[0]->val == 2.0f) {
      return make(Plus, inputTypes[1], inputs[1], inputTypes[1], inputs[1]);
      } 
      // x*2 = x+x
      if (inputs[1]->op == ConstFloat && inputs[1]->val == 2.0f) {
      return make(Plus, inputTypes[0], inputs[0], inputTypes[0], inputs[0]);
      }
      }

      if (opcode == Power && inputs[1]->op == ConstFloat) {
      if (inputs[1]->val == 0.0f) {        // x^0 = 1
      return make(1.0f);
      } else if (inputs[1]->val == 1.0f) { // x^1 = x
      return inputs[0];
      } else if (inputs[1]->val == 2.0f) { // x^2 = x*x
      return make(Times, inputTypes[0], inputs[0], inputTypes[0], inputs[0]);
      } else if (inputs[1]->val == 3.0f) { // x^3 = x*x*x
      IRNode::Ptr squared = make(Times, inputTypes[0], inputs[0], inputTypes[0], inputs[0]);
      return make(t, Times, t, squared, inputTypes[0], inputs[0]);
      } else if (inputs[1]->val == 4.0f) { // x^4 = x*x*x*x
      IRNode::Ptr squared = make(Times, inputTypes[0], inputs[0], inputTypes[0], inputs[0]);
      return make(Times, t, squared, t, squared);                    
      } else if (inputs[1]->val == floorf(inputs[1]->val) &&
      inputs[1]->val > 0) {
      // iterated multiplication
      int power = (int)floorf(inputs[1]->val);

      // find the largest power of two less than power
      int pow2 = 1;
      while (pow2 < power) pow2 = pow2 << 1;
      pow2 = pow2 >> 1;                                       
      int remainder = power - pow2;
                    
      // make a stack x, x^2, x^4, x^8 ...
      // and multiply the appropriate terms from it
      vector<IRNode::Ptr > powStack;
      powStack.push_back(inputs[0]);
      IRNode::Ptr result = (power & 1) ? inputs[0] : NULL;
      IRNode::Ptr last = inputs[0];
      for (int i = 2; i < power; i *= 2) {
      last = make(Times, last->type, last, last->type, last);
      powStack.push_back(last);
      if (power & i) {
      if (!result) result = last;
      else {
      result = make(Times,
      result->type, result,
      last->type, last);
      }
      }
      }
      return result;
      }

      // todo: negative powers (integer)               
      }            
    */

    // Rebalance summations whenever you hit a node that isn't a sum but might have sums for children.
    if (opcode != Plus && opcode != Minus && opcode != PlusImm) {
        for (size_t i = 0; i < inputs.size(); i++) {
            inputs[i] = inputs[i]->rebalanceSum();
        }
    }              

    // Don't merge or modify vars
    if (opcode == Var) {
        vector<IRNode::Ptr> empty;
        return makeNew(t, 1, opcode, empty, 0, 0.0f);
    }

    // Fuse instructions
        
    // Load of something plus an int constant can be converted to a load with offset
    if (opcode == Load || opcode == LoadVector) {
        if (inputs[0]->op == Plus) {
            IRNode::Ptr left = inputs[0]->inputs[0];
            IRNode::Ptr right = inputs[0]->inputs[1];
            if (left->op == Const) {
                IRNode::Ptr n = make(opcode, right, 
                                     NULL_IRNODE_PTR, NULL_IRNODE_PTR, NULL_IRNODE_PTR,
                                     left->ival + ival);
                return n;
            } else if (right->op == Const) {
                IRNode::Ptr n = make(opcode, left,
                                     NULL_IRNODE_PTR, NULL_IRNODE_PTR, NULL_IRNODE_PTR,
                                     right->ival + ival);
                return n;
            }
        } else if (inputs[0]->op == Minus &&
                   inputs[0]->inputs[1]->op == Const) {
            IRNode::Ptr n = make(opcode, inputs[0]->inputs[0], 
                                 NULL_IRNODE_PTR, NULL_IRNODE_PTR, NULL_IRNODE_PTR, 
                                 -inputs[0]->inputs[1]->ival + ival);
            return n;
        } else if (inputs[0]->op == PlusImm) {
            IRNode::Ptr n = make(opcode, inputs[0]->inputs[0],
                                 NULL_IRNODE_PTR, NULL_IRNODE_PTR, NULL_IRNODE_PTR,
                                 inputs[0]->ival + ival);
            return n;
        }
    }

    // Unaligned vector loads that may have nearby aligned loads
    // should just use VSelect on the aligned loads instead
    if (opcode == LoadVector && 
        !(inputs[0]->modulus & 15)) {
        int64_t offset = (inputs[0]->remainder + ival) & 15;
        // If it's word-aligned but not 16-byte aligned we can do something.
        if (offset != 0 && ((offset & 3) == 0)) {
            // Check to see if the aligned equivalents exist
            IRNode::Ptr n1 = make(LoadVector, inputs[0], 
                                  NULL_IRNODE_PTR, NULL_IRNODE_PTR, NULL_IRNODE_PTR,
                                  ival - offset);
            IRNode::Ptr n2 = make(LoadVector, inputs[0], 
                                  NULL_IRNODE_PTR, NULL_IRNODE_PTR, NULL_IRNODE_PTR,
                                  ival + 16 - offset);
            // TODO: just because the aligned equivalents currently
            // exist doesn't mean they're in use in the same
            // kernel. We really need contexts or something.
            if (n1->outputs.size() || n2->outputs.size() || 1) {
                printf("Converting unaligned load to two aligned loads\n");
                return make(SelectVector, n1, n2, NULL_IRNODE_PTR, NULL_IRNODE_PTR,
                            offset/4);
            }
        }
    }

    // Times an int constant can be fused into a times immediate provided the immediate is 32-bit
    if (opcode == Times && t == Int) {
        IRNode::Ptr left = inputs[0];
        IRNode::Ptr right = inputs[1];
        if (left->op == Const && (left->ival >> 32 == 0 || left->ival >> 31 == -1)) {
            IRNode::Ptr n = make(TimesImm, right, 
                             NULL_IRNODE_PTR, NULL_IRNODE_PTR, NULL_IRNODE_PTR,
                             left->ival);
            return n;
        } else if (right->op == Const && (right->ival >> 32 == 0 || right->ival >> 31 == -1)) {
            IRNode::Ptr n = make(TimesImm, left, 
                             NULL_IRNODE_PTR, NULL_IRNODE_PTR, NULL_IRNODE_PTR,
                             right->ival);
            return n;
        }
    }

    // Common subexpression elimination - check if one of the
    // inputs already has a parent that does the same op to the same children.
    if (inputs.size() && inputs[0]->outputs.size() ) {
        for (size_t i = 0; i < inputs[0]->outputs.size(); i++) {
            IRNode::Ptr candidate = inputs[0]->outputs[i].lock();
            // Redo any analysis in case something has changed
            if (!candidate) continue;
            candidate->analyze();
            if (candidate->ival != ival) continue;
            if (candidate->fval != fval) continue;
            if (candidate->op != opcode) continue;
            if (candidate->type != t) continue;
            if (candidate->inputs.size() != inputs.size()) continue;
            bool inputsMatch = true;
            for (size_t j = 0; j < inputs.size(); j++) {
                if (candidate->inputs[j] != inputs[j]) inputsMatch = false;
            }
            // it's the same op on the same inputs, reuse the old node 
            if (inputsMatch) return candidate;
        }
    }


    // We didn't see any reason to fuse or modify this op, so make a new node.
    return makeNew(t, w, opcode, inputs, ival, fval);
}

// Any optimizations that must be done after generation is
// complete. Rebuilds the graph and then does the final sum
// rebalancing.
IRNode::Ptr IRNode::optimize() {
    // Don't rebuild constants or vars
    if (op == Const) return self.lock();
    if (op == Var) return self.lock();

    vector<IRNode::Ptr > newInputs(inputs.size());
    for (size_t i = 0; i < inputs.size(); i++) {
        newInputs[i] = inputs[i]->optimize();
    }
    IRNode::Ptr newNode = make(op, newInputs, ival, fval); 

    // if I don't have any sum parents who will do it for me, call rebalanceSum
    bool sumParent = false;
    for (size_t i = 0; i < outputs.size(); i++) {
        Ptr out = outputs[i].lock();
        if (!out) continue;
        if (out->op == Plus || 
            out->op == Minus ||
            out->op == PlusImm) sumParent = true;
    }
    if (!sumParent) newNode = newNode->rebalanceSum();
    
    return newNode;
}        

// type coercion
IRNode::Ptr IRNode::as(Type t) {
    if (t == type) return self.lock();

    // insert new casting operators
    if (type == Int) {
        if (t == Float) 
            return make(IntToFloat, self.lock());
        if (t == Bool)
            return make(NEQ, self.lock(), make(0ll));
    }

    if (type == Bool) {            
        if (t == Float) 
            return make(And, self.lock(), make(1.0f));
        if (t == Int) 
            return make(And, self.lock(), make(1ll));
    }

    if (type == Float) {
        if (t == Bool) 
            return make(NEQ, self.lock(), make(0.0f));
        if (t == Int) 
            return make(FloatToInt, self.lock());
    }            

    panic("Casting to/from unknown type\n");
    return self.lock();
            
}

void IRNode::assignLevel(unsigned char l) {
    if (level == l) return;
    level = l;
    // Tell my (living) parents I got a promotion (they'll tell the grandparents).
    for (size_t i = 0; i < outputs.size(); i++) {
        Ptr out = outputs[i].lock();
        if (!out) continue;
        if (out->level < level)
            out->assignLevel(level);
    }
}

void IRNode::printExp() {
    switch(op) {
    case Const:
        if (type == Float) printf("%f", fval);
        else printf("%lld", ival);
        break;
    case Var:
        printf("var");
        break;
    case Plus:
        printf("(");
        inputs[0]->printExp();
        printf("+");
        inputs[1]->printExp();
        printf(")");
        break;
    case PlusImm:
        printf("(");
        inputs[0]->printExp();
        printf("+%lld)", ival);
        break;
    case TimesImm:
        printf("(");
        inputs[0]->printExp();
        printf("*%lld)", ival);
        break;
    case Minus:
        printf("(");
        inputs[0]->printExp();
        printf("-");
        inputs[1]->printExp();
        printf(")");
        break;
    case Times:
        printf("(");
        inputs[0]->printExp();
        printf("*");
        inputs[1]->printExp();
        printf(")");
        break;
    case Divide:
        printf("(");
        inputs[0]->printExp();
        printf("/");
        inputs[1]->printExp();
        printf(")");
        break;
    case Load: 
        printf("Load(");
        inputs[0]->printExp();
        printf("+%lld)", ival);
        break;
    case LoadVector:
        printf("LoadVector(");
        inputs[0]->printExp();
        printf("+%lld)", ival);
        break;        
    case SelectVector:
        printf("SelectVector(");
        inputs[0]->printExp();
        printf(", ");
        inputs[1]->printExp();
        printf(", %lld)", ival);
        break;
    default:
        if (inputs.size() == 0) {
            printf("%s", opname[op]);
        } else {
            printf("%s(", opname[op]); 
            inputs[0]->printExp();
            for (size_t i = 1; i < inputs.size(); i++) {
                printf(", ");
                inputs[1]->printExp();
            }
            printf(")");
        }
        break;
    }
}

void IRNode::print() {
            
    if (reg < 16)
        printf("r%d = ", reg);
    else 
        printf("xmm%d = ", reg-16);

    vector<string> args(inputs.size());
    char buf[32];
    for (size_t i = 0; i < inputs.size(); i++) {
        if (inputs[i]->reg < 0) 
            sprintf(buf, "%lld", inputs[i]->ival);
        else if (inputs[i]->reg < 16)
            sprintf(buf, "r%d", inputs[i]->reg);
        else
            sprintf(buf, "xmm%d", inputs[i]->reg-16);
        args[i] += buf;
    }
            
    switch (op) {
    case Const:
        if (type == Float) printf("%f", fval);
        else printf("%lld", ival);
        break;                
    case Plus:
        printf("%s + %s", args[0].c_str(), args[1].c_str());
        break;
    case Minus:
        printf("%s - %s", args[0].c_str(), args[1].c_str());
        break;
    case Times:
        printf("%s * %s", args[0].c_str(), args[1].c_str());
        break;
    case Divide:
        printf("%s / %s", args[0].c_str(), args[1].c_str());
        break;
    case PlusImm:
        printf("%s + %lld", args[0].c_str(), ival);
        break;
    case TimesImm:
        printf("%s * %lld", args[0].c_str(), ival);
        break;
    case LoadVector:
        printf("LoadVector %s + %lld", args[0].c_str(), ival);
        break;
    case Load:
        printf("Load %s + %lld", args[0].c_str(), ival);
        break;
    case SelectVector:
        printf("SelectVector %s %s %lld", args[0].c_str(), args[1].c_str(), ival);
        break;
    default:
        printf("%s", opname[op]);
        for (size_t i = 0; i < args.size(); i++)
            printf(" %s", args[i].c_str());
        break;
    }

    printf("\n");
}

void IRNode::saveDot(const char *filename) {
    FILE *f = fopen(filename, "w");
    fprintf(f, "digraph G {\n");
    char id[256];
    for (size_t i = 0; i < allNodes.size(); i++) {
        IRNode::Ptr n = allNodes[i].lock();
        if (!n) continue;
        if (n->ival) 
            fprintf(f, "  n%llx [label = \"%s %lld\"]\n", (long long)n.get(), opname[n->op], n->ival);
        else if (n->fval)  
            fprintf(f, "  n%llx [label = \"%s %f\"]\n", (long long)n.get(), opname[n->op], n->fval);           
        else 
            fprintf(f, "  n%llx [label = \"%s\"]\n", (long long)n.get(), opname[n->op]);            
        for (size_t j = 0; j < n->inputs.size(); j++) {
            IRNode::Ptr in = n->inputs[j];
            fprintf(f, "  n%llx -> n%llx\n", (long long)n.get(), (long long)in.get());
        }
        /*
        for (size_t j = 0; j < n->outputs.size(); j++) {
            IRNode::Ptr out = n->outputs[j].lock();
            if (!out) continue;
            fprintf(f, "  n%x -> n%x [color = \"gray\"]\n", (long long)out.get(), (long long)n.get());
        }        
        */
    } 
    fprintf(f, "}\n");
    fclose(f);
}

// make a new version of this IRNode with one node replaced with
// another. Also optimizes.
IRNode::Ptr IRNode::substitute(IRNode::Ptr a, IRNode::Ptr b) {
    if (self.lock() == a) return b;

    // Don't rebuild consts or vars
    if (op == Const) return self.lock();
    if (op == Var) return self.lock();

    // rebuild all the inputs
    vector<IRNode::Ptr > newInputs(inputs.size());
    for (size_t i = 0; i < newInputs.size(); i++) {
        newInputs[i] = inputs[i]->substitute(a, b);
    }

    IRNode::Ptr newNode = make(op, newInputs, ival, fval);

    //printf("This: "); printExp(); printf(" @ %d\n", level);
    //printf("Became: "); newNode->printExp(); printf(" @ %d\n", newNode->level);

    // if I don't have any sum parents who will do it for me, call rebalanceSum
    bool sumParent = false;
    for (size_t i = 0; i < outputs.size(); i++) {
        Ptr out = outputs[i].lock();
        if (!out) continue;
        if (out->op == Plus || 
            out->op == Minus ||
            out->op == PlusImm) sumParent = true;
    }
    if (!sumParent) newNode = newNode->rebalanceSum();

    return newNode;
}

IRNode::Ptr IRNode::rebalanceSum() {
    if (op != Plus && op != Minus && op != PlusImm) return self.lock();
            
    // collect all the inputs
    vector<pair<IRNode::Ptr , bool> > terms;

    collectSum(terms);

    // extract out the const terms
    vector<pair<IRNode::Ptr , bool> > constTerms;
    vector<pair<IRNode::Ptr , bool> > nonConstTerms;
    for (size_t i = 0; i < terms.size(); i++) {
        if (terms[i].first->op == Const) {
            constTerms.push_back(terms[i]);
        } else {
            nonConstTerms.push_back(terms[i]);
        }
    }
            
    // sort the non-const terms by level
    for (size_t i = 0; i < nonConstTerms.size(); i++) {
        for (size_t j = i+1; j < nonConstTerms.size(); j++) {
            int li = nonConstTerms[i].first->level;
            int lj = nonConstTerms[j].first->level;
            if (li > lj) {
                pair<IRNode::Ptr , bool> tmp = nonConstTerms[i];
                nonConstTerms[i] = nonConstTerms[j];
                nonConstTerms[j] = tmp;
            }
        }
    }

    // remake the summation
    IRNode::Ptr t;
    bool tPos;
    t = nonConstTerms[0].first;
    tPos = nonConstTerms[0].second;

    // If we're building a float sum, the const term is innermost
    if (type == Float) {
        float c = 0.0f;
        for (size_t i = 0; i < constTerms.size(); i++) {
            if (constTerms[i].second) {
                c += constTerms[i].first->fval;
            } else {
                c -= constTerms[i].first->fval;
            }
        }
        if (c != 0.0f) {
            if (tPos) 
                t = make(Plus, make(c), t);
            else
                t = make(Minus, make(c), t);
        }
    }

    // if we're building an int sum, a 32-bit const term is outermost
    // so that loads/plusimm/timesimm can pick it up. The 64-bit
    // component is innermost.
    int64_t innerConst = 0;
    int64_t outerConst = 0;
    if (type == Int) {
        int64_t c = 0;
        for (size_t i = 0; i < constTerms.size(); i++) {
            if (constTerms[i].second) {
                c += constTerms[i].first->ival;
            } else {
                c -= constTerms[i].first->ival;
            }
        }

        if ((int32_t)c != c) {
            // Look for an existing 64-bit const within 32-bits of this value
            bool baseFound = false;
            map<int64_t, IRNode::WeakPtr>::iterator iter;
            for (iter = intInstances.begin();
                 iter != intInstances.end();
                 iter++) {
                int64_t val = iter->first;
                int64_t offset = c - val;
                if ((int32_t)offset == offset) {
                    printf("Using %lld as a base for %lld\n", val, c);
                    baseFound = true;
                    innerConst = val;
                    outerConst = offset;
                    break;
                }
            }

            if (!baseFound) {
                outerConst = 0;
                innerConst = c;
            }
        } else {
            innerConst = 0;
            outerConst = c;
        }

        if (innerConst) {
            if (tPos) {
                t = make(Plus, t, make(innerConst));
            } else {
                t = make(Minus, make(innerConst), t);
            }
            tPos = true;
        }
        
    }

    for (size_t i = 1; i < nonConstTerms.size(); i++) {
        bool nextPos = nonConstTerms[i].second;
        if (tPos == nextPos)
            t = make(Plus, t, nonConstTerms[i].first);
        else if (tPos) // and not nextPos
            t = make(Minus, t, nonConstTerms[i].first);
        else { // nextPos and not tPos
            tPos = true;
            t = make(Minus, nonConstTerms[i].first, t);
        }                    
    }

    if (outerConst != 0) {
        if (tPos) {
            t = make(PlusImm, t, NULL_IRNODE_PTR, NULL_IRNODE_PTR, NULL_IRNODE_PTR, outerConst);
        } else {
            // TODO: in this case it was pointless to divide the
            // constant term up into an inner and outer const
            t = make(Minus, make(outerConst), t);
        }
    }


    return t;
}


void IRNode::collectSum(vector<pair<IRNode::Ptr , bool> > &terms, bool positive) {
    if (op == Plus) {
        inputs[0]->collectSum(terms, positive);
        inputs[1]->collectSum(terms, positive);
    } else if (op == Minus) {
        inputs[0]->collectSum(terms, positive);
        inputs[1]->collectSum(terms, !positive);                
    } else if (op == PlusImm) {
        inputs[0]->collectSum(terms, positive);
        terms.push_back(make_pair(make(ival), true));
    } else {
        terms.push_back(make_pair(self.lock(), positive));
    }
}

// TODO: rebalance product

IRNode::Ptr IRNode::makeNew(Type t, int w, OpCode opcode, 
                            const vector<IRNode::Ptr> &inputs,
                            int64_t iv, float fv) {
    // This is the only place where "new IRNode" should appear
    Ptr sharedPtr(new IRNode(t, w, opcode, inputs, iv, fv));
    WeakPtr weakPtr(sharedPtr);
    sharedPtr->self = weakPtr;

    for (size_t i = 0; i < inputs.size(); i++) {
        inputs[i]->outputs.push_back(weakPtr);
    }

    allNodes.push_back(weakPtr);

    return sharedPtr;
}

IRNode::Ptr IRNode::makeNew(float v) {
    vector<IRNode::Ptr> empty;
    return makeNew(Float, 1, Const, empty, 0, v);
}

IRNode::Ptr IRNode::makeNew(int64_t v) {
    if ((v>>32) != 0 && (v>>31) != -1) {
        printf("We only trust 32 bit values for now: 0x%llx\n", v);
    }
    vector<IRNode::Ptr> empty;
    return makeNew(Int, 1, Const, empty, v, 0);
}

unsigned int gcd(unsigned int a, unsigned int b) {
    unsigned int tmp;
    while (b) {
        tmp = b;
        b = a % b;
        a = tmp;
    }
    return a;
}

// Only makeNew may call this
IRNode::IRNode(Type t, int w, OpCode opcode, 
               const vector<IRNode::Ptr> &input,
               int64_t iv, float fv) {
    ival = iv;
    fval = fv;

    for (size_t i = 0; i < input.size(); i++) 
        inputs.push_back(input[i]);

    type = t;
    width = w;

    op = opcode;
    level = 0;


    modulus = remainder = 0;

    if (op == Var) {
        constant = false;
    } else {
        constant = true;
    }

    for (size_t i = 0; i < inputs.size(); i++) {
        constant = constant && inputs[i]->constant;
        if (inputs[i]->level > level) level = inputs[i]->level;
    }

    // No register assigned yet
    reg = -1;

    // Do static analysis
    analyze();
}


void IRNode::analyze() {
    if (type == Int) {
        if (op == PlusImm) {

            modulus = inputs[0]->modulus;
            remainder = (inputs[0]->remainder + ival) % modulus;
            while (remainder < 0) remainder += modulus;

        } else if (op == TimesImm) {

            int64_t mod = (int64_t)inputs[0]->modulus * ival;
            if (mod < 0) mod = -mod;
            if ((mod>>32) || (abs(ival)>>32)) {
                //printf("Bailing out of modulus analysis due to potential overflow\n");
                modulus = 1;
                remainder = 0;
            } else {
                modulus = (unsigned int)mod;
                remainder = (inputs[0]->remainder * ival) % modulus;
            }
            while (remainder < 0) remainder += modulus;

        } else if (op == Plus) {           
            if (inputs[0]->op == Const) {
                modulus = inputs[1]->modulus;
                remainder = (inputs[1]->remainder + inputs[0]->ival) % modulus;
                while (remainder < 0) remainder += modulus;
            } else if (inputs[1]->op == Const) {
                modulus = inputs[0]->modulus;
                remainder = (inputs[0]->remainder + inputs[1]->ival) % modulus;
                while (remainder < 0) remainder += modulus;
            } else {
                modulus = gcd(inputs[0]->modulus, inputs[1]->modulus);
                if (inputs[0]->modulus == inputs[1]->modulus) modulus = inputs[0]->modulus;
                remainder = (inputs[0]->remainder + inputs[1]->remainder) % modulus;
            }

        } else if (op == Minus) {
            if (inputs[0]->op == Const) {
                modulus = inputs[1]->modulus;
                remainder = (inputs[1]->remainder - inputs[0]->ival) % modulus;
                while (remainder < 0) remainder += modulus;
            } else if (inputs[1]->op == Const) {
                modulus = inputs[0]->modulus;
                remainder = (inputs[0]->remainder - inputs[1]->ival) % modulus;
                while (remainder < 0) remainder += modulus;
            } else {
                modulus = gcd(inputs[0]->modulus, inputs[1]->modulus);
                if (inputs[0]->modulus == inputs[1]->modulus) modulus = inputs[0]->modulus;
                remainder = (modulus + inputs[0]->remainder - inputs[1]->remainder) % modulus;
            }

        } else if (op == Times && inputs[0]->op == Const) {
            int64_t mod = (int64_t)inputs[1]->modulus * (int64_t)inputs[0]->ival;
            if (mod < 0) mod = -mod;
            if ((mod>>32) || (abs(inputs[0]->ival)>>32)) {
                modulus = 1;
                remainder = 0;
            } else {
                modulus = (unsigned int)mod;
                remainder = (inputs[1]->remainder * ival) % modulus;
            }
            while (remainder < 0) remainder += modulus;            

        } else if (op == Times && inputs[1]->op == Const) {
            int64_t mod = (int64_t)inputs[0]->modulus * (int64_t)inputs[1]->ival;
            if (mod < 0) mod = -mod;
            if ((mod>>32) || (abs(inputs[1]->ival)>>32)) {
                modulus = 1;
                remainder = 0;
            } else {
                modulus = (unsigned int)mod;
                remainder = (inputs[0]->remainder * ival) % modulus;
            }
            while (remainder < 0) remainder += modulus;            
        } else {
            modulus = 1;
            remainder = 0;
        }
    }
};

IRNode::~IRNode() {
    // Oh, I guess nobody wants me any more. I'd better not have any live outputs then
    for (size_t i = 0; i < outputs.size(); i++) {
        assert(outputs[i].expired(), "IRNode with live outputs being destroyed!\n");
    }

    // Tell the children not to bother calling anymore
    for (size_t i = 0; i < inputs.size(); i++) {
        Ptr in = inputs[i];
        for (size_t j = 0; j < in->outputs.size(); j++) {
            Ptr out = in->outputs[j].lock();
            if (out.get() == this) {
                in->outputs[j] = in->outputs[in->outputs.size()-1];
                in->outputs.pop_back();
                j--;
            }
        }
    }

    // Remove myself from any arrays I might belong to
    if (op == Const) {
        if (type == Int) {
            intInstances.erase(intInstances.find(ival));
        } else if (type == Float) {
            floatInstances.erase(floatInstances.find(fval));
        }
    }

    for (size_t i = 0; i < allNodes.size(); i++) {
        if (allNodes[i].lock().get() == this) {
            allNodes[i] = allNodes[allNodes.size()-1];
            allNodes.pop_back();
            break;
        }
    }
}

