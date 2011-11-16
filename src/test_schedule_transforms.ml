open Lower
open Ir
open Schedule
open Schedule_transforms

let _ =
  let x = Var (i32, "x") in
  let one = IntImm 1 in
  let two = IntImm 2 in
  

  let f = ("f", [(i32, "x")], f32, Pure ((Load (f32, ".input", x)) *~ (FloatImm 2.7))) in

  let g = ("g", [(i32, "x")], f32, Pure ((Call (f32, "f", [x +~ one])) +~ (Call (f32, "f", [x -~ one])))) in
  
  let env = Environment.empty in
  let env = Environment.add "f" f env in
  let env = Environment.add "g" g env in
  
  let sched = make_default_schedule "g" env [("x", IntImm 0, IntImm 150)] in

  let callg = Call(f32, "g", [x]) in

  let lowered = lower_function "g" env sched true in
  let lowered = Break_false_dependence.break_false_dependence_stmt lowered in
  let lowered = Constant_fold.constant_fold_stmt lowered in

  Printf.printf "\n\nLowered to:\n%s\n" (Ir_printer.string_of_stmt lowered);
  
  Cg_llvm.codegen_to_file
    "test_schedule_transforms.bc"
    ([Buffer ".input"; Buffer ".result"], lowered)
    Architecture.host
