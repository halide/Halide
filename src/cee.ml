(* Based on modules from https://code.google.com/p/quest-tester/

Copyright (c) 2004, 2005 Christian Lindig <lindig@eecs.harvard.edu>. All
rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the following
disclaimer in the documentation and/or other materials provided
with the distribution.

THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR AND COPYRIGHT HOLDER BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
SUCH DAMAGE.
*)

type size           = int
type name           = string
type signed         = Signed | Unsigned
type iwidth         = Char | Short  | IInt | Long | LongLong
type fwidth         = FFloat  | Double | LongDouble

type function_type =
  { return:       ty
  ; args:         (name * ty) list
  ; varargs:      ty list
  }

and ty =
  | Void
  | Bool
  | Int           of iwidth * signed
  | Float         of fwidth 
  | Ptr           of ty
  | Array         of ty * size
  | Struct        of name * (name * ty) list option
  | Union         of name * (name * ty) list option
  | Fun           of function_type
  | TyName        of string
  | Bitfield      of int * signed

type infix   = Eq | Neq | GT | GE | LT | LE | Mult | Div | Mod | Add | Sub | LOr | LAnd     (* many more missing *)
type prefix  = Not | Neg                (* many more missing *)
type postfix = PostDec | PostInc        (* many more missing *)

type expr = 
  | ID            of string
  | IntConst      of int
  | Const         of string (* char, int, float, string *)
  | Call          of expr * expr list
  | Assign        of expr * expr
  | Access        of expr * expr  (* array , index *) 
  | Select        of expr * name  (* struct or union, member *)
  | Arrow         of expr * name
  | Deref         of expr 
  | AddrOf        of expr
  | Ternary       of expr * expr * expr (* a ? b : c *)
  | Infix         of expr * infix * expr
  | Prefix        of prefix * expr
  | Postfix       of expr * postfix
  | Cast          of ty   * expr
  | Type          of ty 

type init = 
  | SingleInit    of expr
  | CompoundInit  of init list

type decl = 
  | VarDecl of name * ty * init option
  | Typedef of name * ty

type stmt =
  | Expr          of expr
  | Block         of decl list * stmt list
  | Return        of expr option
  | IfThen        of expr * stmt
  | IfThenElse    of expr * stmt * stmt
  | For           of decl * expr * expr * stmt
                  (* decl, condition, update, body *)
  | Comment       of string
  | Nop
  (* incomplete *)

type fundef =
  { name:     name                    (* name of this function *)
  ; static:   bool
  ; ty:       function_type
  ; decls:    decl list               (* variable declarations in body *)
  ; body:     stmt list               (* statements in body *)
  }

type scope    = Static | Extern | Public
type toplevel =
  | TopDecl   of scope * decl
  | Function  of fundef
  | CPP       of string   (* C Preprocessor statement *)

type program = toplevel list

let rec sizeof = function
  | Void              -> 0
  | Int(Char,_)       -> 1
  | Int(Short,_)      -> 2
  | Int(IInt,_)       -> 4
  | Int(Long,_)       -> 8
  | Int(LongLong,_)   -> 16
  | Bitfield(_,_)     -> 1
  | Float(FFloat)     -> 4
  | Float(Double)     -> 8
  | Float(LongDouble) -> 16
  | Ptr(_)            -> 4
  | Array(t,n)        -> n * sizeof t
  | Struct(_,None)    -> failwith "sizeof: struct reference"
  | Struct(_,Some ms) -> List.fold_left 
                          (fun s (n,t) -> s+(sizeof t)) 0 ms
  | Union(_,None)     -> failwith "sizeof: union rerefence"
  | Union(_,Some ms)  -> List.fold_left 
                          (fun s (n,t) -> max s (sizeof t)) 0 ms
  | Fun(_)            -> 4                        
  | TyName _          -> failwith "sizeof: named type"
  | Bool              -> failwith "sizeof: Bool not implemented"

let rec strip = function
  | Struct(n,Some ms) ->    Struct(n, None)
  | Union (n,Some ms) ->    Union (n, None)
  | Ptr(t)            ->    Ptr(strip t)
  | Array(t,n)        ->    Array(strip t,n)
  | Fun(f)            ->    Fun(strip_function_type f)
  | x                 ->    x  

and strip_function_type fty =
  { return  = strip fty.return
  ; args    = List.map (function name, ty -> name, strip ty) fty.args
  ; varargs = List.map strip fty.varargs
  }

let simple = function
  | Void      -> failwith "applied Cee.simple to Void"
  | Int(_)    -> true
  | Float(_)  -> true
  | _         -> false (* we consider pointers as complex types *)

let pointable = function
  | Array(_,_)    -> false
  | Fun(_)        -> false 
  | _             -> true

let variadic fty = fty.varargs <> []

let int     = Int(IInt, Signed)
let char    = Int(Char, Signed)
let string  = Ptr(char)

let unsigned = function
  | Int(t,_)  -> Int(t,Unsigned)
  | _         -> failwith "Cee.unsigned applied to non-integer type"

type header =
  { stdarg: bool
  }
  
let headers prg = 
  let scan header = function
    | Function({ty = {varargs=_::_}}) -> { header with stdarg = true }
    | _ -> header
  in
    List.fold_left scan { stdarg = false } prg

let include_headers prg =
  let h   = headers prg in
  let prg = if h.stdarg then CPP("#include <stdarg.h>") :: prg else prg in
    prg
