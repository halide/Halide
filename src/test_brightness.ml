open Lower
open Ir
open Schedule

let _ =
  let x = Var (i32, "x")
  and y = Var (i32, "y")
  and c = Var (i32, "c") in

  let w  = Var (i32, ".width")
  and h  = Var (i32, ".height")
  and ch = Var (i32, ".channels") in

  let addr_expr = (h *~ w *~ c) +~ (w *~ y) +~ x in

  let load_addr = Var (i32, "load_addr")
  and store_addr = Var (i32, "store_addr") in

  let coeff = Var (f32, ".coefficient") in

  let im = (
    "im",
    [(i32, "x"); (i32, "y"); (i32, "c")],
    f32,
    Pure (
      Let (
        "load_addr",
        addr_expr,
        Debug (
          Load (f32, ".input", load_addr),
          "Load input: ",
          [x; y; c; load_addr]
        )
      )
    )
  ) in

  let f_call_sched = (Inline) in
  let f_sched = [] in
  (*
  let f_sched = [Split ("x", "xo", "xi", ((Var (i32, "g.xo")) *~ (IntImm 4)) -~ one);
                 Vectorized ("xi", IntImm 0, 4);
                 Unrolled ("xo", IntImm 0, 2)
                ] in 
  *)

  let brighten = (
    "brighten",
    [(i32, "x"); (i32, "y"); (i32, "c")],
    f32,
    Pure (
      (Call (f32, "im", [x; y; c])) *~ coeff
    )
  ) in
  
  let g_call_sched = Root in
  (*let g_sched = [Split ("x", "xo", "xi", IntImm 0); Vectorized ("xi", IntImm 0, 4); Parallel ("xo", IntImm 0, IntImm 25)] in*)
  let g_sched = [Serial ("x", IntImm 0, w); Serial ("y", IntImm 0, h); Serial ("c", IntImm 0, ch)] in
    
  let env = Environment.empty in
  let env = Environment.add "im" im env in
  let env = Environment.add "brighten" brighten env in
  
  let sched = empty_schedule in
  let sched = set_schedule sched "brighten.im" f_call_sched f_sched in
  let sched = set_schedule sched "brighten" g_call_sched g_sched in

  print_schedule sched;

  let lowered_body = lower_function "brighten" env sched in
  let lowered_body = Break_false_dependence.break_false_dependence_stmt lowered_body in
  let lowered_body = Constant_fold.constant_fold_stmt lowered_body in

  Printf.printf "\n\nLowered to:\n%s\n" (Ir_printer.string_of_stmt lowered_body);

  let entry = (
    [Buffer ".result"; Buffer ".input";
     Scalar (".width", i32); Scalar (".height", i32); Scalar (".channels", i32);
     Scalar (".coefficient", f32)],
    lowered_body
  ) in
  
  Cg_llvm.codegen_to_file "test_brightness.bc" entry
