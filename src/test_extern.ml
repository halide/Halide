open Lower
open Ir
open Schedule

let _ =
  let x = Var (i32, "x") in
  let one = IntImm 1 in
  let two = IntImm 2 in
  
  let cos = (".llvm.cos.f32", [(f32, "x")], f32, Extern) in

  let c x = Call (f32, ".llvm.cos.f32", [Cast(f32, x)]) in

  let g = ("g", [(i32, "x")], f32, Pure ((c (x +~ one)) +~ (c (x -~ one)))) in
  
  let g_call_sched = Root in
  (* TODO: vectorized schedule doesn't work yet for extern calls *)
  (* let g_sched = [Split ("x", "gxo", "gxi", IntImm 0); Vectorized ("gxi", IntImm 0, 4); Parallel ("gxo", IntImm 0, IntImm 25)] in *)
  let g_sched = [Parallel ("x", IntImm 0, IntImm 100)] in
  
  let env = Environment.empty in
  let env = Environment.add "llvm.cos.f32" cos env in
  let env = Environment.add "g" g env in
  
  let sched = empty_schedule in
  let sched = set_schedule sched "g" g_call_sched g_sched in  

  print_schedule sched;

  let lowered = lower_function "g" env sched false in
  let lowered = Break_false_dependence.break_false_dependence_stmt lowered in
  let lowered = Constant_fold.constant_fold_stmt lowered in

  Printf.printf "\n\nLowered\n%s\nto:\n%s\n"
    (Ir_printer.string_of_environment env)
    (Ir_printer.string_of_stmt lowered);
  
  Cg_llvm.codegen_to_file
    "test_extern.bc"
    ("g", [Buffer ".input"; Buffer ".result"], lowered)
    Architecture.host
