open Schedule
open Printf
open Ir

let _ = 
  let s = empty_schedule in
  let s = set_schedule s "f" Root [Serial ("fx", IntImm 0, IntImm 16); Split ("fy", "fyo", "fyi", IntImm 0); Serial ("fyo", IntImm 0, IntImm 5); Serial ("fyi", IntImm 0, IntImm 2)] in
  let s = set_schedule s "f.g" (Chunk "fx") [Serial ("gx", IntImm 0, IntImm 16)] in 
  let s = set_schedule s "a.b.c" (Inline) [Serial ("cx", IntImm 0, IntImm 1)] in
  let s = set_schedule s "a" (Inline) [Serial ("ax", IntImm 0, IntImm 1)] in
  let s = set_schedule s "a.b" (Inline) [Serial ("bx", IntImm 0, IntImm 1)] in
  let s = set_schedule s "a.b.c.g" (Inline) [] in
  
  print_schedule s;


  List.iter (fun x -> Printf.printf "%s\n" x) (find_all_schedule s "g")


