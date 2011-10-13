open Schedule
open Printf
open Ir

let _ = 
  let s = empty_schedule in
  let s = set_schedule s ["f"] Root [Serial ("fx", IntImm 0, IntImm 16); Split ("fy", 2, "fyo", "fyi"); Serial ("fyo", IntImm 0, IntImm 5); Serial ("fyi", IntImm 0, IntImm 2)] in
  let s = set_schedule s ["f"; "g"] (Chunk ("fx", true)) [Serial ("gx", IntImm 0, IntImm 16)] in 
  let s = set_schedule s ["a"; "b"; "c"] (Inline) [Serial ("cx", IntImm 0, IntImm 1)] in
  let s = set_schedule s ["a"] (Inline) [Serial ("ax", IntImm 0, IntImm 1)] in
  let s = set_schedule s ["a"; "b"] (Inline) [Serial ("bx", IntImm 0, IntImm 1)] in
  
  print_schedule s;

  let (_, f_sched) = find_schedule s "f" in
  let f_args = ["fx"; "fy"] in
    printf "\n%s\n" (String.concat "\n"
      (List.map Ir_printer.string_of_expr (stride_list f_sched f_args)))
