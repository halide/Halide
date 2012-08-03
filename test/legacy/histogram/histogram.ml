open Lower
open Ir
open Schedule

let _ =
  let x = Var (i32, "x")
  and y = Var (i32, "y")
  and c = Var (i32, "c") 
  and i = Var (i32, "i")
  in

  let w  = Var (i32, ".width")
  and h  = Var (i32, ".height")
  and ch = Var (i32, ".channels") in

  let addr_expr = (h *~ w *~ c) +~ (w *~ y) +~ x in

  let load_addr = Var (i32, "load_addr") in

  let im = (
    "im",
    [(i32, "x"); (i32, "y"); (i32, "c")],
    i32,
    Pure (
      Let (
        "load_addr",
        addr_expr,
        Cast(i32, Load (f32, ".input", load_addr) *~ (FloatImm 256.0))
      )
    )
  ) in

  let call_im = Call (i32, "im", [x; y; c]) in

  let im_call_sched = (Inline) in
  let im_sched = [] in

  let hist = (
    "hist",
    [(i32, "i")],
    i32,
    Impure (
      (* Initial value - some expr in function arguments *)
      IntImm 0, 
      (* arg value to modify *)
      [call_im],
      (* Modified value *)
      call_im +~ (IntImm 1))
  ) in
  
  let hist_call_sched = Root in
  let hist_sched = [Serial ("i", IntImm 0, IntImm 256);
                    Serial ("x", IntImm 0, w);
                    Serial ("y", IntImm 0, h);
                    Serial ("c", IntImm 0, ch)
                   ] in
    
  let env = Environment.empty in
  let env = Environment.add "im" im env in
  let env = Environment.add "hist" hist env in
  
  let sched = empty_schedule in
  let sched = set_schedule sched "hist.im" im_call_sched im_sched in
  let sched = set_schedule sched "hist" hist_call_sched hist_sched in

  print_schedule sched;

  let lowered_body = lower_function "hist" env sched false in
  let lowered_body = Break_false_dependence.break_false_dependence_stmt lowered_body in
  let lowered_body = Constant_fold.constant_fold_stmt lowered_body in
  let lowered_body = Loop_lifting.loop_lifting lowered_body in

  Printf.printf "\n\nLowered to:\n%s\n" (Ir_printer.string_of_stmt lowered_body);

  let entry = (
    [Buffer ".result"; Buffer ".input";
     Scalar (".width", i32); Scalar (".height", i32); Scalar (".channels", i32)],
    lowered_body
  ) in
  
  Cg_llvm.codegen_to_file "histogram.bc" entry Architecture.host;
