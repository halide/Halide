open Lower
open Ir
open Schedule

let _ =
  let x = Var (i32, "x") in
  let one = IntImm 1 in
  let two = IntImm 2 in
  

  let f = ("f", [("x", i32)], i32, Pure (x *~ two)) in

  let f_call_sched = (Chunk ("x", true)) in
  let f_sched = [Serial ("x", IntImm 0, IntImm 100)] in

  let g = ("g", [("x", i32)], i32, Pure ((Call ("f", i32, [x +~ one])) +~ (Call ("f", i32, [x -~ one])))) in
  
  let g_call_sched = Root in
  let g_sched = [Serial ("x", IntImm 0, IntImm 100)] in

  let env = Environment.empty in
  let env = Environment.add "f" f env in
  let env = Environment.add "g" g env in
  
  let sched = empty_schedule in
  let sched = set_schedule sched ["g.f"] f_call_sched f_sched in
  let sched = set_schedule sched ["g"] g_call_sched g_sched in
  
  let callf = Call("g", i32, [x]) in
  let stmt = Map ("x", IntImm 0, IntImm 100, Store (callf, "result", x)) in

  lower stmt env sched
    
    
