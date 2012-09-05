#!/usr/bin/env ocaml
(* Permission is granted to use and modify this program under WTFPL *)
#use "topfind";;
#require "str";;
#require "unix";;
#require "findlib";;

(* micro-stdlib *)
module Fn = Filename
let (</>) = Fn.concat
open Printf
let (|>) x f = f x
let (|-) f g x = g (f x)
let tap f x = f x; x
let debug = ref false
let dtap f x = if !debug then f x; x
let dprintf fmt = if !debug then printf (fmt ^^ "\n%!") else ifprintf stdout fmt
let (//) x y = if x = "" then y else x
let iff p f x = if p x then f x else x
let de_exn f x = try Some (f x) with _ -> None
let de_exn2 f x y = try Some (f x y) with _ -> None
let mkdir d = if not (Sys.file_exists d) then Unix.mkdir d 0o755
let getenv_def ?(def="") v = try Sys.getenv v with Not_found -> def
let getenv v =
  try Sys.getenv v
  with Not_found -> failwith ("undefined environment variable: " ^ v)
let starts_with s p = Str.string_match (Str.regexp ("^" ^ p)) s 0
let rec str_next str off want =
  if off >= String.length str then None
  else if String.contains want str.[off] then Some(str.[off], off)
  else str_next str (off+1) want
let slice str st en = String.sub str st (en-st) (* from offset st to en-1 *)
let split str chr = let i = String.index str chr in
                    (slice str 0 i, slice str (i+1) (String.length str))
let expand_tilde_slash p =
  if starts_with p "~/" then
	let home_dir = getenv "HOME" in
	Str.replace_first (Str.regexp "^~") home_dir p
  else p
let indir d f =
  let here = Sys.getcwd () in
  dprintf "Changing dirs from %s to %s\n%!" here (expand_tilde_slash d);
  Sys.chdir (expand_tilde_slash d);
  let res = f () in
  dprintf "Returning to %s\n%!" here;
  Sys.chdir here;
  res
let todevnull ?err cmd =
  let err = match err with Some () -> "2" | None -> "" in
  if Sys.os_type = "Win32" then cmd ^ " >NUL" else cmd ^ " " ^ err ^ "> /dev/null"
let detect_exe exe = Sys.command (todevnull ("which \"" ^ exe ^ "\"")) = 0
let get_exe () = (* returns the full path and name of the current program *)
  Sys.argv.(0) |> iff Fn.is_relative (fun e -> Sys.getcwd () </> e)
  |> iff (fun e -> Unix.((lstat e).st_kind = S_LNK)) Unix.readlink
let run_or ~cmd ~err = dprintf "R:%s" cmd; if Sys.command cmd <> 0 then raise err
let chomp s = let l = String.length s in if l = 0 || s.[l-1] != '\r' then s else slice s 0 (l-1)
let print_list l = List.iter (printf "%s ") l; print_newline ()
let rec mapi f i = function [] -> [] | h::t -> let a=f i h in a::mapi f (i+1) t
let rec unopt = function []->[] | Some x::t -> x::unopt t | None::t -> unopt t

let read_lines fn =
  let ic = open_in fn and lst = ref [] in
  try while true do lst := input_line ic :: !lst done; assert false
  with End_of_file -> close_in ic; List.rev !lst
let first_line_output cmd =
  let ic = Unix.open_process_in cmd in
  try let line = input_line ic in ignore(Unix.close_process_in ic); line
  with End_of_file -> ""

(* Useful types *)
module StringSet = struct (* extend type with more operations *)
  include Set.Make(struct type t = string let compare = Pervasives.compare end)
  let of_list l = List.fold_left (fun s e -> add e s) empty l
  let print s =	iter (printf "%s ") s; print_newline ()
end

(* Configurable parameters, some by command line *)
let webroots = Str.split (Str.regexp "|")
	  (getenv_def ~def:"http://oasis.ocamlcore.org/dev/odb/" "ODB_PACKAGE_ROOT")
(*let webroots = ["http://mutt.cse.msu.edu:8081/"] *)
let default_base = (Sys.getenv "HOME") </> ".odb"
let odb_home    = getenv_def ~def:default_base "ODB_INSTALL_DIR"
let odb_lib     = getenv_def ~def:(odb_home </> "lib") "ODB_LIB_DIR"
let odb_stubs   = getenv_def ~def:(odb_lib </> "stublibs") "ODB_STUBS_DIR"
let odb_bin     = getenv_def ~def:(odb_home </> "bin") "ODB_BIN_DIR"
let build_dir   = ref (getenv_def ~def:default_base "ODB_BUILD_DIR")
let sudo = ref (Unix.geteuid () = 0) (* true if root *)
let to_install = ref []
let force = ref false
let force_all = ref false
let repository = ref "stable"
let auto_reinstall = ref false
let have_perms = ref false (* auto-detected in main *)
let ignore_unknown = ref false
let base = ref (getenv_def "GODI_LOCALBASE" // getenv_def "OCAML_BASE")
let configure_flags = ref ""
let configure_flags_global = ref ""
(* what packages need to be reinstalled because of updates *)
let reqs = ref (StringSet.empty)
type main_act = Install | Get | Info | Clean | Package
let main = ref Install

(* Command line argument handling *)
let push_install s = to_install := s :: !to_install
let set_ref ref v = Arg.Unit (fun () -> ref := v)
let cmd_line =  Arg.align [
  "--clean", Arg.Unit(fun () -> main := Clean), " Cleanup downloaded tarballs and install folders";
  "--sudo", Arg.Set sudo, " Switch to root for installs";
  "--have-perms", Arg.Set have_perms, " Don't use --prefix even without sudo";
  "--no-base", Arg.Unit(fun () -> base := ""), " Don't auto-detect GODI/BASE";
  "--configure-flags", Arg.Set_string configure_flags, " Flags to pass to explicitly installed packages' configure step";
  "--configure-flags-all", Arg.Set_string configure_flags_global, " Flags to pass to all packages' configure step";
  "--force", Arg.Set force, " Force (re)installation of packages named";
  "--force-all", Arg.Set force_all, " Force (re)installation of dependencies";
  "--debug", Arg.Set debug, " Debug package dependencies";
  "--unstable", set_ref repository "unstable", " Use unstable repo";
  "--stable", set_ref repository "stable", " Use stable repo";
  "--testing", set_ref repository "testing", " Use testing repo [default]";
  "--auto-reinstall", Arg.Set auto_reinstall, " Auto-reinstall dependent packages on update";
  "--ignore", Arg.Set ignore_unknown, " Don't fail on unknown package name";
  "--get", set_ref main Get, " Only download and extract packages; don't install";
  "--info", set_ref main Info, " Only print the metadata for the packages listed; don't install";
  "--package", set_ref main Package, " Install all packages from package files";
  ]

let () =
  Arg.parse cmd_line push_install "ocaml odb.ml [--sudo] [<packages>]";
  if !base <> "" then print_endline ("Installing to OCaml base: " ^ !base);
  ()

(* micro-http library *)
module Http = struct
  let dl_cmd =
	if detect_exe "curl" then
	  (fun ~silent uri fn ->
	   let s = if silent then " -s" else "" in
	   "curl -f -k -L --url " ^ uri ^ " -o " ^ fn ^ s)
	else if detect_exe "wget" then
	  (fun ~silent uri fn ->
	   let s = if silent then " -q" else "" in
	   "wget --no-check-certificate " ^ uri ^ " -O " ^ fn ^ s)
	else (fun ~silent:_ _uri _fn ->
		  failwith "neither curl nor wget was found, cannot download")
  let get_fn ?(silent=true) uri ?(fn=Fn.basename uri) () =
	dprintf "Getting URI: %s" uri;
	if Sys.command (dl_cmd ~silent uri fn) <> 0 then
	  failwith ("failed to get " ^ uri);
	fn
  let get_contents uri =
	let fn = Fn.temp_file "odb" ".info" in
	ignore(get_fn uri ~fn ());
	let ic = open_in fn in
	let len = in_channel_length ic in
	let ret = String.create len in
	really_input ic ret 0 len;
	close_in ic;
	Unix.unlink fn;
	ret
end

(* Type of a package, with its information in a prop list *)
type pkg = {id: string; mutable props: (string * string) list}

(* micro property-list library *)
module PL = struct
  let get ~p ~n = try List.assoc n p.props with Not_found -> ""
  let get_opt ~p ~n = try Some (List.assoc n p.props) with Not_found -> None
  let get_b ~p ~n =
    try List.assoc n p.props |> bool_of_string with Not_found -> false | Invalid_argument "bool_of_string" -> failwith (sprintf "Cannot convert %s.%s=\"%s\" to bool" p.id n (List.assoc n p.props))
  let get_i ~p ~n =
    try List.assoc n p.props |> int_of_string with Not_found -> -1 | Failure "int_of_string" -> failwith (sprintf "Cannot convert %s.%s=\"%s\" to int" p.id n (List.assoc n p.props))

  let of_string str =
    let rec parse str acc =
      try let key, rest = split str '=' in
          if rest <> "" && rest.[0] = '{' then
            try let value, rest = split rest '}' in
                let value = String.sub value 1 (String.length value - 1) in (* remove '{' *)
                parse rest ((key, value)::acc)
            with Not_found -> failwith "Unclosed { in property list"
          else
            try let value, rest = split rest ' ' in
                parse rest ((key, value)::acc)
            with Not_found -> (key, rest)::acc
      with Not_found -> acc
    in
    let str = str  (* will break files with # not at head of line *)
              |> Str.global_replace (Str.regexp "#[^\n]*\n") ""
              |> Str.global_replace (Str.regexp " *= *") "="
              |> Str.global_replace (Str.regexp "[\n \t\r]+") " " in
    parse str []
  let add ~p k v = p.props <- (k,v) :: p.props
  let modify_assoc ~n f pl =
    try let old_v = List.assoc n pl in
        (n, f old_v) :: List.remove_assoc n pl with Not_found -> pl
  let has_key ~p k0 = List.mem_assoc k0 p.props
  let print p = printf "%s\n" p.id;
                List.iter (fun (k,v) ->
                           if (String.contains v ' ') then printf "%s={%s}\n" k v
                           else printf "%s=%s\n" k v
                          ) p.props;
                printf "\n"
end

let deps_uri id webroot = webroot ^ !repository ^ "/pkg/info/" ^ id
let prefix_webroot webroot fn = webroot ^ !repository ^ "/pkg/" ^ fn
let prefix_webroot_backup wr fn = wr ^ !repository ^ "/pkg/backup/" ^ fn

let make_backup_dl webroot pl = (* create backup tarball location *)
  try let tarball = List.assoc "tarball" pl in
      let backup_addr = prefix_webroot_backup webroot tarball in
      ("tarball2", backup_addr) :: pl
  with Not_found -> pl
let make_install_type pl =
  if List.mem_assoc "inst_type" pl then pl else
    match de_exn2 List.assoc "is_library" pl, de_exn2 List.assoc "is_program" pl with
    | Some "true", _ -> ("inst_type", "lib")::pl
    | _, Some "true" -> ("inst_type", "app")::pl
    | _ -> pl
(* wrapper functions to get data from server *)
let info_cache = Hashtbl.create 10
let get_info id = (* gets a package's info from the repo *)
  try Hashtbl.find info_cache id
  with Not_found ->
    let rec find_uri = function
      | [] -> failwith ("Package not in " ^ !repository ^" repo: " ^ id)
      | webroot :: tl ->
        try deps_uri id webroot |> Http.get_contents
            |> PL.of_string
            |> make_backup_dl webroot
            |> make_install_type (* convert is_* to inst_type *)
            (* prefix the tarball location by the server address *)
            |> PL.modify_assoc ~n:"tarball" (prefix_webroot webroot)
            |> tap (Hashtbl.add info_cache id)
        with Failure _ -> find_uri tl
    in
    find_uri webroots

(* some keywords handled in the packages file for user-defined actions
   to override the default ones *)
let usr_config_key = "config"

let parse_package_line fn linenum str =
  (* remove from the line user commands to override default ones.
     User commands are given between braces like in
     config={~/configure.sh} *)
 if chomp str = "" || str.[0] = '#' then None (* comments and blank lines *)
 else try let id, rest = split str ' ' in
          Hashtbl.add info_cache id (PL.of_string rest);
          Some id
      with Failure s ->
        printf "W: packages file %s line %d is invalid: %s\n" fn linenum s; None

let parse_package_file fn =
  if not (Sys.file_exists fn) then [] else
    let packages = read_lines fn |> mapi (parse_package_line fn) 1 |> unopt in
    dprintf "%d packages loaded from %s\n" (List.length packages) fn; packages

let is_uri str =
  Str.string_match (Str.regexp "^\\(http\\|ftp\\|https\\):") str 0

let get_remote fn =
  if is_uri fn then Http.get_fn ~silent:false fn ()(* download to current dir *)
  else if Fn.is_relative fn then failwith "non-absolute filename not allowed"
  else (dprintf "Local File %s" fn; fn)

(* [get_tarball_check p idir] fetches the tarball associated to the
   package described by property list [p] in directory [idir]. The
   format of the tarball is checked, as well as its integrity if some
   hash information is provided in [p]*)
let get_tarball_chk p idir =
  let attempt url =
    let fn = get_remote url in
    let hash_chk ~actual ~hash =
      if actual <> (PL.get ~p ~n:hash) then
        failwith (sprintf "Tarball %s failed %s verification, aborting\n" fn hash)
      else printf "Tarball %s passed %s check\n" fn hash
    in
    (* checks that the downloaded file is a known archive type *)
    let known_types = ["application/x-gzip" ; "application/zip" ; "application/x-bzip2"] in
    let output = first_line_output ("file -b --mime " ^ fn) in
    ( match Str.split (Str.regexp ";") output with
      | mime::_ when List.mem mime known_types -> ()
      | _ -> failwith ("The format of the downloaded archive is not handled: " ^ output) );
    (* Check the package signature or hash *)
    if PL.has_key ~p "gpg" then
      if not (detect_exe "gpg") then
        failwith ("gpg executable not found; cannot check signature for " ^ fn)
      else
        let sig_uri  = PL.get ~p ~n:"gpg" in
        let sig_file = get_remote sig_uri in
        let cmd = Printf.sprintf "gpg --verify %s %s" sig_file (idir </> fn) in
        printf "gpg command: %s\n%!" cmd;
        if Sys.command cmd == 0 then
          printf "Tarball %s passed gpg check %s\n" fn sig_file
        else hash_chk ~hash:"gpg" ~actual:"gpg check failed"
    else if PL.has_key ~p "sha1" then
      if not (detect_exe "sha1sum") then
        failwith ("sha1sum executable not found; cannot check sum for " ^ fn)
      else
        let out = first_line_output ("sha1sum " ^ fn) in
        match Str.split (Str.regexp " ") out with
        | [sum; _sep; _file] -> hash_chk ~hash:"sha1" ~actual:sum
        | _ -> failwith ("unexpected output from sha1sum: " ^ out)
    else if PL.has_key ~p "md5" then
      hash_chk ~actual:(Digest.file fn |> Digest.to_hex) ~hash:"md5";
    dprintf "Tarball %s has md5 hash %s" fn (Digest.file fn |> Digest.to_hex);

    fn
  in
  try attempt (PL.get ~p ~n:"tarball")
  with Failure s -> (
    printf "First attempt to download the package failed with the following error:\n" ;
    printf "==> %s\nTrying with backup location\n" s ;
    attempt (PL.get ~p ~n:"tarball2")
  )

let to_pkg id =
  if is_uri id then (* remote URL *)
    if Fn.check_suffix id "git" then (* is git clone URI *)
      {id=Fn.basename id |> Fn.chop_extension;
       props = ["git", id; "cli", "yes"]}
    else (* otherwise assume is remote tarball *)
      {id=Fn.basename id; props= ["tarball",id;"cli","yes"]}
  else if Sys.file_exists id then
    if Sys.is_directory id then (* is local directory *)
      {id=Fn.basename id; props= ["dir", id; "cli", "yes"]}
    else (* is local larball *)
      {id=Fn.basename id; props= ["tarball",id;"cli","yes"]}
  else (* is remote file *)
    {id = id; props = get_info id}

(* Version number handling *)
module Ver = struct
  (* A version number is a list of (string or number) *)
  type ver_comp = Num of int | Str of string
  type ver = ver_comp list

  let rec cmp : ver -> ver -> int = fun a b ->
    match a,b with
    | [],[] -> 0 (* each component was equal *)
    (* ignore trailing .0's *)
    | Str"."::Num 0::t, [] -> cmp t [] | [], Str"."::Num 0::t -> cmp [] t
    (* longer version numbers are before shorter ones *)
    | _::_,[] -> 1 | [], _::_ -> -1
    (* compare tails when heads are equal *)
    | (x::xt), (y::yt) when x=y -> cmp xt yt
    (* just compare numbers *)
    | (Num x::_), (Num y::_) -> compare (x:int) y
    (* extend with name ordering? *)
    | (Str x::_), (Str y::_) -> compare (x:string) y
    | (Num x::_), (Str y::_) -> -1 (* a number is always before a string *)
    | (Str x::_), (Num y::_) -> 1   (* a string is always after a number *)
  let to_ver = function
    | Str.Delim s -> Str s
    | Str.Text s -> Num (int_of_string s)
  let parse_ver v =
    try Str.full_split (Str.regexp "[^0-9]+") v |> List.map to_ver
    with Failure _ -> eprintf "Could not parse version: %s" v; []
  let comp_to_string = function Str s -> s | Num n -> string_of_int n
  let to_string v = List.map comp_to_string v |> String.concat ""
end

(* Dependency comparison library *)
module Dep = struct
  open Ver
  type cmp = GE | EQ | GT (* Add more?  Add &&, ||? *)
  type dep = pkg * (cmp * ver) option
  let comp vc = function GE -> vc >= 0 | EQ -> vc = 0 | GT -> vc > 0
  let comp_to_string = function GE -> ">=" | EQ -> "=" | GT -> ">"
  let req_to_string = function
    |None -> ""
    | Some (c,ver) -> (comp_to_string c) ^ (Ver.to_string ver)

  let get_reqs p =
    let p_id_len = String.length p.id in
    try
      Fl_package_base.package_users [] [p.id]
      |> List.filter (fun r -> String.length r < p_id_len
                               || String.sub r 0 p_id_len <> p.id)
    with Findlib.No_such_package _ ->
      []
  let get_ver p =
    match PL.get p "inst_type" with
    | "lib" ->
      ( try Some (Findlib.package_property [] p.id "version" |> parse_ver)
        with Findlib.No_such_package _ -> None )
    | "app" -> (* can't detect version number of programs reliably *)
      if detect_exe p.id then Some [] else None
    | "clib" ->
      ( try Some (parse_ver (first_line_output ("pkg-config --modversion " ^ p.id)))
        with _ -> None )
    | "" | _ ->
           ( try Some (Findlib.package_property [] p.id "version" |> parse_ver)
             with Findlib.No_such_package _ ->
               if not (detect_exe p.id) then None
               else Some (parse_ver (first_line_output p.id)) )

  let has_dep (p,req) = (* true iff p satisfies version requirement req*)
    match req, get_ver p with
    | _, None -> dprintf "Package %s not installed" p.id; false
    | None, Some _ -> dprintf "Package %s installed" p.id; true
    | Some (c,vreq), Some inst -> comp (Ver.cmp inst vreq) c
                                  |> dtap (printf "Package %s(%s) dep satisfied: %B\n%!" p.id (req_to_string req))
  let parse_vreq vr =
    let l = String.length vr in
    if vr.[0] = '>' && vr.[1] = '=' then (GE, parse_ver (String.sub vr 2 (l-3)))
    else if vr.[0] = '>' then (GT, parse_ver (String.sub vr 1 (l-2)))
    else if vr.[0] = '=' then (EQ, parse_ver (String.sub vr 1 (l-2)))
    else failwith ("Unknown comparator in dependency, cannot parse version requirement: " ^ vr)
  let whitespace_rx = Str.regexp "[ \t]+"
  let make_dep str =
    try
      let str = Str.global_replace whitespace_rx "" str in
      match Str.bounded_split (Str.regexp_string "(") str 2 with
      | [pkg; vreq] -> to_pkg pkg, Some (parse_vreq vreq)
      | _ -> to_pkg str, None
    with x -> if !ignore_unknown then {id="";props=[]}, None else raise x
  let string_to_deps s = Str.split (Str.regexp ",") s |> List.map make_dep
                         |> List.filter (fun (p,_) -> p.id <> "")
  let get_deps p = let deps = PL.get ~p ~n:"deps" in
                   if deps = "?" then [] else string_to_deps deps
  let is_auto_dep p = PL.get ~p ~n:"deps" = "?" || PL.get ~p ~n:"cli" = "yes"
  let of_oasis dir = (* reads the [dir]/_oasis file *)
    let fn = dir </> "_oasis" in
    if not (Sys.file_exists fn) then [] else
      let lines = read_lines fn in
      List.map (fun line ->
                match Str.bounded_split (Str.regexp_string ":") line 2 with
                | [("BuildDepends"|"    BuildDepends"); ds] -> (* FIXME, fragile *)
                  [string_to_deps ds]
                | _ -> []) lines |> List.concat

  (* We are being very conservative here - just match all the requires, strip everything after dot *)
  (* grepping META files is not the preffered way of getting dependencies *)
  let require_rx = Str.regexp ".*requires\\(([^)]*)\\)?[ \t]*\\+?=[ \t]*\"\\([^\"]*\\)\".*"
  let meta_rx = Str.regexp "META\\(\\.\\(.*\\)\\)?"
  let weird_rx = Str.regexp "__.*"    (* possibly autoconf var *)
  (* Search for META files - does not honor sym-links *)
  let rec find_metas dir =
    let lst = Sys.readdir dir |> Array.to_list |> List.map ((</>) dir) in
    let dirs, files = List.partition
                        (fun fn -> try Sys.is_directory fn with Sys_error _ -> false) lst in
    let is_valid_meta fn = Str.string_match meta_rx (Fn.basename fn) 0 in
    (* Annotate META with the coresponding package name, either from the directory or suffix *)
    let annot_meta fn =
      let p = Fn.dirname fn in
      let fn' = Fn.basename fn in
      try
        if Str.string_match meta_rx fn' 0 then
          Str.matched_group 2 fn', fn
        else p, fn
      with Not_found -> p, fn
    in
    let metas = List.filter is_valid_meta files in
    let metas = List.map annot_meta metas in
    metas @ (List.map find_metas dirs |> List.concat)
  let of_metas p dir = (* determines dependencies based on META files *)
    let lst = find_metas dir in
    let meta (p, fn) =
      let lines = read_lines fn in
      List.map (fun line ->
                if Str.string_match require_rx line 0 then
                  try Str.split (Str.regexp "[ ,]") (Str.matched_group 2 line)
                  with Not_found -> []
                else []) lines |> List.concat
      (* Consider only base packages, filter out the package we are installing to
         handle cases where we have sub packages *)
      |> List.map (fun p -> Str.split (Str.regexp "\\.") p |> List.hd)
      |> List.filter ((<>) p)
      |> List.filter (fun p -> not (Str.string_match weird_rx p 0))
    in
    List.map meta lst |> List.concat |> List.filter ((<>) p) |> List.map string_to_deps |> List.concat
end

let extract_cmd fn =
  let suff = Fn.check_suffix fn in
  if suff ".tar.gz" || suff ".tgz" then         "tar -zxf " ^ fn
  else if suff ".tar.bz2" || suff ".tbz" then "tar -jxf " ^ fn
  else if suff ".tar.xz" || suff ".txz" then    "tar -Jxf " ^ fn
  else if suff ".zip" then                  "unzip " ^ fn
  else failwith ("Don't know how to extract " ^ fn)

type build_type = Oasis_bootstrap | Oasis | Omake | Make

let string_of_build_type = function
  | Oasis_bootstrap -> "Oasis_bootstrap"
  | Oasis -> "Oasis"
  | Omake -> "OMake"
  | Make -> "Make"

(* detect directory created by tarball extraction or git clone *)
let find_install_dir dir =
  let is_dir fn = Sys.is_directory (dir </> fn) in
  try dir </> (Sys.readdir dir |> Array.to_list |> List.find is_dir)
  with Not_found -> dir

let make_install_dir pid =
  (* Set up the directory to install into *)
  let install_dir = !build_dir </> ("install-" ^ pid) in
  if Sys.file_exists install_dir then
    ignore(Sys.command ("rm -rf " ^ install_dir));
  Unix.mkdir install_dir 0o700;
  install_dir

let clone ~cmd act p =
  let idir = make_install_dir p.id in
  let err = Failure ("Could not " ^ act ^ " for " ^ p.id) in
  indir idir (fun () -> run_or ~cmd ~err);
  find_install_dir idir

let extract_tarball p =
  let idir = make_install_dir p.id in
  let err = Failure ("Could not extract tarball for " ^ p.id) in
  indir idir (fun () -> run_or ~cmd:(extract_cmd (get_tarball_chk p idir)) ~err);
  dprintf "Extracted tarball for %s into %s" p.id idir;
  find_install_dir idir

let clone_git p =
  let cmd = "git clone --depth=1 " ^ PL.get p "git" in
  let cmd = if PL.get p "branch" <> "" then cmd ^ " --branch " ^ PL.get p "branch" else cmd in
  clone ~cmd "clone git" p
let clone_svn p = clone ~cmd:("svn checkout " ^ PL.get p "svn") "checkout svn" p
let clone_cvs p =
  let idir = make_install_dir p.id in
  run_or ~cmd:("cvs -z3 -d" ^ PL.get p "cvs" ^ " co " ^ PL.get p "cvspath")
         ~err:(Failure ("Could not checkout cvs for " ^ p.id));
  idir </> (PL.get p "cvspath") (* special dir for cvs *)
let clone_hg p = clone ~cmd:("hg clone " ^ PL.get p "hg") "clone mercurial" p
let clone_darcs p = clone ~cmd:("darcs get --lazy " ^ PL.get p "darcs") "get darcs" p

(* returns the directory that the package was extracted to *)
let get_package p =
  if PL.has_key ~p "tarball" then extract_tarball p
  else if PL.has_key ~p "dir" then (PL.get ~p ~n:"dir")
  else if PL.has_key ~p "git" then clone_git p
  else if PL.has_key ~p "svn" then clone_svn p
  else if PL.has_key ~p "cvs" then clone_cvs p
  else if PL.has_key ~p "hg"    then clone_hg p
  else if PL.has_key ~p "darcs" then clone_darcs p
  else if PL.has_key ~p "deps" then "." (* packages that are only deps are ok *)
  else failwith ("No download method available for: " ^ p.id)

let get_package p = get_package p |> tap (printf "package downloaded to %s\n%!")

let uninstall p =
  let as_root = PL.get_b p "install_as_root" || !sudo in
  let install_pre =
    if as_root then "sudo " else if !have_perms || !base <> "" then "" else
      "OCAMLFIND_DESTDIR="^odb_lib^" " in
  print_endline ("Uninstalling forced library " ^ p.id);
  Sys.command (install_pre ^ "ocamlfind remove " ^ p.id) |> ignore

(* Installing a package *)
let rec install_from_current_dir ~is_dep p =
  dprintf "Installing %s from %s" p.id (Sys.getcwd ());
  let meta_deps = if Dep.is_auto_dep p then Dep.of_metas p.id (Sys.getcwd ()) else [] in
  List.iter (fun (p,_ as d) ->
             if not (Dep.has_dep d) then install_full ~is_dep:true p) meta_deps;

  (* define exceptions to raise for errors in various steps *)
  let oasis_fail = Failure ("Could not bootstrap from _oasis " ^ p.id)    in
  let config_fail = Failure ("Could not configure " ^ p.id)    in
  let build_fail = Failure ("Could not build " ^ p.id) in
  (*  let test_fail = Failure ("Tests for package " ^ p.id ^ "did not complete successfully") in*)
  let install_fail = Failure ("Could not install package " ^ p.id) in

  (* configure installation parameters based on command-line flags *)
  let as_root = PL.get_b p "install_as_root" || !sudo in
  let config_opt = if as_root || !have_perms then ""
                   else if !base <> "" then " --prefix " ^ !base
                   else " --prefix " ^ odb_home in
  let config_opt = config_opt ^ if not is_dep then (" " ^ !configure_flags) else "" in
  let config_opt = config_opt ^ " " ^ !configure_flags_global in
  let install_pre, destdir =
    if as_root then "sudo ", ""
    else if !have_perms || !base <> "" then "", ""
    else "", odb_lib
  in

  let rec try_build_using buildtype =
    (* Do the install *)
    dprintf "Now installing with %s" (string_of_build_type buildtype);

    match buildtype with
    | Oasis_bootstrap ->
      if not (Sys.file_exists "_oasis") then failwith "_oasis file not found in package, cannot bootstrap.";
      if not (detect_exe "oasis") then (
        printf "This package (%s) most likely needs oasis on the path.\n" p.id;
        print_endline "In general tarballs shouldn't require oasis.";
        print_endline "Trying to get oasis.";
        install_full ~is_dep:false (to_pkg "oasis")
      );
      run_or ~cmd:("oasis setup") ~err:oasis_fail;
      try_build_using Oasis
    | Oasis ->
      run_or ~cmd:("ocaml setup.ml -configure" ^ config_opt) ~err:config_fail;
      run_or ~cmd:"ocaml setup.ml -build" ~err:build_fail;
      Unix.putenv "OCAMLFIND_DESTDIR" destdir;
      (*          run_or ~cmd:"ocaml setup.ml -test" ~err:test_fail;*)
      run_or ~cmd:(install_pre ^ "ocaml setup.ml -install") ~err:install_fail;
    | Omake ->
      if not (detect_exe "omake") then
        failwith "OMake executable not found; cannot build";
      run_or ~cmd:"omake" ~err:build_fail;
      (* TODO: MAKE TEST *)
      run_or ~cmd:(install_pre ^ "omake install") ~err:install_fail;
    | Make ->
      if PL.has_key ~p usr_config_key then
        (* user configure command overrides default one *)
        let config_cmd = PL.get ~p ~n:usr_config_key in
        run_or ~cmd:config_cmd ~err:config_fail;
      else if Sys.file_exists "configure" then
        run_or ~cmd:("./configure" ^ config_opt) ~err:config_fail;
      (* Autodetect 'gnumake', 'gmake' and 'make' *)
      let make =
        if detect_exe "gnumake" then "gnumake"
        else if detect_exe "gmake" then "gmake"
        else if detect_exe "make" then "make"
        else failwith "No gnumake/gmake/make executable found; cannot build"
      in
      if Sys.command make <> 0 then try_build_using Oasis_bootstrap
      else (
        (* We rely on the fact that, at least on windows, setting an environment
         * variable to the empty string amounts to clearing it. *)
        Unix.putenv "OCAMLFIND_DESTDIR" destdir;
        (* TODO: MAKE TEST *)
        run_or ~cmd:(install_pre ^ make ^ " install") ~err:install_fail)
  in

  (* detect build type based on files in directory *)
  let buildtype =
    if Sys.file_exists "setup.ml" then Oasis
    else if Sys.file_exists "OMakefile" && Sys.file_exists "OMakeroot" then Omake
    else Make
  in

  try_build_using buildtype;

  (* test whether installation was successful *)
  if (PL.get p "cli" <> "yes") && not (Dep.has_dep (p,None)) then (
    print_endline ("Problem with installed package: " ^ p.id);
    print_endline ("Installed package is not available to the system");
    print_endline ("Make sure "^odb_lib^" is in your OCAMLPATH");
    print_endline ("and "^odb_bin^" is in your PATH");
    exit 1;
  );

  print_endline ("Successfully installed " ^ p.id);
  Dep.get_reqs p (* return the requirements for this package *)

and install_package ~is_dep p =
  (* uninstall forced libraries *)
  if (Dep.get_ver p <> None) && (PL.get p "inst_type" == "lib") then
    uninstall p;
  indir (get_package p) (fun () -> install_from_current_dir ~is_dep p)

(* install a package and all its deps *)
and install_full ~is_dep p =
  let force_me = !force_all || (not is_dep && !force) in
  match Dep.get_ver p with
  (* abort if old version of dependency exists and no --force-all *)
  | Some v when is_dep && not !force_all ->
    printf "\nDependency %s has version %s but needs to be upgraded.\nTo allow odb to do this, use --force-all\nAborting." p.id (Ver.to_string v);
    exit 1
  (* warn if a dependency is already installed *)
  | Some v when not force_me ->
    let cat, arg = if is_dep then "Dependency", "--force-all" else "Package", "--force" in
    printf "%s %s(%s) already installed, use %s to reinstall\n" cat p.id (Ver.to_string v) arg;
  | _ ->
    printf "Installing %s\n%!" p.id;
    let deps = Dep.get_deps p in
    List.iter (fun (p,_ as d) -> if not (Dep.has_dep d) then install_full ~is_dep:true p) deps;
    if deps <> [] then printf "Deps for %s satisfied\n%!" p.id;
    let rec install_get_reqs p =
      let reqs_imm =
        install_package ~is_dep p |>
          (List.filter (fun s -> not (String.contains s '.'))) |>
          StringSet.of_list
      in
      if !auto_reinstall then
        StringSet.iter
          (fun pid ->
           try install_get_reqs (to_pkg pid)
           with _ ->
             (* if install of req fails, print pid *)
             reqs := StringSet.add pid !reqs)
          reqs_imm
      else
        reqs := StringSet.union reqs_imm !reqs; (* unreinstalled reqs *)
    in
    install_get_reqs p

let install_list pkgs =
  List.iter (to_pkg |- install_full ~is_dep:false) pkgs;
  if not (StringSet.is_empty !reqs) then (
    print_endline "Some packages depend on the just installed packages and should be re-installed.";
    print_endline "The command to do this is:";
    print_string "  ocaml odb.ml --force ";
    StringSet.print !reqs;
  )

let () = (** MAIN **)(* Command line arguments already parsed above, pre-main *)
  ignore(parse_package_file (odb_home </> "packages"));
  ignore(parse_package_file (Fn.dirname (get_exe ()) </> "packages"));
  (* initialize build directory if needed *)
  if !sudo then build_dir := Fn.temp_dir_name
  else (
    mkdir odb_home;
    if not !sudo then (mkdir odb_lib; mkdir odb_bin; mkdir odb_stubs);
  );
  Sys.chdir !build_dir;
  match !main with
  | Clean -> Sys.command ("rm -rvf install-*") |> ignore
  | Package when !to_install = [] -> (* install everything from system packages *)
    if !force_all then
      Hashtbl.iter (fun id p -> install_full ~is_dep:false {id=id;props=p}) info_cache
    else
      print_string "No package file given, use --force-all to install all packages from system package files\n"
  | _ when !to_install = [] -> (* list packages to install *)
          let pkgs =
            List.map (fun wr ->
                      try deps_uri "00list" wr |> Http.get_contents
                      with Failure _ -> printf "%s is unavailable or not a valid repository\n\n" wr; ""
                     ) webroots |> String.concat " "
          in
          let pkgs = Str.split (Str.regexp " +") pkgs in
          (match pkgs with
           | [] -> print_endline "No packages available"
           | hd :: tl -> (* Remove duplicate entries (inefficiently) *)
             let pkgs = List.fold_left (fun accu p -> if List.mem p accu then accu else p :: accu) [hd] tl in
             print_string "Available packages from oasis: ";
             print_list (List.rev pkgs);
          );
          print_string "Locally configured packages:";
          Hashtbl.iter (fun k _v -> printf " %s" k) info_cache;
          print_newline ()
        | Get -> (* just download packages *)
          let print_loc pid =
            printf "Package %s downloaded to %s\n" pid (to_pkg pid |> get_package) in
          List.iter print_loc (List.rev !to_install)
        | Info -> List.iter (to_pkg |- PL.print) (List.rev !to_install)
        | Install -> install_list (List.rev !to_install);
        (* TODO: TEST FOR CAML_LD_LIBRARY_PATH=odb_lib and warn if not set *)
        | Package -> (* install all packages from package files *)
          let ps = List.map (get_remote |- parse_package_file) !to_install |> List.concat in
          print_string "Packages to install: "; print_list ps;
          install_list ps
