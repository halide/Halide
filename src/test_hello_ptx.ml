open Lower
open Ir
open Schedule

(*
  Compile to PTX with:
  
    llc -O0 -march=ptx64 test_hello_ptx.bc -o=test_hello_ptx.ptx
*)

let _ =
  (* let x = Var (i32, "x") in
  let x = Cast(f32, x) in *)
  let x = FloatImm 1.234 in
  let f = ("f", [(i32, "x")], f32, Pure (x)) in

  (* All PTX kernels *have* to be split over both thread ID and block ID to be
   * meaningful. This seems to suggest using utilities to create this split,
   * rather than doing it directly. *)
  let block_size = 256 in
  
  let f_call_sched = Root in
  let f_sched = [
    Split ("x", "blockidx", "threadidx", IntImm 0);
    Parallel ("threadidx", IntImm 0, IntImm block_size);
    Parallel ("blockidx", IntImm 0, (Var (i32, ".N")) /~ (IntImm block_size));
  ] in

  let env = Environment.empty in
  let env = Environment.add "f" f env in

  let sched = empty_schedule in
  let sched = set_schedule sched "f" f_call_sched f_sched in

  print_schedule sched;

  let lowered = lower_function "f" env sched false in
  let lowered = Break_false_dependence.break_false_dependence_stmt lowered in
  let lowered = Constant_fold.constant_fold_stmt lowered in
  (* let lowered = Simtfy.simtfy_stmt lowered in *)

  Printf.printf "\n\nLowered\n%s\nto:\n%s\n"
    (Ir_printer.string_of_environment env)
    (Ir_printer.string_of_stmt lowered);
  
  let module Cg = Cg_llvm.CodegenForArch(Ptx) in
  Cg.codegen_to_file
    ("f", [Buffer ".input"; Buffer ".result"; Scalar (".N", i32)], lowered)
    "test_hello_ptx.bc"
