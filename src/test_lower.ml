open Lower
open Ir
open Schedule

let _ =
  let x = Var (i32, "x") in
  let one = IntImm 1 in
  let two = IntImm 2 in
  

  let f = ("f", [(i32, "x")], f32, Pure ((Load (f32, ".input", x)) *~ (FloatImm 2.7))) in

  let f_call_sched = (Chunk "g.xi") in
  (*let f_sched = [Serial ("x", ((Var (i32, "g.xo")) *~ (IntImm 4)) -~ one, IntImm 6)] in *)
  (* let f_sched = [Split ("x", "xo", "xi", (Var (i32, "g.xo") *~ (IntImm 4)) -~ (IntImm 1));
                 Vectorized ("xi", IntImm 0, 4); Unrolled ("xo", IntImm 0, 2)] in  *)

  let f_sched = [Split ("x", "xo", "xi", ((Var (i32, "g.xo")) *~ (IntImm 4)) -~ one);
                 Vectorized ("xi", IntImm 0, 4);
                 Unrolled ("xo", IntImm 0, 2)
                ] in 

  let g = ("g", [(i32, "x")], f32, Pure ((Call (f32, "f", [x +~ one])) +~ (Call (f32, "f", [x -~ one])))) in
  
  let g_call_sched = Root in
  let g_sched = [Split ("x", "xo", "xi", IntImm 0); Vectorized ("xi", IntImm 0, 4); Parallel ("xo", IntImm 0, IntImm 25)] in

  let env = Environment.empty in
  let env = Environment.add "f" f env in
  let env = Environment.add "g" g env in
  
  let sched = empty_schedule in
  let sched = set_schedule sched "g.f" f_call_sched f_sched in
  let sched = set_schedule sched "g" g_call_sched g_sched in

  print_schedule sched;
  ignore (find_schedule sched "g.f");
  
  let callf = Call(f32, "g", [x]) in
  let stmt = For ("x", IntImm 0, IntImm 100, false, Store (callf, ".result", x)) in

  let lowered = lower stmt env sched in
  let lowered = Break_false_dependence.break_false_dependence_stmt lowered in
  let lowered = Constant_fold.constant_fold_stmt lowered in


  Printf.printf "\n\nLowered to:\n%s\n" (Ir_printer.string_of_stmt lowered);
  
  Cg_llvm.codegen_to_file "test_lower.bc" ([Buffer ".input"; Buffer "result"], lowered)
    
    
