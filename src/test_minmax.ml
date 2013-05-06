open Lower
open Ir
open Schedule

let _ =
  let x = Var (i32, "x") in
  let one = IntImm 1 in
  let three = IntImm 3 in
  
  let imax = ("imax", [(i32, "a"); (i32, "b")], i32, Pure (Bop(Max, Var(i32, "a"), Var(i32, "b")))) in

  let imax_call_sched = Inline in
  let imax_sched = [Parallel ("a", (IntImm (-2)), (IntImm 22))] in

  let c x = Call (f32, "imax", [x +~ three; x *~ three]) in

  (* let g = ("g", [(i32, "x")], i32, Pure ((c (x +~ one)) +~ (c (x -~ one)))) in *)
  let g = ("g", [(i32, "x")], i32, Pure ((c x))) in
  
  let g_call_sched = Root in
  (* let g_sched = [Split ("x", "gxo", "gxi", IntImm 0); Vectorized ("gxi", IntImm 0, 4); Parallel ("gxo", IntImm 0, IntImm 25)] in *)
  let g_sched = [Parallel ("x", IntImm 0, IntImm 100)] in
  
  let env = Environment.empty in
  let env = Environment.add "imax" imax env in
  let env = Environment.add "g" g env in
  
  let sched = empty_schedule in
  let sched = set_schedule sched "g" g_call_sched g_sched in  
  let sched = set_schedule sched "g.imax" imax_call_sched imax_sched in  

  print_schedule sched;

  let lowered = lower_function "g" env sched false in
  let lowered = Break_false_dependence.break_false_dependence_stmt lowered in
  let lowered = Constant_fold.constant_fold_stmt lowered in

  Printf.printf "\n\nLowered\n%s\nto:\n%s\n"
    (Ir_printer.string_of_environment env)
    (Ir_printer.string_of_stmt lowered);
  
  let module Cg = Cg_llvm.CodegenForHost in
  Cg.codegen_to_file
    ("g", [Buffer ".result"], lowered)
    "test_minmax.bc"
