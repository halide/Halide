

(*
                                
f(x, y) = g(A(x, y), B(x, y)) + g(C(x, y), D(x, y))

The fully specified way to schedule f/g is to put it inside some maps and lets - i.e. wrap it up in imperative code.

Any externally imposed schedule is a recipe to help us do this.

How can we specify such a schedule more tersely from the outside while losing a minimum of generality?

*)

(* Every (transitive) function has a schedule. Every callsite has a call_schedule. *)

(* How can we schedule a single function - i.e. what kind of Maps do we insert. The following options create a perfectly nested set of maps. You get a list of them, where each dimension must be handled. *) 
type schedule = 
  | Split of dimension * int * dimension * dimension (* dimension, tile size, names of dimensions introduced *)

  | Serial of dimension * int * int (* Serialize across a dimension between the specified bounds *)
  | Vectorized of dimension 
  | Parallel of dimension * int * int 

(* How should a sub-function be called - i.e. what lets do we introduce? A sufficient representation is (with reference to the schedule of the caller), to what caller dimension should I hoist this out to, and should I fuse with other calls to the same callee *)
type call_schedule =
  | Block dimension (* of caller *) * bool (* unify all calls within this function? *)
  | Dynamic dimension (* level in callee where dynamic/static transition happens - chunk granularity *)
  | Coiterate dimension (* of caller - always pairs with outermost dimension of callee *) * int (* offset *) * int (* modulus *)



(* Take 
   
   f(x) = g(x-1) + g(x+1)

   schedule for f(x):  
   [Split ("x", 10, "xo", "xi");
   Serial ("xo", 0, 10);
   Serial ("xi", 0, 16)]

   call_schedule for f.g is ("xo", true)

   schedule for f.g(x):
   [Split ("x", 4, "xo", "xi");
   Serial ("xo", -1, 5);
   Vectorize "xi"]
   
   This creates imperative code:

   Map "f_xo" from 0 to 10
     Let G[24]:
     Map "g_xo" from -1 to 5
       G[g_xo .. g_xo+4] = g(g_xo ... g_xo+4)

     Map "f_xi" from 0 to 16
       f(f_xo*10 + f_xi) = G[f_xo*10 + f_xi-1] + G[f_xo*10 + f_xi+1]

   

   

