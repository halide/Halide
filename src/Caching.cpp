#include "Caching.h"
#include "IRMutator.h"

namespace Halide {
namespace Internal {

namespace {

class KeyInfo {
public:
  KeyInfo(Function function) {
  }

  // Return the number of bytes needed to store the cache key
  // for the target function of this clas.
  size_t KeySize();

  // Code to fill int he Allocation named key_name with the byte of
  // the key. The Allocation is guaranteed to be 1d, of type uint8_t
  // and of the size returned from KeySize
  Stmt GenerateKey(std::string key_name);

  // Returns a bool expression, which either evaluates to true,
  // in which case the Allocation named by storage will be filled in,
  // or false.
  Expr GenerateLookup(std::string key_allocation_name, std::string storage_allocation_name);

  // Returns a statement which will store the result of a computation under this key
  Stmt StoreComputation(std::string key_allocation_name, std::string storage_allocation_name);
};

}
// Inject caching structure around compute_cached realizations.
class InjectCaching : public IRMutator {
public:
  const std::map<std::string, Function> &env;

    InjectCaching(const std::map<std::string, Function> &e) :
        env(e) {}
private:

    using IRMutator::visit;

    void visit(const Pipeline *op) {
        std::map<std::string, Function>::const_iterator f = env.find(op->name);
        if (f != env.end() &&
	    f->second.schedule().cached) {

#if 0
	  Allocate cache_hit {
    	    Allocate cache_key { // Need a way to get size.
	      replace produce node with
		Block {
		   compute cache key
		     let cache_hit_tmp cache_lookup(cache_key, storage) {
		     Pipeline {
		       IfThenElse (!cache_hit) {
			 current produce
		       }
		       IfThenElse (!cache_hit) {
			 current UPDATE
		       }
		       Block {
			 IfThenElse (!cache_hit) {
			   cache_store(cache_key, storage);
			 }
			 current consume
		       }
		     }
		   }
		   Free cache_key;
	      }
	      Free cache_hit;
	    }

        std::string old = producing;
        producing = op->name;
        Stmt produce = mutate(op->produce);
        Stmt update;
        if (op->update.defined()) {
            update = mutate(op->update);
        }
        producing = old;
        Stmt consume = mutate(op->consume);

        if (produce.same_as(op->produce) &&
            update.same_as(op->update) &&
            consume.same_as(op->consume)) {
            stmt = op;
        } else {
            stmt = Pipeline::make(op->name, produce, update, consume);
        }
#endif
	  }
	}
};

Stmt inject_caching(Stmt s, const std::map<std::string, Function> &env) {
  InjectCaching injector(env);

  return injector.mutate(s);
}

}
}
