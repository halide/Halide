open Lower
open Ir
open Schedule

(* 
For now, SIMT doesn't support:

- anything that generates an alloca

And has bugs which require manual patches of the output LLVM bitcode:

- main entrypoint should have calling convention qualifier "ptx_kernel void @_im_main"
- other functions must have convention "ptx_device"
- PTX thread ID intrinsics get codegenerated with their dotted name components mangled. Replace e.g. @x with @llvm.ptx.read.tid.x()
- _im_main_runner trampoline should be dropped (no support for device function calls)

All that said, this actually generates PTX once patched with:

    llc -O0 -march=ptx32 -o=/dev/stdout < test_simt.ll > test_simt.ptx

and compiles with:

    ptxas -v test_simt.ptx -o test_simt.o               # -v prints interesting info
    cuobjdump --dump-sass test_simt.o > test_simt.sass  # print out actual SASS machine code
*)

let _ =
  let x = Var (i32, "x") in
  let one = IntImm 1 in
  let two = IntImm 2 in
  
  let f = ("f", [(i32, "x")], f32, Pure ((Load (f32, ".input", x)) *~ (FloatImm 2.7))) in

  (* let f_call_sched = (Chunk "gxi") in *)
  let f_call_sched = Inline in
  (* let f_sched = [Vectorized ("x", ((Var (i32, "g.xo")) *~ (IntImm 4)) -~ two, 8)] in *)
  (* let f_sched = [Split ("x", "xo", "xi", (Var (i32, "g.xo") *~ (IntImm 4)) -~ (IntImm 1));
                 Vectorized ("xi", IntImm 0, 4); Unrolled ("xo", IntImm 0, 2)] in  *)

  let f_sched = [Split ("x", "fxo", "fxi", ((Var (i32, "threadidx")) *~ (IntImm 4)) -~ one);
                 Serial ("fxi", IntImm 0, IntImm 4);
                 Unrolled ("fxo", IntImm 0, 2)
                ] in 

  let g = ("g", [(i32, "x")], f32, Pure ((Call (f32, "f", [x +~ one])) +~ (Call (f32, "f", [x -~ one])))) in
  
  let g_call_sched = Root in
  let g_sched = [Split ("x", "threadidx", "gxi", IntImm 0); Unrolled ("gxi", IntImm 0, 4); Parallel ("threadidx", IntImm 0, IntImm 25)] in
    
  let env = Environment.empty in
  let env = Environment.add "f" f env in
  let env = Environment.add "g" g env in
  
  let sched = empty_schedule in
  let sched = set_schedule sched "g.f" f_call_sched f_sched in
  let sched = set_schedule sched "g" g_call_sched g_sched in  

  print_schedule sched;

  let lowered = lower_function "g" env sched false in
  let lowered = Break_false_dependence.break_false_dependence_stmt lowered in
  let lowered = Constant_fold.constant_fold_stmt lowered in
  let lowered = Simtfy.simtfy_stmt lowered in

  Printf.printf "\n\nLowered\n%s\nto:\n%s\n"
    (Ir_printer.string_of_environment env)
    (Ir_printer.string_of_stmt lowered);
  
  Cg_llvm.codegen_to_file "test_simt.bc" ([Buffer ".input"; Buffer ".result"], lowered)
    
    
