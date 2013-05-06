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

val unsigned: ty -> ty

val sizeof: ty -> int       (* not negative *)

val strip: ty -> ty

val variadic: function_type -> bool

val pointable: ty -> bool

val int:    ty     
val char:   ty
val string: ty

val include_headers: program -> program
