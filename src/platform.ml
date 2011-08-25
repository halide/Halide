open Ir
open Analysis

let vector_alignment = 16*8

let alignment x t = match t with
  | Int(_) | UInt(_) | Float(_) -> 0
  | IntVector(bits, n) | UIntVector(bits, n) | FloatVector(bits, n) ->      
    reduce_expr_modulo x (vector_alignment/(bits))
