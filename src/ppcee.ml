module P = Pretty
module C = Cee

let (@@) f x    = f x
let (<<<) f g x  = f (g x)

let group       = P.group 
let nest        = P.nest 4
let break       = P.break
let empty       = P.empty
let text        = P.text
let (^^)        = P.cat
let (^+) x y    = x ^^ nest (break ^^ y)                  
let (^^^) x y   =      if x = empty then y 
                  else if y = empty then x 
                  else x ^^ break ^^ y


let commabreak      = text "," ^^ break
let semibreak       = text ";" ^^ break
let assign          = text "="
let semi            = text ";"

let rec list sep f xs =
  let rec loop acc = function
    | []    -> acc
    | [x]   -> acc ^^ f x 
    | x::y::xs -> loop (acc ^^ f x ^^ sep) (y::xs)
  in
    loop empty xs 

let rec inter sep f xs =
  let rec loop acc = function
    | []    -> acc
    | x::xs -> loop (acc ^^ f x ^^ sep) xs
  in
    loop empty xs

let bracket  l x r  = group (text l ^^ nest (break ^^ x) ^^ break ^^ text r)
let bracket' l x r  = group (text l ^^ nest x ^^ text r)
let brace    x      = bracket  "{" x "}" 
let brace'   x      = bracket' "{" x "}" 
let parent   x      = bracket' "(" x ")"
let bracket  x      = bracket  "[" x "]"
let bracket' x      = bracket' "[" x "]"

let int_width = function
  | C.Short    -> text "short" 
  | C.Long     -> text "long" 
  | C.LongLong -> text "long long"
  | C.IInt     -> empty
  | C.Char     -> failwith "iwidth: called with Char"

let bit_width n = P.printf ":%d" n

let int_sign = function
  | C.Signed   -> empty
  | C.Unsigned -> text "unsigned" 

let bit_sign = function
  | C.Signed   -> text "signed"
  | C.Unsigned -> text "unsigned" 


let rec declaration name t =
  let rec decl t d = match t with
    | C.Void ->                 text "void" ^^^ d
    | C.TyName(s) ->            text s ^^^ d
    | C.Bitfield(w,s) ->        bit_sign s ^^^ d ^^ bit_width w
    | C.Bool ->                 text "bool" ^^^ d
    | C.Int(C.Char,s) ->        int_sign s ^^^ text "char" ^^^ d
    | C.Int(w,s) ->             int_sign s ^^^ int_width w 
                                           ^^^ text "int" ^^^ d
    | C.Float(C.FFloat) ->      text "float" ^^^ d
    | C.Float(C.Double) ->      text "double" ^^^ d
    | C.Float(C.LongDouble) ->  text "long double" ^^^ d
    | C.Ptr(C.Array(_) as ty)-> decl ty (text "(*" ^^ d ^^ text ")") 
    | C.Ptr(C.Fun(_) as ty) ->  decl ty (text "(*" ^^ d ^^ text ")") 
    | C.Ptr(ty) ->              decl ty (text "*" ^^ d)
    | C.Array(ty,size) ->       decl ty (d ^^ P.printf "[%d]" size)
    | C.Struct(n,None) ->       group (text "struct" ^^^ text n) ^+ d
    | C.Struct(n,Some ms) ->    group (text "struct" ^^^text n) 
                                  ^^^ compound ms 
                                  ^^^ d
    | C.Union(n,None) ->        group (text "union" ^^^ text n) ^+ d
    | C.Union(n,Some ms) ->     group (text "union" ^^^ text n) 
                                  ^^^ compound ms 
                                  ^^^ d
    | C.Fun(fty) ->     
      let ts'  = List.map (fun (name,t) -> ty t) fty.C.args 
        @ if C.variadic fty then [text "..."] else [] in
      let id x = x in
        group (decl fty.C.return d) ^^ parent (list commabreak id ts') 
  in                                      
    group (decl t name)

and compound members =
  let decl doc (name,ty) = doc ^^^ declaration (text name) ty ^^ semi
  in
    brace @@ List.fold_left decl P.empty members

and ty t = declaration P.empty t

let infix = function
  | C.LT        -> "<"
  | C.LE        -> "<="
  | C.GT        -> ">"
  | C.GE        -> ">="
  | C.Eq        -> "=="
  | C.Neq       -> "!="
  | C.Add       -> "+"
  | C.Sub       -> "-"
  | C.Mult      -> "*"
  | C.Div       -> "/"
  | C.Mod       -> "%"
  | C.LAnd      -> "&&"
  | C.LOr       -> "||"
  
let prefix = function
  | C.Not       -> "!"
  | C.Neg       -> "-"

let postfix = function
  | C.PostDec   -> "--"
  | C.PostInc   -> "++"
  
(* C++ associativity/precedence ref:
   http://msdn.microsoft.com/en-us/library/126fe14k.aspx *)
type precedence = int 
type side       = Left | Right
let precedence = function
  | C.ID(_)               -> Left, 16
  | C.IntConst(_)         -> Left, 16
  | C.Const(_)            -> Left, 16
  | C.Call(_,_)           -> Left, 16
  | C.Select(_,_)         -> Left, 16
  | C.Access(_,_)         -> Left, 16
  | C.Arrow(_,_)          -> Left, 16
  | C.Deref(_)            -> Right,15
  | C.AddrOf(_)           -> Right,15
  | C.Prefix(C.Neg,_)
  | C.Prefix(C.Not,_)     -> Right,15
  | C.Postfix(_,C.PostDec)
  | C.Postfix(_,C.PostInc)-> Right,15
  | C.Cast(_,_)           -> Right,14

  | C.Infix(_,C.Mult,_)
  | C.Infix(_,C.Div,_)
  | C.Infix(_,C.Mod,_)    -> Left, 12
  | C.Infix(_,C.Add,_)
  | C.Infix(_,C.Sub,_)    -> Left, 11
  | C.Infix(_,C.LT,_)
  | C.Infix(_,C.LE,_)
  | C.Infix(_,C.GT,_)
  | C.Infix(_,C.GE,_)     -> Left, 10
  | C.Infix(_,C.Eq,_)  
  | C.Infix(_,C.Neq,_)    -> Left,  9
  | C.Infix(_,C.LAnd,_)   -> Left,  5
  | C.Infix(_,C.LOr,_)    -> Left,  4
  | C.Ternary(_,_,_)      -> Right, 3
  | C.Assign(_,_)         -> Right, 2
  (* special cases from here down *)
  | C.Type(_)             -> Right, 1

let noparens (ia,ip) (oa,op) side =
   ip > op
|| ip = op && ia = oa && oa = side

let rec expr e =  
  let rec exp outer side e = 
    let inner = precedence e in
    let doc   = match e with 
      | C.ID x          -> text x
      | C.Const x       -> text x
      | C.IntConst i    -> text (string_of_int i)
      | C.Call(f,args)  -> exp inner Left f
                              ^^ nest (parent @@ exprs args)
      | C.Assign(l,r)   -> exp inner Left l 
                              ^^^ text "=" 
                              ^^^ exp inner Right r
      | C.Ternary(c,t,f)-> exp inner Left c
                              ^^^ text "?" 
                              ^^^ exp inner Left t
                              ^^^ text ":"
                              ^^^ exp inner Right f
      | C.Infix(l,op,r) -> exp inner Left l 
                              ^^^ text (infix op)
                              ^^^ exp inner Right r
      | C.Postfix(e,op) -> exp inner Left e ^^ text (postfix op)
      | C.Cast(t,e)     -> parent (ty @@ C.strip t) ^^^ expr e
      | C.Select(e,n)   -> exp inner Left e ^^ text ("." ^ n)
      | C.Arrow(e,n)    -> exp inner Left e ^^ text ("->" ^ n)
      | C.Access(e,i)   -> exp inner Left e ^^ bracket' (expr i) 
      | C.Deref(e)      -> text "*" ^^ exp inner Right e
      | C.AddrOf(e)     -> text "&" ^^ exp inner Right e
      | C.Type(t)       -> ty (C.strip t)  
      | _               -> failwith "expr: not implemented" 
    in
      if noparens inner outer side 
      then group doc 
      else group @@ parent @@ doc
  in
    exp (Left,0) Left e

and exprs xs = list commabreak expr xs    

let rec init = function
  | C.SingleInit e    -> expr e
  | C.CompoundInit is -> brace' (list commabreak init is) 

let decl d = 
  let doc = function
    | C.VarDecl(name, ty, None) -> declaration (text name) ty
    | C.Typedef(name, ty)       -> text "typedef" ^^^ declaration (text name) ty
    | C.VarDecl(name, ty, Some i) -> 
        declaration (text name) ty ^+ group (assign ^^^ init i)
  in 
    group @@ doc d

let decls dd = group (inter semibreak decl dd)

let rec stmt = function
  | C.Expr e          -> expr e ^^ semi
  | C.Block(dd, ss)   -> brace (decls dd ^^^ stmts ss) 
  | C.Return(Some e)  -> text "return" ^^^ expr e ^^ semi
  | C.Return(None)    -> text "return" ^^ semi
  | C.IfThen(e,s)     -> text "if (" ^^ expr e ^^ text ")" ^+ group (stmt s)
  | C.For(init, test, inc, s) ->
                         text "for (" ^^ decl init ^^ semi
                           ^^ expr test ^^ semi
                           ^^ expr inc
                         ^^ text ")"
                         ^+ group (stmt s)
  | C.Comment(str)    -> text ("/* "^str^" */")
  | C.Nop             -> empty
  | _                 -> failwith "stmt: not implemented"

and stmts ss = group (list break (group <<< stmt) ss)

let fundef d =
  let static  = function true -> text "static" | false -> P.empty in
  let formals = d.C.ty.C.args in
  let vargs   = ( match d.C.ty.C.varargs with
                | [] -> P.empty
                | _  -> commabreak ^^ text "..."
                ) in
  let formal (n,t) = group (declaration (text n) t) in
    group begin
      group (static d.C.static ^^^ ty d.C.ty.C.return ^^^ text d.C.name 
         ^^ parent (list commabreak formal formals ^^ vargs))
    ^^^  (brace (decls d.C.decls ^^^ stmts d.C.body)) 
    end

let scope = function
  | C.Public -> P.empty
  | C.Extern -> text "extern"
  | C.Static -> text "static"
  
let toplevel = function
  | C.TopDecl(s,d) -> scope s ^+ decl d ^^ semi
  | C.Function(f)  -> fundef f
  | C.CPP(s)       -> text (s^"\n")    (* hack *)

 

let program tls = 
  let f doc x = doc ^^ break ^^ group (toplevel x) in
  List.fold_left f P.empty tls ^^ break  
