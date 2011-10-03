

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

  | Serial of dimension * expr * expr (* Serialize across a dimension between the specified bounds *)
  | Vectorized of dimension 
  | Parallel of dimension * expr * expr

(* How should a sub-function be called - i.e. what lets do we introduce? A sufficient representation is (with reference to the schedule of the caller), to what caller dimension should I hoist this out to, and should I fuse with other calls to the same callee *)
type call_schedule =
  | Block dimension (* of caller *) * bool (* unify all calls within this function? *)
  | Dynamic dimension (* level in callee where dynamic/static transition happens - chunk granularity *)
  | Coiterate dimension (* of caller - always pairs with outermost dimension of callee *) * int (* offset *) * int (* modulus *)
  | Inline (* block over nothing - just do in place *)
  | Root (* There is no calling context *)


module StringMap = Map.Make(String)

(* Schedule for this function, schedule for sub-functions *)
type schedule_tree = (call_schedule * schedule list) * (schedule_tree StringMap.t) 

let find_schedule (tree:schedule_tree) (call:string list) =
  let sched, map = tree in
  match call with
    | [] -> sched
    | (first::rest) -> 
      find_schedule (StringMap.find first map) rest        

(* For each function, give the list of functions it depends on, and what region of that function *)
type dependence_map = ((string * ((expr * expr) list)) list) StringMap.t

(* Take 
   
   g(x) = x*2
   f(x) = g(x-1) + g(x+1)

   schedule for f(fx):  
   [Split ("fx", 16, "fxo", "fxi");
   Serial ("fxi"); --> 0,16
   Serial ("fxo")] --> 0,10 - implied by realize f [0,100] and split by 16

   call_schedule for f.g is Block ("fxi", true)
   schedule for f.g(gx) 
   [Serial ("gx")]

   Realize f

   -> Initial statement
   result[fx] = f(fx)

   -> Initialize wrapper code using schedule for f

   Map "fxo" from 0 to 10
     Map "fxi" from 0 to 16       
       result[fxo*16 + fxi] = f(fxo*16 + fxi)

   -> Subs f function body
   
   Map "fxo" from 0 to 10
     Map "fxi" from 0 to 16   
       Let f_result[1]
         f_result[0] = g(fxo*16 + fxi - 1) + g(fxo*16 + fxi + 1) 
         result[fxo*16 + fxi] = f_result[0]

   -> Look for calls (two calls to g). Precompute accoring to call schedule

   Map "fxo" from 0 to 10
     Let G[fxo*16+17 - fxo*16-1]
       Map "gx" from fxo*16-1 to fxo*16+17
         G[gx-fxo*16-1] = g(gx)
       Map "fxi" from 0 to 16
         Let f_result[1] 
           f_result[0] = G[fxo*16 + fxi - 1 - (fxo*16-1)] + G[fxo*16 + fxi + 1 - (fxo*16+1)]
           result[fxo*16 + fxi] = f_result[0]

   -> Subs g function body
   
   Map "fxo" from 0 to 10
     Let G[fxo*16+17 - fxo*16-1]
       Map "gx" from fxo*16-1 to fxo*16+17
         Let g_result[1] 
           g_result[0] = gx*2  
           G[gx-fxo*16-1] = g_result[0]
       Map "fxi" from 0 to 16
         Let f_result[1] 
           f_result[0] = G[fxo*16 + fxi - 1 - (fxo*16-1)] + G[fxo*16 + fxi + 1 - (fxo*16+1)]
           result[fxo*16 + fxi] = f_result[0]

   -> Look for calls (none). Done.

   Another example (replace every pixel with how often it occurs)

   hist(im[x, y])++ (* Assume implemented as imperative blob for now *)
   f(x, y) = hist(im[x, y])

   schedule for f(fx, fy):
   [Serial ("fy", 0, 100); Serial ("fx", 0, 100)]

   call schedule for hist: (Block ("fy", true))

   schedule for f.hist: [Serial ("i", 0, 256)]

   -> Initialize wrapper code using schedule for f

   Map "fy" from 0 to 100
     Map "fx" from 0 to 100
       result[fy*100+fx] = f(x, y)

   -> Subs f function body
   Map "fy" from 0 to 100
     Map "fx" from 0 to 100
       Let f_result[1]
         f_result[0] = hist(im[fx, fy])
         result[fy*100+fx] = f_result[0]

   -> Look for calls (hist), and lift according to call schedule

   Let HIST[256]
     Map "i" from 0 to 256
       HIST[i] = hist(i)
     Map "fy" from 0 to 100
       Map "fx" from 0 to 100
         Let f_result[1]
           f_result[0] = HIST[im[fx, fy]]
           result[fy*100+fx] = f_result[0]
   
   -> Subs hist function body
   
   Let HIST[256]
     Map "i" from 0 to 256
       Let hist_result[1]
         Let inner_result[256]
           Map "hx" from 0 to 100
             Map "hy" from 0 to 100
               inner_result[im[hx, hy]]++
           hist_result[0] = inner_result[i]
         HIST[i] = hist_result[0]
     Map "fy" from 0 to 100
       Map "fx" from 0 to 100
         Let f_result[1]
           f_result[0] = HIST[im[fx, fy]]
           result[fy*100+fx] = f_result[0]
   
   -> Lift invariant pipeline roots outside maps
   
   Let HIST[256]
     Let inner_result[256]
       Map "hx" from 0 to 100
         Map "hy" from 0 to 100
           inner_result[im[hx, hy]]++
       Map "i" from 0 to 256
         Let hist_result[1]
           hist_result[0] = inner_result[i]
         HIST[i] = hist_result[0]
     Map "fy" from 0 to 100
       Map "fx" from 0 to 100
         Let f_result[1]
           f_result[0] = HIST[im[fx, fy]]
           result[fy*100+fx] = f_result[0]

   -> Remove trivial scalar lets (assigned once, used once)

   Let HIST[256]
     Let inner_result[256]
       Map "hx" from 0 to 100
         Map "hy" from 0 to 100
           inner_result[im[hx, hy]]++
       Map "i" from 0 to 256
         HIST[i] = inner_result[i]
     Map "fy" from 0 to 100
       Map "fx" from 0 to 100
         result[fy*100+fx] = HIST[im[fx, fy]]

   -> Remove identity pipeline stages
   
   Let inner_result[256]
     Map "hx" from 0 to 100
       Map "hy" from 0 to 100
         inner_result[im[hx, hy]]++
     Map "fy" from 0 to 100
       Map "fx" from 0 to 100
         result[fy*100+fx] = inner_result[im[fx, fy]]   

*)
   

