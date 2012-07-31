open Ir
open Util

exception UnsupportedType of val_type
exception CGFailed of string
exception BCWriteFailed of string

type buffer_field =
  | HostPtr
  | DevPtr
  | HostDirty
  | DevDirty
  | Dim of int
  | ElemSize

let string_of_buffer_field = function
  | HostPtr -> "host"
  | DevPtr -> "dev"
  | HostDirty -> "host_dirty"
  | DevDirty -> "dev_dirty"
  | Dim dim -> "dim." ^ (string_of_int dim)
  | ElemSize -> "elem_size"

let buffer_field_offset = function
  | HostPtr ->   [ 0 ]
  | DevPtr ->    [ 1 ]
  | HostDirty -> [ 2 ]
  | DevDirty ->  [ 3 ]
  | Dim dim ->   [ 4; dim ]
  | ElemSize ->  [ 5 ]

(* map an Ir.arg to an ordered list of names for its constituent Var parts *)
let names_of_arg_vars = function
  | Scalar (n, _) -> [n]
  | Buffer n -> [n; n ^ ".dim.0"; n ^ ".dim.1"; n ^ ".dim.2"; n ^ ".dim.3"]

let arg_var_names arglist = List.flatten (List.map names_of_arg_vars arglist)

(* codegen_c_header entry filename -> () *)
let codegen_c_header e header_file =
  let (object_name, args, _) = e in

  (* Produce a header *)
  let string_of_type = function
    | Int bits -> "int" ^ (string_of_int bits) ^ "_t"
    | UInt bits -> "uint" ^ (string_of_int bits) ^ "_t"
    | Float 32 -> "float"
    | Float 64 -> "double"
    | _ -> failwith "Bad type for toplevel argument"
  in
  let string_of_arg = function
    | Scalar (n, t) -> (string_of_type t) ^ " " ^ (String.sub n 1 ((String.length n)-1))
    | Buffer n -> "buffer_t *" ^ (String.sub n 1 ((String.length n)-1))
  in
  let arg_string = String.concat ", " (List.map string_of_arg args) in
  let lines = 
    ["#ifndef " ^ object_name ^ "_h";
     "#define " ^ object_name ^ "_h";
     "";
     "#ifndef buffer_t_defined";
     "#define buffer_t_defined";
     "#include <stdint.h>";
     "#include <stdlib.h>";
     "#include <stdio.h>";
     "#include <math.h>";
     "#include <stddef.h>";
     "#include <stdbool.h>";
     "";
     "typedef struct buffer_t {";
     "  uint8_t* host;";
     "  uint64_t dev;";
     "  bool host_dirty;";
     "  bool dev_dirty;";
     "  size_t dims[4];";
     "  size_t elem_size;";
     "} buffer_t;";
     "#endif";
     "";
     "#ifdef __cplusplus";
     "extern \"C\" {";
     "#endif";
     "";
     "void " ^ object_name ^ "(" ^ arg_string ^ ");";
     "";
     "#ifdef __cplusplus";
     "}";
     "#endif";
     "";
     "#endif"]
  in

  let out = open_out header_file in
  output_string out (String.concat "\n" lines);
  close_out out
