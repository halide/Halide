open Schedule
open Printf
open Ir

let _ = 
  let s = empty_schedule in
  let s = set_schedule s ["f"] Root [Serial ("fx", IntImm 0, IntImm 16); Serial ("fy", IntImm 0, IntImm 10)] in
  let s = set_schedule s ["f"; "g"] (Chunk ("fx", true)) [Serial ("gx", IntImm 0, IntImm 16)] in 
  let s = set_schedule s ["a"; "b"; "c"] (Inline) [Serial ("cx", IntImm 0, IntImm 1)] in
  let s = set_schedule s ["a"] (Inline) [Serial ("ax", IntImm 0, IntImm 1)] in
  let s = set_schedule s ["a"; "b"] (Inline) [Serial ("bx", IntImm 0, IntImm 1)] in
  
  print_schedule s
    
