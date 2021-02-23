(************************************************************************)
(*         *   The Coq Proof Assistant / The Coq Development Team       *)
(*  v      *   INRIA, CNRS and contributors - Copyright 1999-2019       *)
(* <O___,, *       (see CREDITS file for the list of authors)           *)
(*   \VV/  **************************************************************)
(*    //   *    This file is distributed under the terms of the         *)
(*         *     GNU Lesser General Public License Version 2.1          *)
(*         *     (see LICENSE file for the text of the license)         *)
(************************************************************************)

Require Import ZAxioms ZMulOrder ZSgnAbs NZDiv.
Require Import NZAdd NZOrder ZAdd NZBase.
Require Import GenericMinMax ZMaxMin.


(** * Euclidean Division for integers, Euclid convention
    We use here the "usual" formulation of the Euclid Theorem
    [forall a b, b<>0 -> exists r q, a = b*q+r /\ 0 <= r < |b| ]
    The outcome of the modulo function is hence always positive.
    This corresponds to convention "E" in the following paper:
    R. Boute, "The Euclidean definition of the functions div and mod",
    ACM Transactions on Programming Languages and Systems,
    Vol. 14, No.2, pp. 127-144, April 1992.
    See files [ZDivTrunc] and [ZDivFloor] for others conventions.
    We simply extend NZDiv with a bound for modulo that holds
    regardless of the sign of a and b. This new specification
    subsume mod_bound_pos, which nonetheless stays there for
    subtyping. Note also that ZAxiomSig now already contain
    a div and a modulo (that follow the Floor convention).
    We just ignore them here.
*)

Module Type EuclidSpec (Import A : ZAxiomsSig')(Import B : DivMod A).
 Axiom mod_always_pos : forall a b, b ~= 0 -> 0 <= B.modulo a b < abs b.
End EuclidSpec.

Module Type ZEuclid (Z:ZAxiomsSig) := NZDiv.NZDiv Z <+ EuclidSpec Z.

Module ZEuclidProp
 (Import A : ZAxiomsSig')
 (Import B : ZMulOrderProp A)
 (Import C : ZSgnAbsProp A B)
 (Import D : ZEuclid A).

 (** We put notations in a scope, to avoid warnings about
     redefinitions of notations *)
(* Declare Scope euclid. *)
 Infix "/" := D.div : euclid.
 Infix "mod" := D.modulo : euclid.
 Local Open Scope euclid.

 Module Import Private_NZDiv := Nop <+ NZDivProp A D B.

(** Another formulation of the main equation *)

Lemma mod_eq :
 forall a b, b~=0 -> a mod b == a - b*(a/b).
Proof.
intros.
rewrite <- add_move_l.
symmetry. now apply div_mod.
Qed.

Ltac pos_or_neg a :=
 let LT := fresh "LT" in
 let LE := fresh "LE" in
 destruct (le_gt_cases 0 a) as [LE|LT]; [|rewrite <- opp_pos_neg in LT].

(** Uniqueness theorems *)

Theorem div_mod_unique : forall b q1 q2 r1 r2 : t,
  0<=r1<abs b -> 0<=r2<abs b ->
  b*q1+r1 == b*q2+r2 -> q1 == q2 /\ r1 == r2.
Proof.
intros b q1 q2 r1 r2 Hr1 Hr2 EQ.
pos_or_neg b.
rewrite abs_eq in * by trivial.
apply div_mod_unique with b; trivial.
rewrite abs_neq' in * by auto using lt_le_incl.
rewrite eq_sym_iff. apply div_mod_unique with (-b); trivial.
rewrite 2 mul_opp_l.
rewrite add_move_l, sub_opp_r.
rewrite <-add_assoc.
symmetry. rewrite add_move_l, sub_opp_r.
now rewrite (add_comm r2), (add_comm r1).
Qed.

Theorem div_unique:
 forall a b q r, 0<=r<abs b -> a == b*q + r -> q == a/b.
Proof.
intros a b q r Hr EQ.
assert (Hb : b~=0).
 pos_or_neg b.
 rewrite abs_eq in Hr; intuition; order.
 rewrite <- opp_0, eq_opp_r. rewrite abs_neq' in Hr; intuition; order.
destruct (div_mod_unique b q (a/b) r (a mod b)); trivial.
now apply mod_always_pos.
now rewrite <- div_mod.
Qed.

Theorem mod_unique:
 forall a b q r, 0<=r<abs b -> a == b*q + r -> r == a mod b.
Proof.
intros a b q r Hr EQ.
assert (Hb : b~=0).
 pos_or_neg b.
 rewrite abs_eq in Hr; intuition; order.
 rewrite <- opp_0, eq_opp_r. rewrite abs_neq' in Hr; intuition; order.
destruct (div_mod_unique b q (a/b) r (a mod b)); trivial.
now apply mod_always_pos.
now rewrite <- div_mod.
Qed.

(** Sign rules *)

Lemma div_opp_r : forall a b, b~=0 -> a/(-b) == -(a/b).
Proof.
intros. symmetry.
apply div_unique with (a mod b).
rewrite abs_opp; now apply mod_always_pos.
rewrite mul_opp_opp; now apply div_mod.
Qed.

Lemma mod_opp_r : forall a b, b~=0 -> a mod (-b) == a mod b.
Proof.
intros. symmetry.
apply mod_unique with (-(a/b)).
rewrite abs_opp; now apply mod_always_pos.
rewrite mul_opp_opp; now apply div_mod.
Qed.

Lemma div_opp_l_z : forall a b, b~=0 -> a mod b == 0 ->
 (-a)/b == -(a/b).
Proof.
intros a b Hb Hab. symmetry.
apply div_unique with (-(a mod b)).
rewrite Hab, opp_0. split; [order|].
pos_or_neg b; [rewrite abs_eq | rewrite abs_neq']; order.
now rewrite mul_opp_r, <-opp_add_distr, <-div_mod.
Qed.

Lemma div_opp_l_nz : forall a b, b~=0 -> a mod b ~= 0 ->
 (-a)/b == -(a/b)-sgn b.
Proof.
intros a b Hb Hab. symmetry.
apply div_unique with (abs b -(a mod b)).
rewrite lt_sub_lt_add_l.
rewrite <- le_add_le_sub_l. nzsimpl.
rewrite <- (add_0_l (abs b)) at 2.
rewrite <- add_lt_mono_r.
destruct (mod_always_pos a b); intuition order.
rewrite <- 2 add_opp_r, mul_add_distr_l, 2 mul_opp_r.
rewrite sgn_abs.
rewrite add_shuffle2, add_opp_diag_l; nzsimpl.
rewrite <-opp_add_distr, <-div_mod; order.
Qed.

Lemma mod_opp_l_z : forall a b, b~=0 -> a mod b == 0 ->
 (-a) mod b == 0.
Proof.
intros a b Hb Hab. symmetry.
apply mod_unique with (-(a/b)).
split; [order|now rewrite abs_pos].
now rewrite <-opp_0, <-Hab, mul_opp_r, <-opp_add_distr, <-div_mod.
Qed.

Lemma mod_opp_l_nz : forall a b, b~=0 -> a mod b ~= 0 ->
 (-a) mod b == abs b - (a mod b).
Proof.
intros a b Hb Hab. symmetry.
apply mod_unique with (-(a/b)-sgn b).
rewrite lt_sub_lt_add_l.
rewrite <- le_add_le_sub_l. nzsimpl.
rewrite <- (add_0_l (abs b)) at 2.
rewrite <- add_lt_mono_r.
destruct (mod_always_pos a b); intuition order.
rewrite <- 2 add_opp_r, mul_add_distr_l, 2 mul_opp_r.
rewrite sgn_abs.
rewrite add_shuffle2, add_opp_diag_l; nzsimpl.
rewrite <-opp_add_distr, <-div_mod; order.
Qed.

Lemma div_opp_opp_z : forall a b, b~=0 -> a mod b == 0 ->
 (-a)/(-b) == a/b.
Proof.
intros. now rewrite div_opp_r, div_opp_l_z, opp_involutive.
Qed.

Lemma div_opp_opp_nz : forall a b, b~=0 -> a mod b ~= 0 ->
 (-a)/(-b) == a/b + sgn(b).
Proof.
intros. rewrite div_opp_r, div_opp_l_nz by trivial.
now rewrite opp_sub_distr, opp_involutive.
Qed.

Lemma mod_opp_opp_z : forall a b, b~=0 -> a mod b == 0 ->
 (-a) mod (-b) == 0.
Proof.
intros. now rewrite mod_opp_r, mod_opp_l_z.
Qed.

Lemma mod_opp_opp_nz : forall a b, b~=0 -> a mod b ~= 0 ->
 (-a) mod (-b) == abs b - a mod b.
Proof.
intros. now rewrite mod_opp_r, mod_opp_l_nz.
Qed.

(** A division by itself returns 1 *)

Lemma div_same : forall a, a~=0 -> a/a == 1.
Proof.
intros. symmetry. apply div_unique with 0.
split; [order|now rewrite abs_pos].
now nzsimpl.
Qed.

Lemma mod_same : forall a, a~=0 -> a mod a == 0.
Proof.
intros.
rewrite mod_eq, div_same by trivial. nzsimpl. apply sub_diag.
Qed.

(** A division of a small number by a bigger one yields zero. *)

Theorem div_small: forall a b, 0<=a<b -> a/b == 0.
Proof. exact div_small. Qed.

(** Same situation, in term of modulo: *)

Theorem mod_small: forall a b, 0<=a<b -> a mod b == a.
Proof. exact mod_small. Qed.

(** * Basic values of divisions and modulo. *)

Lemma div_0_l: forall a, a~=0 -> 0/a == 0.
Proof.
intros. pos_or_neg a. apply div_0_l; order.
apply opp_inj. rewrite <- div_opp_r, opp_0 by trivial. now apply div_0_l.
Qed.

Lemma mod_0_l: forall a, a~=0 -> 0 mod a == 0.
Proof.
intros; rewrite mod_eq, div_0_l; now nzsimpl.
Qed.

Lemma div_1_r: forall a, a/1 == a.
Proof.
intros. symmetry. apply div_unique with 0.
assert (H:=lt_0_1); rewrite abs_pos; intuition; order.
now nzsimpl.
Qed.

Lemma mod_1_r: forall a, a mod 1 == 0.
Proof.
intros. rewrite mod_eq, div_1_r; nzsimpl; auto using sub_diag.
apply neq_sym, lt_neq; apply lt_0_1.
Qed.

Lemma div_1_l: forall a, 1<a -> 1/a == 0.
Proof. exact div_1_l. Qed.

Lemma mod_1_l: forall a, 1<a -> 1 mod a == 1.
Proof. exact mod_1_l. Qed.

Lemma div_mul : forall a b, b~=0 -> (a*b)/b == a.
Proof.
intros. symmetry. apply div_unique with 0.
split; [order|now rewrite abs_pos].
nzsimpl; apply mul_comm.
Qed.

Lemma mod_mul : forall a b, b~=0 -> (a*b) mod b == 0.
Proof.
intros. rewrite mod_eq, div_mul by trivial. rewrite mul_comm; apply sub_diag.
Qed.

Theorem div_unique_exact a b q: b~=0 -> a == b*q -> q == a/b.
Proof.
 intros Hb H. rewrite H, mul_comm. symmetry. now apply div_mul.
Qed.

(** * Order results about mod and div *)

(** A modulo cannot grow beyond its starting point. *)

Theorem mod_le: forall a b, 0<=a -> b~=0 -> a mod b <= a.
Proof.
intros. pos_or_neg b. apply mod_le; order.
rewrite <- mod_opp_r by trivial. apply mod_le; order.
Qed.

Theorem div_pos : forall a b, 0<=a -> 0<b -> 0<= a/b.
Proof. exact div_pos. Qed.

Lemma div_str_pos : forall a b, 0<b<=a -> 0 < a/b.
Proof. exact div_str_pos. Qed.

Lemma div_small_iff : forall a b, b~=0 -> (a/b==0 <-> 0<=a<abs b).
Proof.
intros a b Hb.
split.
intros EQ.
rewrite (div_mod a b Hb), EQ; nzsimpl.
now apply mod_always_pos.
intros. pos_or_neg b.
apply div_small.
now rewrite <- (abs_eq b).
apply opp_inj; rewrite opp_0, <- div_opp_r by trivial.
apply div_small.
rewrite <- (abs_neq' b) by order. trivial.
Qed.

Lemma mod_small_iff : forall a b, b~=0 -> (a mod b == a <-> 0<=a<abs b).
Proof.
intros.
rewrite <- div_small_iff, mod_eq by trivial.
rewrite sub_move_r, <- (add_0_r a) at 1. rewrite add_cancel_l.
rewrite eq_sym_iff, eq_mul_0. tauto.
Qed.

(** As soon as the divisor is strictly greater than 1,
    the division is strictly decreasing. *)

Lemma div_lt : forall a b, 0<a -> 1<b -> a/b < a.
Proof. exact div_lt. Qed.

(** [le] is compatible with a positive division. *)

Lemma div_le_mono : forall a b c, 0<c -> a<=b -> a/c <= b/c.
Proof.
intros a b c Hc Hab.
rewrite lt_eq_cases in Hab. destruct Hab as [LT|EQ];
 [|rewrite EQ; order].
rewrite <- lt_succ_r.
rewrite (mul_lt_mono_pos_l c) by order.
nzsimpl.
rewrite (add_lt_mono_r _ _ (a mod c)).
rewrite <- div_mod by order.
apply lt_le_trans with b; trivial.
rewrite (div_mod b c) at 1 by order.
rewrite <- add_assoc, <- add_le_mono_l.
apply le_trans with (c+0).
nzsimpl; destruct (mod_always_pos b c); try order.
rewrite abs_eq in *; order.
rewrite <- add_le_mono_l. destruct (mod_always_pos a c); order.
Qed.

(** In this convention, [div] performs Rounding-Toward-Bottom
    when divisor is positive, and Rounding-Toward-Top otherwise.
    Since we cannot speak of rational values here, we express this
    fact by multiplying back by [b], and this leads to a nice
    unique statement.
*)

Lemma mul_div_le : forall a b, b~=0 -> b*(a/b) <= a.
Proof.
intros.
rewrite (div_mod a b) at 2; trivial.
rewrite <- (add_0_r (b*(a/b))) at 1.
rewrite <- add_le_mono_l.
now destruct (mod_always_pos a b).
Qed.

(** Giving a reversed bound is slightly more complex *)

Lemma mul_succ_div_gt: forall a b, 0<b -> a < b*(S (a/b)).
Proof.
intros.
nzsimpl.
rewrite (div_mod a b) at 1; try order.
rewrite <- add_lt_mono_l.
destruct (mod_always_pos a b). order.
rewrite abs_eq in *; order.
Qed.

Lemma mul_pred_div_gt: forall a b, b<0 -> a < b*(P (a/b)).
Proof.
intros a b Hb.
rewrite mul_pred_r, <- add_opp_r.
rewrite (div_mod a b) at 1; try order.
rewrite <- add_lt_mono_l.
destruct (mod_always_pos a b). order.
rewrite <- opp_pos_neg in Hb. rewrite abs_neq' in *; order.
Qed.

(** NB: The three previous properties could be used as
    specifications for [div]. *)

(** Inequality [mul_div_le] is exact iff the modulo is zero. *)

Lemma div_exact : forall a b, b~=0 -> (a == b*(a/b) <-> a mod b == 0).
Proof.
intros.
rewrite (div_mod a b) at 1; try order.
rewrite <- (add_0_r (b*(a/b))) at 2.
apply add_cancel_l.
Qed.

(** Some additional inequalities about div. *)

Theorem div_lt_upper_bound:
  forall a b q, 0<b -> a < b*q -> a/b < q.
Proof.
intros.
rewrite (mul_lt_mono_pos_l b) by trivial.
apply le_lt_trans with a; trivial.
apply mul_div_le; order.
Qed.

Theorem div_le_upper_bound:
  forall a b q, 0<b -> a <= b*q -> a/b <= q.
Proof.
intros.
rewrite <- (div_mul q b) by order.
apply div_le_mono; trivial. now rewrite mul_comm.
Qed.

Theorem div_le_lower_bound:
  forall a b q, 0<b -> b*q <= a -> q <= a/b.
Proof.
intros.
rewrite <- (div_mul q b) by order.
apply div_le_mono; trivial. now rewrite mul_comm.
Qed.

(** A division respects opposite monotonicity for the divisor *)

Lemma div_le_compat_l: forall p q r, 0<=p -> 0<q<=r -> p/r <= p/q.
Proof. exact div_le_compat_l. Qed.

(** * Relations between usual operations and mod and div *)

Lemma mod_add : forall a b c, c~=0 ->
 (a + b * c) mod c == a mod c.
Proof.
intros.
symmetry.
apply mod_unique with (a/c+b); trivial.
now apply mod_always_pos.
rewrite mul_add_distr_l, add_shuffle0, <- div_mod by order.
now rewrite mul_comm.
Qed.

Lemma div_add : forall a b c, c~=0 ->
 (a + b * c) / c == a / c + b.
Proof.
intros.
apply (mul_cancel_l _ _ c); try order.
apply (add_cancel_r _ _ ((a+b*c) mod c)).
rewrite <- div_mod, mod_add by order.
rewrite mul_add_distr_l, add_shuffle0, <- div_mod by order.
now rewrite mul_comm.
Qed.

Lemma div_add_l: forall a b c, b~=0 ->
 (a * b + c) / b == a + c / b.
Proof.
 intros a b c. rewrite (add_comm _ c), (add_comm a).
 now apply div_add.
Qed.

(** Cancellations. *)

(** With the current convention, the following isn't always true
    when [c<0]: [-3*-1 / -2*-1 = 3/2 = 1] while [-3/-2 = 2] *)

Lemma div_mul_cancel_r : forall a b c, b~=0 -> 0<c ->
 (a*c)/(b*c) == a/b.
Proof.
intros.
symmetry.
apply div_unique with ((a mod b)*c).
(* ineqs *)
rewrite abs_mul, (abs_eq c) by order.
rewrite <-(mul_0_l c), <-mul_lt_mono_pos_r, <-mul_le_mono_pos_r by trivial.
now apply mod_always_pos.
(* equation *)
rewrite (div_mod a b) at 1 by order.
rewrite mul_add_distr_r.
rewrite add_cancel_r.
rewrite <- 2 mul_assoc. now rewrite (mul_comm c).
Qed.

Lemma div_mul_cancel_l : forall a b c, b~=0 -> 0<c ->
 (c*a)/(c*b) == a/b.
Proof.
intros. rewrite !(mul_comm c); now apply div_mul_cancel_r.
Qed.

Lemma mul_mod_distr_l: forall a b c, b~=0 -> 0<c ->
  (c*a) mod (c*b) == c * (a mod b).
Proof.
intros.
rewrite <- (add_cancel_l _ _ ((c*b)* ((c*a)/(c*b)))).
rewrite <- div_mod.
rewrite div_mul_cancel_l by trivial.
rewrite <- mul_assoc, <- mul_add_distr_l, mul_cancel_l by order.
apply div_mod; order.
rewrite <- neq_mul_0; intuition; order.
Qed.

Lemma mul_mod_distr_r: forall a b c, b~=0 -> 0<c ->
  (a*c) mod (b*c) == (a mod b) * c.
Proof.
 intros. rewrite !(mul_comm _ c); now rewrite mul_mod_distr_l.
Qed.


(** Operations modulo. *)

Theorem mod_mod: forall a n, n~=0 ->
 (a mod n) mod n == a mod n.
Proof.
intros. rewrite mod_small_iff by trivial.
now apply mod_always_pos.
Qed.

Lemma mul_mod_idemp_l : forall a b n, n~=0 ->
 ((a mod n)*b) mod n == (a*b) mod n.
Proof.
 intros a b n Hn. symmetry.
 rewrite (div_mod a n) at 1 by order.
 rewrite add_comm, (mul_comm n), (mul_comm _ b).
 rewrite mul_add_distr_l, mul_assoc.
 rewrite mod_add by trivial.
 now rewrite mul_comm.
Qed.

Lemma mul_mod_idemp_r : forall a b n, n~=0 ->
 (a*(b mod n)) mod n == (a*b) mod n.
Proof.
 intros. rewrite !(mul_comm a). now apply mul_mod_idemp_l.
Qed.

Theorem mul_mod: forall a b n, n~=0 ->
 (a * b) mod n == ((a mod n) * (b mod n)) mod n.
Proof.
 intros. now rewrite mul_mod_idemp_l, mul_mod_idemp_r.
Qed.

Lemma add_mod_idemp_l : forall a b n, n~=0 ->
 ((a mod n)+b) mod n == (a+b) mod n.
Proof.
 intros a b n Hn. symmetry.
 rewrite (div_mod a n) at 1 by order.
 rewrite <- add_assoc, add_comm, mul_comm.
 now rewrite mod_add.
Qed.

Lemma add_mod_idemp_r : forall a b n, n~=0 ->
 (a+(b mod n)) mod n == (a+b) mod n.
Proof.
 intros. rewrite !(add_comm a). now apply add_mod_idemp_l.
Qed.

Theorem add_mod: forall a b n, n~=0 ->
 (a+b) mod n == (a mod n + b mod n) mod n.
Proof.
 intros. now rewrite add_mod_idemp_l, add_mod_idemp_r.
Qed.

(** With the current convention, the following result isn't always
    true with a negative intermediate divisor. For instance
    [ 3/(-2)/(-2) = 1 <> 0 = 3 / (-2*-2) ] and
    [ 3/(-2)/2 = -1 <> 0 = 3 / (-2*2) ]. *)

Lemma div_div : forall a b c, 0<b -> c~=0 ->
 (a/b)/c == a/(b*c).
Proof.
 intros a b c Hb Hc.
 apply div_unique with (b*((a/b) mod c) + a mod b).
 (* begin 0<= ... <abs(b*c) *)
 rewrite abs_mul.
 destruct (mod_always_pos (a/b) c), (mod_always_pos a b); try order.
 split.
 apply add_nonneg_nonneg; trivial.
 apply mul_nonneg_nonneg; order.
 apply lt_le_trans with (b*((a/b) mod c) + abs b).
 now rewrite <- add_lt_mono_l.
 rewrite (abs_eq b) by order.
 now rewrite <- mul_succ_r, <- mul_le_mono_pos_l, le_succ_l.
 (* end 0<= ... < abs(b*c) *)
 rewrite (div_mod a b) at 1 by order.
 rewrite add_assoc, add_cancel_r.
 rewrite <- mul_assoc, <- mul_add_distr_l, mul_cancel_l by order.
 apply div_mod; order.
Qed.

(** Similarly, the following result doesn't always hold when [b<0].
    For instance [3 mod (-2*-2)) = 3] while
    [3 mod (-2) + (-2)*((3/-2) mod -2) = -1]. *)

Lemma mod_mul_r : forall a b c, 0<b -> c~=0 ->
 a mod (b*c) == a mod b + b*((a/b) mod c).
Proof.
 intros a b c Hb Hc.
 apply add_cancel_l with (b*c*(a/(b*c))).
 rewrite <- div_mod by (apply neq_mul_0; split; order).
 rewrite <- div_div by trivial.
 rewrite add_assoc, add_shuffle0, <- mul_assoc, <- mul_add_distr_l.
 rewrite <- div_mod by order.
 apply div_mod; order.
Qed.

Lemma mod_div: forall a b, b~=0 ->
 a mod b / b == 0.
Proof.
 intros a b Hb.
 rewrite div_small_iff by assumption.
 auto using mod_always_pos.
Qed.

(** A last inequality: *)

Theorem div_mul_le:
 forall a b c, 0<=a -> 0<b -> 0<=c -> c*(a/b) <= (c*a)/b.
Proof. exact div_mul_le. Qed.

(** mod is related to divisibility *)

Lemma mod_divides : forall a b, b~=0 ->
 (a mod b == 0 <-> (b|a)).
Proof.
intros a b Hb. split.
intros Hab. exists (a/b). rewrite mul_comm.
 rewrite (div_mod a b Hb) at 1. rewrite Hab; now nzsimpl.
intros (c,Hc). rewrite Hc. now apply mod_mul.
Qed.

(********* Lemmas from GenericMinMax that I can't import for some reason? ******)
Lemma max_comm : forall n m, (max n m) == (max m n).
Proof.
Admitted.

Lemma min_comm n m : min n m == min m n.
Proof.
Admitted.

Lemma max_le_iff n m p : p <= max n m <-> p <= n \/ p <= m.
Proof.
Admitted.

Lemma min_le n m p : min n m <= p -> n <= p \/ m <= p.
Proof.
Admitted.

Lemma min_le_iff n m p : min n m <= p <-> n <= p \/ m <= p.
Proof.
Admitted.

Lemma max_lub_lt_iff n m p : max n m < p <-> n < p /\ m < p.
Proof.
Admitted.

(********* Helpful lemmas ************)

Lemma lt_neq_ooo : forall n m, n < m -> m ~= n.
Proof.
  intros.
  cut (n ~= m).
  apply neq_sym.
  apply lt_neq.
  assumption.
Qed.

Lemma neg_div_antimonotone : forall a b c, c < 0 -> a <= b -> a/c >= b/c.
Proof.
  intros.
  rewrite <- opp_involutive with (n := c) at 1.
  rewrite div_opp_r.
  rewrite le_ngt.
  rewrite <- opp_involutive with (n := c) at 1.
  rewrite div_opp_r.
  rewrite nlt_ge.
  rewrite <- opp_le_mono.
  apply div_le_mono.
  apply opp_pos_neg.
  assumption.
  assumption.
  apply lt_neq_ooo.
  apply opp_pos_neg.
  assumption.
  apply lt_neq_ooo.
  apply opp_pos_neg.
  assumption.
Qed.

Lemma max_proper : forall x y z, x == y -> (max x z) == (max y z).
Proof.
  intros.
  cut (x <= z \/ x > z).
  intros.
  destruct H0.
  rewrite max_r.
  cut (y <= z).
  intros.
  rewrite max_r.
  reflexivity.
  assumption.
  rewrite <- H.
  assumption.
  assumption.
  (cut (z <= x)).
  intros.
  rewrite max_l.
  (cut (z <= y)).
  intros.
  rewrite max_l.
  assumption.
  assumption.
  rewrite <- H.
  assumption.
  assumption.
  apply lt_le_incl.
  assumption.
  apply le_gt_cases.
Qed.

Lemma min_proper : forall x y z, x == y -> (min x z) == (min y z).
Proof.
  intros.
  cut (x <= z \/ x > z).
  intros.
  destruct H0.
  rewrite min_l.
  cut (y <= z).
  intros.
  rewrite min_l.
  rewrite H.
  reflexivity.
  assumption.
  rewrite <- H.
  assumption.
  assumption.
  cut (z <= x).
  intros.
  rewrite min_r.
  cut (z <= y).
  intros.
  rewrite min_r.
  reflexivity.
  assumption.
  rewrite <- H.
  assumption.
  assumption.
  apply lt_le_incl.
  assumption.
  apply le_gt_cases.
Qed.

Lemma div_nonzero : forall a b, a ~= 0 -> b ~= 0 -> a mod b == 0 -> a/b ~= 0.
Proof.
  intros.
  cut (a == b*(a/b)).
  intros.
  rewrite <- mul_cancel_l with (p := b).
  rewrite <- H2.
  rewrite mul_0_r.
  assumption.
  assumption.
  apply div_exact.
  assumption.
  assumption.
Qed.

Lemma lt_le_plus_one : forall n, 0 < n -> 1 <= n.
Proof.
  intros.
  rewrite one_succ.
  apply le_succ_l.
  assumption.
Qed.

Lemma mod_lt_denominator : forall a b, 0 < b -> a mod b < b.
Proof.
  intros.
  cut (a < b * (a/b + 1)).
  intros.
  rewrite mul_add_distr_l in H0.
  rewrite mul_1_r in H0.
  rewrite <- lt_sub_lt_add_l in H0.
  rewrite <- mod_eq in H0.
  assumption.
  apply lt_neq_ooo.
  assumption.
  rewrite add_1_r.
  apply mul_succ_div_gt.
  assumption.
Qed.

Lemma mod_gt_z : forall a b, b~= 0 -> 0 <= a mod b.
Proof.
  intros.
  rewrite mod_eq.
  rewrite le_0_sub.
  apply mul_div_le.
  assumption.
  assumption.
Qed.

Lemma pos_div_round_down_mod_z : forall a b, b > 0 -> a mod b == 0 -> a/b == (a + b - 1)/b.
Proof.
  intros.
  cut (b ~= 0).
  intros H00.
  cut ((a + b - 1) mod b == (a + b - 1) - b * ((a + b - 1)/b)).
  intros H1.
  rewrite <- add_sub_assoc in H1.
  rewrite <- add_mod_idemp_l in H1.
  rewrite H0 in H1.
  rewrite add_0_l in H1.
  cut (0 <= (b - 1) < b).
  intros H2.
  rewrite mod_small with (a := (b - 1)) in H1.
  rewrite add_comm with (n := a) (m := (b - 1)) in H1.
  rewrite <- add_move_r in H1.
  rewrite add_cancel_l in H1.
  cut (a == b*(a/b)).
  intros H3.
  rewrite H3 in H1 at 2.
  rewrite <- mul_cancel_l with (p := b).
  rewrite <- add_sub_assoc. 
  rewrite add_comm with (n := a) (m := b - 1).
  apply eq_sym.
  assumption.
  assumption.
  rewrite <- div_exact in H0.
  assumption.
  assumption.
  assumption.
  split.
  rewrite le_0_sub.
  apply lt_le_plus_one.
  assumption.
  rewrite <- add_0_r with (n := b) at 2.
  rewrite <- add_opp_r.
  rewrite <- add_lt_mono_l.
  apply lt_m1_0.
  assumption.
  apply mod_eq.
  assumption.
  apply lt_neq_ooo.
  assumption.
Qed.

Lemma pos_div_round_down_mod_nz : forall a b, b > 0 -> a mod b ~= 0 -> (a + b - 1)/b == a/b + 1.
Proof.
  intros.
  cut (b ~= 0).
  intros H00.
  cut (a mod b == a - b * (a/b)).
  intros H1.
  apply eq_sym in H1.
  rewrite sub_move_r in H1.
  rewrite <- add_cancel_r with (p := (b - 1)) in H1.
  rewrite <- add_opp_r in H1 at 2.
  rewrite add_assoc in H1.
  rewrite <- add_assoc with (n := a mod b) in H1.
  rewrite <- mul_1_r with (n := b) in H1 at 5.
  rewrite <- mul_add_distr_l in H1.
  rewrite add_comm with (n := a mod b) in H1.
  rewrite add_sub_assoc in H1.
  cut (a/b + 1 == (a + b)/b).
  intros H2.
  rewrite H2 in H1.
  rewrite H2.
  apply eq_sym.
  apply div_unique with (a := a + b - 1) (b := b) (q := (a + b)/b) (r := a mod b + -1).
  constructor.
  rewrite add_opp_r.
  rewrite le_0_sub.
  rewrite <- add_0_r with (n := 1).
  rewrite add_1_l.
  rewrite le_succ_l.
  apply le_neq.
  constructor.
  apply mod_gt_z.
  apply lt_neq_ooo.
  assumption.
  apply neq_sym.
  assumption.
  cut (-1 < 0).
  cut (a mod b < abs b).
  intros H3 H4.
  rewrite <- add_0_r with (n := abs b).
  apply add_lt_mono.
  rewrite abs_eq.
  apply mod_lt_denominator.
  assumption.
  apply lt_le_incl.
  assumption.
  apply lt_m1_0.
  rewrite abs_eq.
  apply mod_lt_denominator.
  assumption.
  apply lt_le_incl.
  assumption.
  apply lt_m1_0.
  rewrite <- mul_1_l with (n := b) at 3.
  rewrite div_add.
  rewrite mul_add_distr_l.
  rewrite mul_1_r.
  rewrite <- !add_assoc.
  rewrite add_comm with (m := a mod b + -1).
  rewrite <- add_assoc with (n := a mod b).
  rewrite add_comm with (n := -1) (m := b).
  rewrite !add_assoc.
  rewrite add_opp_r.
  rewrite sub_cancel_r.
  rewrite add_cancel_r.
  apply eq_sym.
  rewrite add_move_l.
  apply mod_eq.
  assumption.
  assumption.
  rewrite <- mul_1_l with (n := b) at 2.
  rewrite div_add.
  reflexivity.
  apply lt_neq_ooo.
  assumption.
  apply mod_eq.
  apply lt_neq_ooo.
  assumption.
  apply lt_neq_ooo.
  assumption.
Qed.

Lemma neg_mod_nz : forall x y, y > 0 -> x mod y ~= 0 -> (- x) mod y ~= 0.
Proof.
  intros.
  rewrite mod_opp_l_nz.
  cut (x mod y < y).
  intros.
  rewrite <- lt_0_sub in H1.
  apply neq_sym.
  apply lt_neq.
  rewrite abs_eq.
  assumption.
  apply lt_le_incl.
  assumption.
  apply mod_lt_denominator.
  assumption.
  apply lt_neq_ooo.
  assumption.
  assumption.
Qed.

Lemma div_mul_mod_z : forall a b c, c > 0 -> b mod c == 0 -> (a*b)/c == a*(b/c).
Proof.
  intros.
  cut (c ~= 0).
  intros H00.
  rewrite <- mul_cancel_l with (p := c).
  rewrite mul_assoc.
  rewrite mul_comm with (n := c) (m := a).
  rewrite <- mul_assoc.
  cut (b == c * (b/c)).
  intros.
  rewrite <- H1.
  cut (a * b == c * ((a * b)/c)).
  intros.
  rewrite <- H2.
  reflexivity.
  rewrite div_exact.
  rewrite <- mul_mod_idemp_r.
  rewrite H0.
  rewrite mul_0_r.
  rewrite mod_0_l.
  reflexivity.
  assumption.
  assumption.
  assumption.
  rewrite div_exact.
  assumption.
  assumption.
  assumption.
  apply lt_neq_ooo.
  assumption.
Qed.

 (* rewrite le_trans with (n := 0) (m := 1). *)

(********* Axioms for new div/mod semantics **)
Lemma div_by_zero : forall a, a / 0 == 0.
Proof.
Admitted.

Lemma mod_by_zero : forall a, a mod 0 == 0.
Proof.
Admitted.

(********* PROOFS OF REWRITE RULES ***********)

(* add104 *)
(* rewrite((x + c0)/c1 + c2, (x + fold(c0 + c1*c2))/c1, c1 != 0, "add104") *)
Lemma add104 : forall x c0 c1 c2, c1 ~= 0 -> (x + c0) / c1 + c2 == (x + (c0 + c1 * c2)) / c1.
Proof.
  intros.
  rewrite <- div_add with (a := (x + c0)).
  rewrite add_assoc.
  rewrite mul_comm.
  reflexivity.
  assumption.
Qed.

(* add105 *)
(* rewrite((x + (y + c0)/c1) + c2, x + (y + fold(c0 + c1*c2))/c1, c1 != 0, "add105") *)
Lemma add105 : forall x y c0 c1 c2, c1 ~= 0 -> x + (y + c0) / c1 + c2 == x + (y + (c0 + c1 * c2)) / c1.
Proof.
  intros.
  rewrite <- add_assoc.
  rewrite add104.
  reflexivity.
  assumption.
Qed.

(* add106 *)
(* rewrite(((y + c0)/c1 + x) + c2, x + (y + fold(c0 + c1*c2))/c1, c1 != 0, "add106") *)
Lemma add106 : forall x y c0 c1 c2, c1 ~= 0 -> (y + c0) / c1 + x + c2 == x + (y + c0 + c1 * c2) / c1.
Proof.
  intros.
  rewrite add_comm with (m := x).
  rewrite <- add_assoc with (n := y).
  apply add105.
  assumption.
Qed.

(* add107 *)
(* rewrite((c0 - x)/c1 + c2, (fold(c0 + c1*c2) - x)/c1, c0 != 0 && c1 != 0, "add107") *)
Lemma add107 : forall x c0 c1 c2, c0 ~= 0 -> c1 ~= 0 -> (c0 - x) / c1 + c2 == ((c0 + c1 * c2) - x) / c1.
Proof.
  intros.
  rewrite <- add_opp_r.
  rewrite add104.
  rewrite add_comm with (n := - x).
  rewrite add_assoc.
  rewrite add_opp_r.
  reflexivity.
  assumption.
Qed.

(* add108 *)
(* rewrite(x + (x + y)/c0, (fold(c0 + 1)*x + y)/c0, c0 != 0, "add108") *)
Lemma add108 : forall x y c0, c0 ~= 0 -> x + (x + y) / c0 == ((c0 + 1) * x + y) / c0.
Proof.
  intros.
  rewrite <- div_add_l with (a := x) (b := c0).
  rewrite add_assoc.
  rewrite <- mul_1_r with (n := x) at 2.
  rewrite mul_comm.
  rewrite mul_comm with (n := x) (m := 1).
  rewrite <- mul_add_distr_r.
  reflexivity.
  assumption.
Qed.

(* add109 *)
(* rewrite(x + (y + x)/c0, (fold(c0 + 1)*x + y)/c0, c0 != 0, "add109") *)
Lemma add109 : forall x y c0, c0 ~= 0 -> x + (y + x) / c0 == ((c0 + 1) * x + y) / c0.
Proof.
  intros.
  rewrite add_comm with (n := y) (m := x).
  apply add108.
  assumption.
Qed.

(* add110 *)
(* rewrite(x + (y - x)/c0, (fold(c0 - 1)*x + y)/c0, c0 != 0, "add110") *)
Lemma add110 : forall x y c0, c0 ~= 0 -> x + (y - x) / c0 == ((c0 - 1) * x + y) / c0.
Proof.
  intros.
  rewrite <- add_opp_r.
  rewrite <- div_add_l with (a := x) (b := c0).
  rewrite add_comm with (n := y) (m := -x).
  rewrite <- mul_1_r with (n := -x).
  rewrite mul_opp_comm.
  rewrite mul_comm.
  rewrite mul_comm with (n := x) (m := -1).
  rewrite add_assoc.
  rewrite <- mul_add_distr_r.
  rewrite add_opp_r.
  reflexivity.
  assumption.
Qed.

(* add111 *)
(* rewrite(x + (x - y)/c0, (fold(c0 + 1)*x - y)/c0, c0 != 0, "add111") *)
Lemma add111 : forall x y c0, c0 ~= 0 -> x + (x - y) / c0 == ((c0 + 1) * x - y)/c0.
Proof.
  intros.
  rewrite <- add_opp_r.
  rewrite add108.
  rewrite add_opp_r.
  reflexivity.
  assumption.
Qed.

(* add112 *)
(* rewrite((x - y)/c0 + x, (fold(c0 + 1)*x - y)/c0, c0 != 0, "add112") *)
Lemma add112 : forall x y c0, c0 ~= 0 -> (x - y) / c0 + x == ((c0 + 1)*x - y) / c0.
Proof.
  intros.
  rewrite add_comm.
  apply add111.
  assumption.
Qed.

(* add113 *)
(* rewrite((y - x)/c0 + x, (y + fold(c0 - 1)*x)/c0, c0 != 0, "add113") *)
Lemma add113 : forall x y c0, c0 ~= 0 -> (y - x) / c0 + x == (y + (c0 - 1) * x) / c0.
Proof.
  intros.
  rewrite add_comm.
  rewrite add110.
  rewrite add_comm.
  reflexivity.
  assumption.
Qed.

(* add114 *)
(* rewrite((x + y)/c0 + x, (fold(c0 + 1)*x + y)/c0, c0 != 0, "add114") *)
Lemma add114 : forall x y c0, c0 ~= 0 -> (x + y) / c0 + x == ((c0 + 1) * x + y) / c0.
Proof.
  intros.
  rewrite add_comm.
  apply add108.
  assumption.
Qed.

(* add115 *)
(* rewrite((y + x)/c0 + x, (y + fold(c0 + 1)*x)/c0, c0 != 0, "add115") *)
Lemma add115 : forall x y c0, c0 ~= 0 -> (y + x) / c0 + x == (y + (c0 + 1) * x) / c0.
Proof.
  intros.
  rewrite add_comm.
  rewrite add109.
  rewrite add_comm.
  reflexivity.
  assumption.
Qed.


(************** SIMPLIFY_DIV ******************)

(* div133 *)
(* rewrite((x / c0) / c2, x / fold(c0 * c2), c0 > 0 && c2 > 0 && !overflows(c0 * c2), "div133") *)
Lemma div133 : forall x c0 c2, c0 > 0 -> (x / c0) / c2 == x / (c0 * c2).
Proof.
  intros.
  cut (c2 == 0 \/ c2 ~= 0).
  intro H0.
  destruct H0.
  rewrite H0.
  rewrite mul_0_r.
  rewrite !div_by_zero.
  reflexivity.
  apply div_div.
  assumption.
  assumption.
  apply eq_decidable.
Qed.

(* div134 *)
(* rewrite((x / c0 + c1) / c2, (x + fold(c1 * c0)) / fold(c0 * c2), c0 > 0 && c2 > 0 && !overflows(c0 * c2) && !overflows(c0 * c1), "div134") *)
Lemma div134 : forall x c0 c1 c2, c0 > 0 -> ((x / c0) + c1) / c2 == (x + (c1 * c0)) / (c0 * c2).
Proof.
  intros.
  cut (c2 == 0 \/ c2 ~= 0).
  intro H0.
  destruct H0.
  rewrite H0.
  rewrite mul_0_r.
  rewrite !div_by_zero.
  reflexivity.
  rewrite <- div_add.
  rewrite div_div.
  reflexivity.
  assumption.
  assumption.
  apply neq_sym.
  apply lt_neq.
  assumption.
  apply eq_decidable.
Qed.

(* div135 *)
(* rewrite((x * c0) / c1, x / fold(c1 / c0), c1 % c0 == 0 && c0 > 0 && c1 / c0 != 0, "div135") *)
Lemma div135 : forall x c0 c1, c0 > 0 -> c1/c0 ~= 0 -> c1 mod c0 == 0 -> (x*c0)/c1 == x/(c1/c0).
Proof.
  intros x c0 c1 H0 H1.
  rewrite <- div_exact with (a := c1) (b := c0).
  intros.
  rewrite H at 1.
  rewrite mul_comm.
  rewrite div_mul_cancel_l.
  reflexivity.
  assumption.
  assumption.
  apply neq_sym.
  apply lt_neq.
  assumption.
Qed.

(* div137 *)
(* rewrite((x * c0) / c1, x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0, "div137") *)
Lemma div137 : forall x c0 c1, c0 mod c1 == 0 -> (x * c0) / c1 == x * (c0 / c1).
Proof.
  intros.
  cut (c1 ~= 0 \/ c1 == 0).
  intros H0.
  destruct H0.
  cut (c0 == c1 * (c0 / c1)).
  intro H2.
  cut (x * c0 == (x * c0) / c1 * c1).
  intro H3.
  rewrite <- mul_cancel_r with (n := x * c0/c1) (m := x * (c0 / c1)) (p := c1).
  rewrite <- mul_assoc.
  rewrite mul_comm with (n := c0 / c1) (m := c1).
  rewrite <- H2.
  rewrite <- H3.
  reflexivity.
  assumption.
  rewrite mul_comm with (m := c1).
  rewrite div_exact.
  rewrite mul_mod.
  rewrite H.
  rewrite mul_0_r.
  apply mod_0_l.
  assumption.
  assumption.
  assumption.
  apply div_exact.
  assumption.
  assumption.
  rewrite H0.
  rewrite !div_by_zero.
  rewrite mul_0_r.
  reflexivity.
  rewrite or_comm.
  apply eq_decidable.
Qed.

(* div139 *)
(* rewrite((x * c0 + y) / c1, y / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0, "div139") *)
Lemma div139 : forall x y c0 c1, c0 mod c1 == 0 -> (x * c0 + y)/c1 == y/c1 + x*(c0/c1).
Proof.
  intros x y c0 c1 H.
  cut (c1 == 0 \/ c1 ~= 0).
  intros H0.
  destruct H0.
  rewrite H0.
  rewrite !div_by_zero.
  rewrite mul_0_r.
  rewrite add_0_r.
  reflexivity.
  rewrite add_comm at 1.
  rewrite <- div_exact in H.
  rewrite H at 1.
  rewrite mul_assoc.
  rewrite mul_comm at 1.
  rewrite mul_assoc.
  rewrite div_add.
  rewrite mul_comm at 1.
  reflexivity.
  assumption.
  assumption.
  apply eq_decidable.
Qed.

(* div140 *)
(* rewrite((x * c0 - y) / c1, (-y) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0, "div140") *)
Lemma div140 : forall x y c0 c1, c0 mod c1 == 0 -> (x * c0 - y)/c1 == (-y)/c1 + x * (c0/c1).
Proof.
  intros.
  rewrite <- add_opp_r.
  apply div139.
  assumption.
Qed.

(* div141 *)
(* rewrite((y + x * c0) / c1, y / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0, "div141") *)
Lemma div141 : forall x y c0 c1, c0 mod c1 == 0 -> (y + x * c0)/c1 == y/c1 + x*(c0/c1).
Proof.
  intros.
  rewrite add_comm at 1.
  apply div139.
  assumption.
Qed.

(* div142 *)
(* rewrite((y - x * c0) / c1, y / c1 - x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0, "div142") *)
Lemma div142 : forall x y c0 c1, c0 mod c1 == 0 -> (y - x*c0)/c1 == y/c1 - x*(c0/c1).
Proof.
  intros.
  rewrite <- add_opp_r.
  rewrite <- mul_opp_l.
  rewrite div141.
  rewrite mul_opp_l.
  rewrite add_opp_r.
  reflexivity.
  assumption.
Qed.

(* div144 *)
(* rewrite(((x * c0 + y) + z) / c1, (y + z) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0, "div144") *)
Lemma div144 : forall x y z c0 c1, c0 mod c1 == 0 -> ((x * c0 + y) + z)/c1 == (y + z)/c1 + x*(c0/c1).
Proof.
  intros.
  rewrite <- add_assoc.
  apply div139.
  assumption.
Qed.

(* div145 *)
(* rewrite(((x * c0 - y) + z) / c1, (z - y) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0, "div145") *)
Lemma div145 : forall x y z c0 c1, c0 mod c1 == 0 -> ((x * c0 - y) + z) / c1 == (z - y) / c1 + x * (c0 / c1).
Proof.
  intros.
  rewrite <- !add_opp_r.
  rewrite add_comm with (n := z) (m := - y).
  apply div144.
  assumption.
Qed.

(* div146 *)
(* rewrite(((x * c0 + y) - z) / c1, (y - z) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0, "div146") *)
Lemma div146 : forall x y z c0 c1, c0 mod c1 == 0 -> ((x * c0 + y) - z) / c1 == (y - z) / c1 + x * (c0 / c1).
Proof.
  intros.
  rewrite <- !add_opp_r.
  apply div144.
  assumption.
Qed.

(* div147 *)
(* rewrite(((x * c0 - y) - z) / c1, (-y - z) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0, "div147") *)
Lemma div147 : forall x y z c0 c1, c0 mod c1 == 0 -> ((x * c0 - y) - z) / c1 == (-y - z) / c1 + x * (c0 / c1).
Proof.
  intros.
  rewrite <- !add_opp_r.
  apply div144.
  assumption.
Qed.

(* div149 *)
(* rewrite(((y + x * c0) + z) / c1, (y + z) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0, "div149") *)
Lemma div149 : forall x y z c0 c1, c0 mod c1 == 0 -> ((y + x * c0) + z) / c1 == (y + z) / c1 + x * (c0 / c1).
Proof.
  intros.
  rewrite add_comm with (n := y) (m := x * c0).
  apply div144.
  assumption.
Qed.

(* div150 *)
(* rewrite(((y + x * c0) - z) / c1, (y - z) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0, "div150") *)
Lemma div150 : forall x y z c0 c1, c0 mod c1 == 0 -> ((y + x * c0) - z) / c1 == (y - z) / c1 + x * (c0 / c1).
Proof.
  intros.
  rewrite add_comm with (n := y) (m := x * c0).
  apply div146.
  assumption.
Qed.

(* div151 *)
(* rewrite(((y - x * c0) - z) / c1, (y - z) / c1 - x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0, "div151") *)
Lemma div151 : forall x y z c0 c1, c0 mod c1 == 0 -> ((y - x * c0) - z) / c1 == (y - z) / c1 - x * (c0 / c1).
Proof.
  intros.
  rewrite <- !add_opp_r.
  rewrite <- add_assoc.
  rewrite add_comm with (n := - (x * c0)) (m := - z).
  rewrite add_assoc.
  rewrite !add_opp_r.
  apply div142.
  assumption.
Qed.

(* div152 *)
(* rewrite(((y - x * c0) + z) / c1, (y + z) / c1 - x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0, "div152") *)
Lemma div152 : forall x y z c0 c1, c0 mod c1 == 0 -> ((y - x * c0) + z) / c1 == (y + z) / c1 - x * (c0 / c1).
Proof.
  intros.
  rewrite <- add_opp_r.
  rewrite <- add_assoc.
  rewrite add_comm with (n := - (x * c0)) (m := z).
  rewrite add_assoc.
  rewrite add_opp_r.
  apply div142.
  assumption.
Qed.

(* div154 *)
(* rewrite((z + (x * c0 + y)) / c1, (z + y) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0, "div154") *)
Lemma div154 : forall x y z c0 c1, c0 mod c1 == 0 -> (z + (x * c0 + y)) / c1 == (z + y) / c1 + x * (c0 / c1).
Proof.
  intros.
  rewrite add_comm.
  rewrite add_comm with (n := z) (m := y).
  apply div144.
  assumption.
Qed.

(* div155 *)
(* rewrite((z + (x * c0 - y)) / c1, (z - y) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0, "div155") *)
Lemma div155 : forall x y z c0 c1, c0 mod c1 == 0 -> (z + (x * c0 - y)) / c1 == (z - y) / c1 + x * (c0 / c1).
Proof.
  intros.
  rewrite add_comm.
  apply div145.
  assumption.
Qed.

(* div156 *)
(* rewrite((z - (x * c0 - y)) / c1, (z + y) / c1 - x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0, "div156") *)
Lemma div156 : forall x y z c0 c1, c0 mod c1 == 0 -> (z - (x * c0 - y)) / c1 == (z + y) / c1 - x * (c0 / c1).
Proof.
  intros.
  rewrite <- add_opp_r.
  rewrite opp_sub_distr with (n := x * c0) (m := y).
  rewrite add_comm with (n := - (x * c0)).
  rewrite add_opp_r.
  rewrite add_comm.
  rewrite add_comm with (n := z) (m := y).
  apply div152.
  assumption.
Qed.

(* div157 *)
(* rewrite((z - (x * c0 + y)) / c1, (z - y) / c1 - x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0, "div157") *)
Lemma div157 : forall x y z c0 c1, c0 mod c1 == 0 -> (z - (x * c0 + y)) / c1 == (z - y) / c1 - x * (c0 / c1).
Proof.
  intros.
  rewrite <- !add_opp_r.
  rewrite opp_add_distr.
  rewrite <- !mul_opp_l.
  apply div154.
  assumption.
Qed.

(* div159 *)
(* rewrite((z + (y + x * c0)) / c1, (z + y) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0, "div159") *)
Lemma div159 : forall x y z c0 c1, c0 mod c1 == 0 -> (z + (y + x * c0)) / c1 == (z + y) / c1 + x * (c0 / c1).
Proof.
  intros.
  rewrite add_comm with (n := y) (m := x*c0).
  apply div154.
  assumption.
Qed.

(* div160 *)
(* rewrite((z - (y + x * c0)) / c1, (z - y) / c1 - x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0, "div160") *)
Lemma div160 : forall x y z c0 c1, c0 mod c1 == 0 -> (z - (y + x * c0)) / c1 == (z - y) / c1 - x * (c0 / c1).
Proof.
  intros.
  rewrite add_comm with (n := y) (m := x*c0).
  apply div157.
  assumption.
Qed.

(* div161 *)
(* rewrite((z + (y - x * c0)) / c1, (z + y) / c1 - x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0, "div161") *)
Lemma div161 : forall x y z c0 c1, c0 mod c1 == 0 -> (z + (y - x * c0)) / c1 == (z + y) / c1 - x * (c0 / c1).
Proof.
  intros.
  rewrite add_comm.
  rewrite add_comm with (n := z) (m := y).
  apply div152.
  assumption.
Qed.

(* div162 *)
(* rewrite((z - (y - x * c0)) / c1, (z - y) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0, "div162") *)
Lemma div162 : forall x y z c0 c1, c0 mod c1 == 0 -> (z - (y - x * c0)) / c1 == (z - y) / c1 + x * (c0 / c1).
Proof.
  intros.
  rewrite <- add_opp_r.
  rewrite opp_sub_distr.
  rewrite add_comm with (n := -y) (m := x * c0).
  rewrite add_opp_r.
  apply div155.
  assumption.
Qed.

(* div165 *)
(* rewrite((((x * c0 + y) + z) + w) / c1, (y + z + w) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0, "div165") *)
Lemma div165 : forall x y z w c0 c1, c0 mod c1 == 0 -> (((x * c0 + y) + z) + w) / c1 == (y + z + w) / c1 + x * (c0 / c1).
Proof.
  intros.
  rewrite <- add_assoc.
  rewrite <- add_assoc with (n := y) (m := z) (p := w).
  apply div144.
  assumption.
Qed.

(* div166 *)
(* rewrite((((y + x * c0) + z) + w) / c1, (y + z + w) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0, "div166") *)
Lemma div166 : forall x y z w c0 c1, c0 mod c1 == 0 -> (((y + x * c0) + z) + w) / c1 == (y + z + w) / c1 + x * (c0 / c1).
Proof.
  intros.
  rewrite add_comm with (n := y) (m := x * c0).
  apply div165.
  assumption.
Qed.

(* div167 *)
(* rewrite(((z + (x * c0 + y)) + w) / c1, (y + z + w) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0, "div167") *)
Lemma div167 : forall x y z w c0 c1, c0 mod c1 == 0 -> ((z + (x * c0 + y)) + w) / c1 == (y + z + w) / c1 + x * (c0 / c1).
Proof.
  intros.
  rewrite add_comm with (n := z) (m := (x*c0 + y)).
  apply div165.
  assumption.
Qed.

(* div168 *)
(* rewrite(((z + (y + x * c0)) + w) / c1, (y + z + w) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0, "div168") *)
Lemma div168 : forall x y z w c0 c1, c0 mod c1 == 0 -> ((z + (y + x * c0)) + w) / c1 == (y + z + w) / c1 + x * (c0 / c1).
Proof.
  intros.
  rewrite add_comm with (n := y) (m := x * c0).
  apply div167.
  assumption.
Qed.

(* div169 *)
(* rewrite((w + ((x * c0 + y) + z)) / c1, (y + z + w) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0, "div169") *)
Lemma div169 : forall x y z w c0 c1, c0 mod c1 == 0 -> (w + ((x * c0 + y) + z)) / c1 == (y + z + w) / c1 + x * (c0 / c1).
Proof.
  intros.
  rewrite add_comm with (n := w) (m := (x * c0 + y) + z).
  apply div165.
  assumption.
Qed.

(* div170 *)
(* rewrite((w + ((y + x * c0) + z)) / c1, (y + z + w) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0, "div170") *)
Lemma div170 : forall x y z w c0 c1, c0 mod c1 == 0 -> (w + ((y + x * c0) + z)) / c1 == (y + z + w) / c1 + x * (c0 / c1).
Proof.
  intros.
  rewrite add_comm with (n := y) (m := x * c0).
  apply div169.
  assumption.
Qed.

(* div171 *)
(* rewrite((w + (z + (x * c0 + y))) / c1, (y + z + w) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0, "div171") *)
Lemma div171 : forall x y z w c0 c1, c0 mod c1 == 0 -> (w + (z + (x * c0 + y))) / c1 == (y + z + w) / c1 + x * (c0 / c1).
Proof.
  intros.
  rewrite add_comm with (n := z) (m := x * c0 + y).
  apply div169.
  assumption.
Qed.

(* div172 *)
(* rewrite((w + (z + (y + x * c0))) / c1, (y + z + w) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0, "div172") *)
Lemma div172 : forall x y z w c0 c1, c0 mod c1 == 0 -> (w + (z + (y + x * c0))) / c1 == (y + z + w) / c1 + x * (c0 / c1).
Proof.
  intros.
  rewrite add_comm with (n := y) (m := x * c0).
  apply div171.
  assumption.
Qed.

(* div175 *)
(* rewrite((x + c0) / c1, x / c1 + fold(c0 / c1), c0 % c1 == 0 && c1 > 0, "div175") *)
Lemma div175 : forall x c0 c1, c0 mod c1 == 0 -> (x + c0)/c1 == x/c1 + c0/c1.
Proof.
  intros x c0 c1 H.
  cut (c1 == 0 \/ c1 ~= 0).
  intros H0.
  destruct H0.
  rewrite H0.
  rewrite !div_by_zero.
  rewrite add_0_r.
  reflexivity.
  rewrite <- div_exact with (b := c1) in H.
  rewrite H at 1.
  rewrite mul_comm at 1.
  rewrite div_add.
  reflexivity.
  assumption.
  assumption.
  apply eq_decidable.
Qed.

Lemma div176_lemma : forall c0 c1, c1 > 1 -> (c0 + 1) mod c1 == 0 -> c0 mod c1 ~= 0.
Proof.
  intros c0 c1 H H0.
  rewrite <- add_mod_idemp_l in H0.
  unfold not.
  intros H1.
  rewrite H1 in H0.
  rewrite add_0_l in H0.
  cut (1 mod c1 == 1).
  intros H2.
  rewrite H2 in H0.
  rewrite one_succ in H0.
  cut (S 0 ~= 0).
  intros.
  contradiction.
  apply neq_succ_diag_l.
  apply mod_small.
  cut (0 <= 1).
  intros.
  auto.
  apply le_0_1.
  cut (0 < 1).
  intros.
  apply lt_neq_ooo.
  apply lt_trans with (n := 0) (m := 1) (p := c1).
  assumption.
  assumption.
  apply lt_0_1.
Qed.

Lemma one_gt : forall n, 1 < n -> 0 <= 1 < n.
Proof.
  intros.
  cut (0 <= 1).
  intros.
  auto.
  apply le_0_1.
Qed.

Lemma gt_one_nz : forall n, 1 < n -> n ~= 0.
Proof.
  intros.
  apply lt_neq_ooo.
  cut (0 < 1).
  intros.
  apply lt_trans with (n := 0) (m := 1) (p := n).
  assumption.
  assumption.
  apply lt_0_1.
Qed.

Lemma div176_lemma2 : forall c0 c1, c1 > 1 -> (c0 + 1) mod c1 == 0 -> c0 mod c1 == (c1 - 1).
Proof.
  intros.
  cut (c0 + 1 == c1*((c0 + 1)/c1)).
  intros.
  rewrite add_move_r in H1.
  rewrite H1.
  rewrite <- add_opp_r.
  rewrite <- add_mod_idemp_l.
  cut (c0 + 1 == c1*((c0 + 1)/c1)).
  intros.
  rewrite <- H2.
  rewrite H0.
  rewrite add_0_l.
  rewrite mod_opp_l_nz.
  rewrite mod_small.
  rewrite abs_eq.
  reflexivity.
  rewrite one_succ in H.
  apply lt_succ_l in H.
  apply lt_le_incl.
  assumption.
  apply one_gt.
  assumption.
  apply gt_one_nz.
  assumption.
  rewrite mod_small.
  rewrite one_succ.
  apply neq_succ_diag_l.
  apply one_gt.
  assumption.
  apply div_exact.
  apply gt_one_nz.
  assumption.
  assumption.
  apply gt_one_nz.
  assumption.
  apply div_exact.
  apply gt_one_nz.
  assumption.
  assumption.
Qed.

(* div176 *)
(* rewrite((c0 - y)/c1, fold(c0 / c1) - y / c1, (c0 + 1) % c1 == 0 && c1 > 0, "div176") *)
Lemma div176 : forall y c0 c1, (c0 + 1) mod c1 == 0 -> c1 > 0 -> (c0 - y)/c1 == (c0 / c1) - y / c1.
Proof.
  intros.
  rewrite <- le_succ_l in H0.
  rewrite <- one_succ in H0.
  cut (0 < c1).
  intros H00.
  cut (c1 == 1 \/ c1 > 1).
  intros.
  destruct H1.
  rewrite H1.
  rewrite !div_1_r.
  reflexivity.
  cut (c0 == c1*(c0/c1) + (c1 - 1)).
  intros H3.
  rewrite H3 at 1.
  rewrite <- add_opp_r.
  rewrite <- add_assoc.
  rewrite mul_comm.
  rewrite div_add_l.
  rewrite add_comm with (n := (c1 - 1)).
  rewrite add_sub_assoc.
  cut (y mod c1 == 0 \/ y mod c1 ~= 0 ).
  intros.
  destruct H2.
  rewrite <- pos_div_round_down_mod_z.
  rewrite div_opp_l_z.
  rewrite add_opp_r.
  reflexivity.
  apply gt_one_nz.
  assumption.
  assumption.
  assumption.
  apply mod_opp_l_z.
  apply gt_one_nz.
  assumption.
  assumption.
  rewrite pos_div_round_down_mod_nz.
  rewrite div_opp_l_nz.
  rewrite sgn_pos.
  rewrite <- add_opp_r.
  rewrite <- add_assoc.
  rewrite add_opp_diag_l.
  rewrite add_0_r.
  rewrite add_opp_r.
  reflexivity.
  assumption.
  apply gt_one_nz.
  assumption.
  assumption.
  assumption.
  apply neg_mod_nz.
  assumption.
  assumption.
  apply eq_decidable.
  apply lt_neq_ooo.
  assumption.
  rewrite <- div176_lemma2 with (c0 := c0).
  apply eq_sym.
  rewrite add_move_l.
  apply mod_eq.
  apply lt_neq_ooo.
  assumption.
  assumption.
  assumption.
  rewrite le_lteq in H0.
  intuition.
  rewrite one_succ in H0.
  apply le_succ_l in H0.
  assumption.
Qed.
  
(* div178 (Originally 160 *)
(* denominator_non_zero && rewrite((x + y)/x, y/x + 1, "div178") *)
Lemma div178 : forall x y, x ~= 0 -> (x + y)/x == (y/x + 1).
Proof.
  intros.
  rewrite <- mul_1_l with (n := x) at 1.
  rewrite div_add_l.
  rewrite add_comm at 1.
  reflexivity.
  assumption.
Qed.

(* div179 *)
(* denominator_non_zero && rewrite((y + x)/x, y/x + 1, "div179") *)
Lemma div179 : forall x y, x ~=  0 -> (y + x)/x == (y/x + 1).
Proof.
  intros.
  rewrite add_comm at 1.
  apply div178.
  assumption.
Qed.

(* div180 *)
(* denominator_non_zero && rewrite((x - y)/x, (-y)/x + 1, "div180") *)
Lemma div180 : forall x y, x ~= 0 -> (x - y)/x == ((-y)/x + 1).
Proof.
  intros.
  rewrite <- add_opp_r.
  apply div178.
  assumption.
Qed.

(* div181 *)
(* denominator_non_zero && rewrite((y - x)/x, y/x - 1, "div181") *)
Lemma div181 : forall x y, x ~= 0 -> (y - x)/x == y/x - 1.
Proof.
  intros.
  rewrite <- mul_1_l with (n := x) at 1.
  rewrite <- add_opp_r.
  rewrite <- mul_opp_l.
  rewrite div_add.
  rewrite add_opp_r.
  reflexivity.
  assumption.
Qed.

(* div182 *)
(* denominator_non_zero && rewrite(((x + y) + z)/x, (y + z)/x + 1, "div182") *)
Lemma div182 : forall x y z, x ~= 0 -> (x + y + z)/x == (y + z)/x + 1.
Proof.
  intros.
  rewrite <- mul_1_l with (n := x) at 1.
  rewrite <- add_assoc.
  rewrite div_add_l.
  rewrite add_comm at 1.
  reflexivity.
  assumption.
Qed.

(* div183 *)
(* denominator_non_zero && rewrite(((y + x) + z)/x, (y + z)/x + 1, "div183") *)
Lemma div183 : forall x y z, x ~= 0 -> ((y + x) + z)/x == (y + z)/x + 1.
Proof.
  intros.
  rewrite <- add_assoc.
  rewrite add_comm with (n := x).
  rewrite add_assoc.
  rewrite <- mul_1_l with (n := x) at 1.
  rewrite div_add.
  reflexivity.
  assumption.
Qed.

(* div184 *)
(* denominator_non_zero && rewrite((z + (x + y))/x, (z + y)/x + 1, "div184") *)
Lemma div184 : forall x y z, x ~= 0 -> (z + (x + y))/ x == (z + y)/x + 1.
Proof.
  intros.
  rewrite add_comm.
  rewrite add_comm with (n := z).
  apply div182.
  assumption.
Qed.

(* div185 *)
(* denominator_non_zero && rewrite((z + (y + x))/x, (z + y)/x + 1, "div185") *)
Lemma div185 : forall x y z, x ~= 0 -> (z + (y + x))/ x == (z + y)/x + 1.
Proof.
  intros.
  rewrite add_comm with (n := y) (m := x).
  apply div184.
  assumption.
Qed.

(* div186 *)
(* denominator_non_zero && rewrite((x*y)/x, y, "div186") *)
Lemma div186 : forall x y, x ~= 0 -> (x*y)/x == y.
Proof.
  intros.
  rewrite mul_comm.
  apply div_mul.
  assumption.
Qed.

(* div187 *)
(* denominator_non_zero && rewrite((y*x)/x, y, "div187") *)
Lemma div187 : forall x y, x ~= 0 -> (y*x) / x == y.
Proof.
  intros.
  apply div_mul.
  assumption.
Qed.

(* div188 *)
(* denominator_non_zero && rewrite((x*y + z)/x, y + z/x, "div188") *)
Lemma div188 : forall x y z, x ~= 0 -> (x * y + z)/x == (y + z/x).
Proof.
  intros.
  rewrite mul_comm at 1.
  rewrite div_add_l.
  reflexivity.
  assumption.
Qed.

(* div189 *)
(* denominator_non_zero && rewrite((y*x + z)/x, y + z/x, "div189") *)
Lemma div189 : forall x y z, x ~= 0 -> (y * x + z)/x == (y + z/x).
Proof.
  intros.
  rewrite mul_comm at 1.
  apply div188.
  assumption.
Qed.

(* div190 *)
(* denominator_non_zero && rewrite((z + x*y)/x, z/x + y, "div190") *)
Lemma div190 : forall x y z, x ~= 0 -> (z + x * y)/x == z/x + y.
Proof.
  intros.
  rewrite add_comm at 1.
  rewrite div188.
  rewrite add_comm at 1.
  reflexivity.
  assumption.
Qed.

(* div191 *)
(* denominator_non_zero && rewrite((z + y*x)/x, z/x + y, "div191") *)
Lemma div191 : forall x y z, x ~= 0 -> (z + y*x)/x == z/x + y.
Proof.
  intros.
  rewrite mul_comm.
  apply div190.
  assumption.
Qed.

(* div192 *)
(* denominator_non_zero && rewrite((x*y - z)/x, y + (-z)/x, "div192") *)
Lemma div192 : forall x y z, x ~= 0 -> (x*y - z)/x == (y + (-z)/x).
Proof.
  intros.
  rewrite <- add_opp_r.
  rewrite mul_comm.
  rewrite div_add_l.
  reflexivity.
  assumption.
Qed.

(* div193 *)
(* rewrite((y*x - z)/x, y + (-z)/x) *)
(* denominator_non_zero && rewrite((y*x - z)/x, y + (-z)/x, "div193") *)
Lemma div193 : forall x y z, x ~= 0 -> (y*x - z)/x == y + (-z)/x.
Proof.
  intros.
  rewrite mul_comm.
  apply div192.
  assumption.
Qed.

(* div194 *)
(* denominator_non_zero && rewrite((z - x*y)/x, z/x - y, "div194") *)
Lemma div194 : forall x y z, x ~= 0 -> (z - x*y)/x == z/x - y.
Proof.
  intros.
  rewrite <- add_opp_r.
  rewrite <- mul_opp_r.
  rewrite mul_comm.
  rewrite div_add.
  rewrite add_opp_r.
  reflexivity.
  assumption.
Qed.

(* div195 *)
(* denominator_non_zero && rewrite((z - y*x)/x, z/x - y, "div195") *)
Lemma div195 : forall x y z, x ~= 0 -> (z - y*x)/x == (z/x - y).
Proof.
  intros.
  rewrite mul_comm.
  apply div194.
  assumption.
Qed.

(* div200 *)
(* rewrite(ramp(x, c0) / broadcast(c1), ramp(x / c1, fold(c0 / c1), lanes), c0 % c1 == 0, "div200") *)
Lemma div200 : forall x c0 c1 lanes, c0 mod c1 == 0 -> (x + c0 * lanes) / c1 == (x/c1 + (c0/c1)*lanes).
Proof.
  intros.
  cut (c1 == 0 \/ c1 ~= 0).
  intros H0.
  destruct H0.
  rewrite H0.
  rewrite !div_by_zero.
  rewrite mul_0_l.
  rewrite add_0_r.
  reflexivity.
  rewrite <- div_exact in H.
  rewrite mul_comm in H.
  rewrite H at 1.
  rewrite <- mul_assoc.
  rewrite mul_comm with (n := c1) (m := lanes).
  rewrite mul_assoc.
  rewrite div_add.
  reflexivity.
  assumption.
  assumption.
  apply eq_decidable.
Qed.

(* div201 *)
(* rewrite(ramp(x, c0) / broadcast(c1), broadcast(x / c1, lanes),
                       // First and last lanes are the same when...
                       can_prove((x % c1 + c0 * (lanes - 1)) / c1 == 0, this), "div201"))) *)
Lemma div201 : forall x c0 c1 lanes, ((x mod c1) + c0 * lanes) / c1 == 0 -> (x + c0 * lanes) / c1 == x/ c1.
Proof.
  intros.
  cut (c1 == 0 \/ c1 ~= 0).
  intros H0.
  destruct H0.
  rewrite H0.
  rewrite !div_by_zero.
  reflexivity.
  cut (x == c1 *(x/c1) + x mod c1).
  intros.
  rewrite H1 at 1.
  rewrite mul_comm with (n := c1) (m := x/c1).
  rewrite <- add_assoc.
  rewrite div_add_l.
  rewrite H.
  rewrite add_0_r.
  reflexivity.
  assumption.
  rewrite add_comm.
  rewrite <- sub_move_r.
  apply eq_sym.
  apply mod_eq.
  assumption.
  apply eq_decidable.
Qed.

(* div207 *)
(* rewrite((x * c0 + c1) / c2, (x + fold(c1 / c0)) / fold(c2 / c0), c2 > 0 && c0 > 0 && c2 % c0 == 0, "div207") *)
Lemma div207 : forall x c0 c1 c2, c2 ~= 0 -> c0 > 0 -> c2 mod c0 == 0 -> (x * c0 + c1)/c2 == (x + (c1/c0))/(c2/c0).
Proof.
  intros.
  cut (c0 ~= 0).
  intros.
  rewrite <- div_exact in H1.
  rewrite H1.
  rewrite <- div_div.
  rewrite div_add_l.
  rewrite <- H1.
  reflexivity.
  assumption.
  assumption.
  apply div_nonzero.
  assumption.
  assumption.
  apply div_exact.
  assumption.
  assumption.
  assumption.
  apply lt_neq_ooo.
  assumption.
Qed.

(* div210 *)
(* rewrite((x * c0 + c1) / c2, x * fold(c0 / c2) + fold(c1 / c2), c2 > 0 && c0 % c2 == 0, "div210") *)
Lemma div210 : forall x c0 c1 c2, c0 mod c2 == 0 -> (x * c0 + c1) / c2 == x * (c0 / c2) + (c1 / c2).
Proof.
  intros.
  cut (c2 == 0 \/ c2 ~= 0).
  intros H0.
  destruct H0.
  rewrite H0.
  rewrite !div_by_zero.
  rewrite add_0_r.
  rewrite mul_0_r.
  reflexivity.
  rewrite <- div_exact in H.
  rewrite H at 1.
  rewrite mul_comm with (n := c2).
  rewrite mul_assoc.
  rewrite div_add_l.
  reflexivity.
  assumption.
  assumption.
  apply eq_decidable.
Qed.

(* expr114 *)
(* rewrite(ramp(x, 0), broadcast(x, lanes), "expr114") *)
Lemma expr114 : forall x lanes, x + 0 * lanes == x.
Proof.
  intros.
  rewrite mul_0_l.
  rewrite add_0_r.
  reflexivity.
Qed.

(* lt49 *)
(* rewrite(ramp(x, c1) < broadcast(z), true, can_prove(x + fold(max(0, c1 * (lanes - 1))) < z, this), "lt49") *)
Lemma lt49 : forall x z c1 lanes, ((x + (max 0 (c1 * lanes))) < z) -> (x + c1 * lanes) < z.
Proof.
  intros.
  rewrite lt_add_lt_sub_l.
  rewrite lt_add_lt_sub_l in H.
  cut (0 <= c1 * lanes \/ c1 * lanes <= 0).
  intros H0.
  destruct H0.
  rewrite max_r in H.
  assumption.
  assumption.
  rewrite max_l in H.
  apply le_lt_trans with (n := c1 * lanes) (m := 0) (p := z - x).
  assumption.
  assumption.
  assumption.
  apply le_ge_cases.
Qed.

(* lt50 *)
(* rewrite(ramp(x, c1) < broadcast(z), false, can_prove(x + fold(min(0, c1 * (lanes - 1))) >= z, this), "lt50") *)
Lemma lt50 : forall x z c1 lanes, x + (min 0 (c1 * lanes)) >= z -> ~(x + c1 * lanes < z).
Proof.
  intros.
  rewrite <- le_ngt.
  rewrite <- le_sub_le_add_l.
  rewrite <- le_sub_le_add_l in H.
  cut (0 <= c1 * lanes \/ c1 * lanes <= 0).
  intros H0.
  destruct H0.
  rewrite min_l in H.
  apply le_trans with (n := z - x) (m := 0) (p := c1 * lanes).
  assumption.
  assumption.
  assumption.
  rewrite min_r in H.
  assumption.
  assumption.
  apply le_ge_cases.
Qed.

(* lt51 *)
(* rewrite(broadcast(z) < ramp(x, c1), true, can_prove(z < x + fold(min(0, c1 * (lanes - 1))), this), "lt51") *)
Lemma lt51 : forall x z c1 lanes, z < (x + (min 0 (c1 * lanes))) -> z < (x + c1 * lanes).
Proof.
  intros.
  rewrite <- lt_sub_lt_add_l.
  rewrite <- lt_sub_lt_add_l in H.
  cut (0 <= c1 * lanes \/ c1 * lanes <= 0).
  intros H0.
  destruct H0.
  rewrite min_l in H.
  apply lt_le_trans with (n := z - x) (m := 0) (p := c1 * lanes).
  assumption.
  assumption.
  assumption.
  rewrite min_r in H.
  assumption.
  assumption.
  apply le_ge_cases.
Qed.

(* lt52 *)
(* rewrite(broadcast(z) < ramp(x, c1), false, can_prove(z >= x + fold(max(0, c1 * (lanes - 1))), this), "lt52") *)
Lemma lt52 : forall x z c1 lanes, z >= (x + (max 0 (c1 * lanes))) -> ~(z < (x + c1 * lanes)).
Proof.
  intros.
  rewrite <- le_ngt.
  rewrite le_add_le_sub_l.
  rewrite le_add_le_sub_l in H.
  cut (0 <= c1 * lanes \/ c1 * lanes <= 0).
  intros H0.
  destruct H0.
  rewrite max_r in H.
  assumption.
  assumption.
  rewrite max_l in H.
  apply le_trans with (n := c1 * lanes) (m := 0) (p := z - x).
  assumption.
  assumption.
  assumption.
  apply le_ge_cases.
Qed.

(* lt143 *)
(* rewrite(x * c0 < c1, x < fold((c1 + c0 - 1) / c0), c0 > 0, "lt143") *)
Lemma lt143 : forall x c0 c1, c0 > 0 -> x * c0 < c1 -> x < (c1 + c0 - 1)/c0.
Proof.
  intros.
  cut (c1 mod c0 == 0 \/ c1 mod c0 ~= 0).
  intros.
  destruct H1.
  rewrite <- pos_div_round_down_mod_z.
  rewrite mul_lt_mono_pos_l with (p := c0).
  rewrite <- div_exact in H1.
  rewrite <- H1.
  rewrite mul_comm.
  assumption.
  apply lt_neq_ooo.
  assumption.
  assumption.
  assumption.
  assumption.
  rewrite pos_div_round_down_mod_nz.
  cut (c1 < c0 * (c1/c0 + 1)).
  intros.
  rewrite mul_lt_mono_pos_l with (p := c0).
  rewrite mul_comm.
  apply lt_trans with (n := x * c0) (m := c1) (p := c0 * (c1/c0 + 1)).
  assumption.
  assumption.
  assumption.
  rewrite add_1_r.
  apply mul_succ_div_gt.
  assumption.
  assumption.
  assumption.
  apply eq_decidable.
Qed.

(* lt145 *)
(* rewrite(c1 < x * c0, fold(c1 / c0) < x, c0 > 0, "lt145") *)
Lemma lt145 : forall x c0 c1, c0 > 0 -> c1 < x * c0 -> (c1 / c0) < x.
Proof.
  intros x c0 c1 H.
  rewrite mul_lt_mono_pos_l with (n := c1/c0) (m := x) (p := c0).
  cut (c0 * (c1/c0) <= c1).
  rewrite mul_comm with (n := x) (m := c0).
  apply le_lt_trans.
  apply mul_div_le.
  apply lt_neq_ooo.
  assumption.
  assumption.
Qed.

(* lt148 *)
(* rewrite(x / c0 < c1, x < c1 * c0, c0 > 0, "lt148") *)
Lemma lt148 : forall x c0 c1, c0 > 0 -> x / c0 < c1 -> x < c1 * c0.
Proof.
  intros.
  rewrite <- le_succ_l in H0.
  cut (c0 * S (x/c0) <= c0 * c1).
  cut (x < c0 * S (x/c0)).
  intros.
  apply lt_le_trans with (m := c0 * S (x/c0)).
  assumption.
  rewrite mul_comm with (n := c1) (m := c0).
  assumption.
  apply mul_succ_div_gt.
  assumption.
  apply mul_le_mono_pos_l.
  assumption.
  assumption.
Qed.

(* lt149 *)
(* rewrite(c0 < x / c1, fold((c0 + 1) * c1 - 1) < x, c1 > 0, "lt149") *)
Lemma lt149 : forall x c0 c1, c1 > 0 -> c0 < x / c1 -> ((c0 + 1) * c1 - 1) < x.
Proof.
  intros.
  cut ((c0 + 1) * c1 - 1 < x \/ (c0 + 1) * c1 - 1 >= x).
  intros.
  destruct H1.
  assumption.
  apply div_le_mono with (c := c1) in H1.
  rewrite mul_add_distr_r in H1.
  rewrite mul_1_l in H1.
  cut (c0 * c1 mod c1 == 0).
  intros.
  rewrite <- pos_div_round_down_mod_z in H1.
  rewrite div_mul in H1.
  rewrite le_ngt in H1.
  unfold not in H1.
  contradiction H1.
  apply lt_neq_ooo.
  assumption.
  assumption.
  apply mod_mul.
  apply lt_neq_ooo.
  assumption.
  apply mod_mul.
  apply lt_neq_ooo.
  assumption.
  assumption.
  apply lt_ge_cases.
Qed.

(* lt229 *)
(* rewrite(x * c0 < y * c0 + c1, x < y + fold((c1 + c0 - 1)/c0), c0 > 0, "lt229") *)
Lemma lt229 : forall x y c0 c1, c0 > 0 -> x * c0 < y * c0 + c1 -> x < y + (c1 + c0 - 1)/c0.
Proof.
  intros.
  cut (c0 ~= 0).
  intros H00.
  cut (c1 mod c0 == 0 \/ c1 mod c0 ~= 0).
  intros.
  destruct H1.
  rewrite <- div_exact in H1.
  rewrite H1 in H0.
  rewrite <- pos_div_round_down_mod_z.
  rewrite mul_lt_mono_pos_l with (p := c0).
  rewrite mul_add_distr_l.
  rewrite mul_comm with (n := c0) (m := x).
  rewrite mul_comm with (n := c0) (m := y).
  assumption.
  assumption.
  assumption.
  apply div_exact.
  assumption.
  assumption.
  assumption.
  rewrite <- lt_sub_lt_add_l in H0.
  cut (c1 < c0*(c1/c0 + 1)).
  intros.
  rewrite <- lt_sub_lt_add_l.
  rewrite mul_lt_mono_pos_l with (p := c0).
  rewrite mul_sub_distr_l.
  rewrite pos_div_round_down_mod_nz.
  rewrite mul_comm with (n := c0) (m := x).
  rewrite mul_comm with (n := c0) (m := y).
  apply lt_trans with (n := x * c0 - y * c0) (m := c1) (p := c0 * (c1 / c0 + 1)).
  assumption.
  assumption.
  assumption.
  assumption.
  assumption.
  rewrite add_1_r.
  apply mul_succ_div_gt.
  assumption.
  apply eq_decidable.
  apply lt_neq_ooo.
  assumption.
Qed.

(* lt230 *)
(* rewrite(x * c0 + c1 < y * c0, x + fold(c1/c0) < y, c0 > 0, "lt230") *)
Lemma lt230 : forall x y c0 c1, c0 > 0 -> x * c0 + c1 < y * c0 -> x + (c1/c0) < y.
Proof.
  intros.
  cut (c0 ~= 0).
  intros.
  rewrite mul_lt_mono_pos_l with (p := c0).
  rewrite mul_add_distr_l.
  rewrite add_comm.
  rewrite lt_add_lt_sub_r.
  rewrite add_comm in H0.
  rewrite lt_add_lt_sub_r in H0.
  cut (c0 * (c1/ c0) <= c1).
  intro.
  apply le_lt_trans with (m := c1).
  apply mul_div_le.
  assumption.
  rewrite mul_comm.
  rewrite mul_comm with (n := c0) (m := x).
  assumption.
  apply mul_div_le.
  assumption.
  assumption.
  apply lt_neq_ooo.
  assumption.
Qed.

(* lt292 *)
(* rewrite((x + c1)/c0 < (x + c2)/c0, false, c0 > 0 && c1 >= c2, "lt292") *)
Lemma lt292 : forall x c0 c1 c2, c0 > 0 -> c1 >= c2 -> ~((x + c1) / c0 < (x + c2) / c0).
Proof.
  intros.
  rewrite nlt_ge.
  apply div_le_mono.
  assumption.
  apply add_le_mono_l.
  assumption.
Qed.

(* lt295 *)
(* rewrite(x/c0 < (x + c2)/c0, false, c0 > 0 && 0 >= c2, "lt295") *)
Lemma lt295 : forall x c0 c2, c0 > 0 -> 0 >= c2 -> ~ (x/c0 < (x + c2)/c0).
Proof.
  intros.
  rewrite <- add_0_r with (n := x) at 1.
  apply lt292.
  assumption.
  assumption.
Qed.

(* lt298 *)
(* rewrite((x + c1)/c0 < x/c0, false, c0 > 0 && c1 >= 0, "lt298") *)
Lemma lt298 : forall x c0 c1, c0 > 0 -> c1 >= 0 -> ~((x + c1) / c0 < x / c0).
Proof.
  intros.
  rewrite <- add_0_r with (n := x) at 2.
  apply lt292.
  assumption.
  assumption.
Qed.

(* lt302 *)
(* rewrite((x + c1)/c0 < x/c0 + c2, false, c0 > 0 && c1 >= c2 * c0, "lt302") *)
Lemma lt302 : forall x c0 c1 c2, c0 > 0 -> c1 >= c2 * c0 -> ~((x + c1) / c0 < x / c0 + c2).
Proof.
  intros.
  rewrite <- le_ngt.
  rewrite <- div_add.
  apply div_le_mono.
  assumption.
  apply add_le_mono_l.
  assumption.
  apply lt_neq_ooo.
  assumption.
Qed.

(* lt306 *)
(* rewrite((x + c1)/c0 < (min(x/c0, y) + c2), false, c0 > 0 && c1 >= c2 * c0, "lt306") *)
Lemma lt306 : forall x y c0 c1 c2, c0 > 0 -> c1 >= (c2 * c0) -> ~((x + c1) / c0 < (min (x / c0) y) + c2).
Proof.
  intros.
  rewrite <- le_ngt.
  rewrite le_add_le_sub_r.
  rewrite min_le_iff.
  cut (x / c0 <= (x + c1) / c0 - c2).
  auto.
  rewrite <- le_add_le_sub_r.
  rewrite <- div_add.
  apply div_le_mono.
  assumption.
  apply add_le_mono_l.
  assumption.
  apply lt_neq_ooo.
  assumption.
Qed.

(* lt308 *)
(* rewrite((x + c1)/c0 < min((x + c2)/c0, y), false, c0 > 0 && c1 >= c2, "lt308") *)
Lemma lt308 : forall x y c0 c1 c2, c0 > 0 -> c1 >= c2 -> ~((x + c1) / c0 < (min ((x + c2) / c0) y)).
Proof.
  intros.
  rewrite <- le_ngt.
  rewrite min_le_iff.
  cut ((x + c2) / c0 <= (x + c1) / c0).
  auto.
  apply div_le_mono.
  assumption.
  apply add_le_mono_l.
  assumption.
Qed.

(* lt310 *)
(* rewrite((x + c1)/c0 < min(x/c0, y), false, c0 > 0 && c1 >= 0, "lt310") *)
Lemma lt310 : forall x y c0 c1, c0 > 0 -> c1 >= 0 -> ~((x + c1) / c0 < (min (x / c0) y)).
Proof.
  intros.
  rewrite <- le_ngt.
  cut (x/c0 <= (x + c1)/c0).
  intros.
  rewrite min_le_iff.
  auto.
  rewrite <- add_0_r with (n := x) at 1.
  apply div_le_mono.
  assumption.
  apply add_le_mono_l.
  assumption.
Qed.

(* lt313 *)
(* rewrite((x + c1)/c0 < (min(y, x/c0) + c2), false, c0 > 0 && c1 >= c2 * c0, "lt313") *)
Lemma lt313 : forall x y c0 c1 c2, c0 > 0 -> c1 >= c2 * c0 -> ~((x + c1) / c0 < (min y (x / c0)) + c2).
Proof.
  intros.
  rewrite min_comm.
  apply lt306.
  assumption.
  assumption.
Qed.

(* lt315 *)
(* rewrite((x + c1)/c0 < min(y, (x + c2)/c0), false, c0 > 0 && c1 >= c2, "lt315") *)
Lemma lt315 : forall x y c0 c1 c2, c0 > 0 -> c1 >= c2 -> ~((x + c1) / c0 < (min y ((x + c2)/c0))).
Proof.
  intros.
  rewrite min_comm.
  apply lt308.
  assumption.
  assumption.
Qed.

(* lt317 *)
(* rewrite((x + c1)/c0 < min(y, x/c0), false, c0 > 0 && c1 >= 0, "lt317") *)
Lemma lt317 : forall x y c0 c1, c0 > 0 -> c1 >= 0 -> ~((x + c1) / c0 < (min y (x / c0))).
Proof.
  intros.
  rewrite min_comm.
  apply lt310.
  assumption.
  assumption.
Qed.

(* lt320 *)
(* rewrite(max((x + c2)/c0, y) < (x + c1)/c0, false, c0 > 0 && c2 >= c1, "lt320") *)
Lemma lt320 : forall x y c0 c1 c2, c0 > 0 -> c2 >= c1 -> ~((max ((x + c2)/c0) y) < (x + c1) / c0).
Proof.
  intros.
  rewrite <- le_ngt.
  rewrite max_le_iff.
  cut ((x + c1) / c0 <= (x + c2) / c0).
  auto.
  apply div_le_mono.
  assumption.
  apply add_le_mono_l.
  assumption.
Qed.

(* lt322 *)
(* rewrite(max(x/c0, y) < (x + c1)/c0, false, c0 > 0 && 0 >= c1, "lt322") *)
Lemma lt322 : forall x y c0 c1, c0 > 0 -> 0 >= c1 -> ~((max (x / c0) y) < (x + c1) / c0).
Proof.
  intros.
  rewrite <- le_ngt.
  rewrite max_le_iff.
  cut ((x + c1)/c0 <= x/c0).
  auto.
  rewrite <- add_0_r with (n := x) at 2.
  apply div_le_mono.
  assumption.
  apply add_le_mono_l.
  assumption.
Qed.

(* lt324 *)
(* rewrite(max(y, (x + c2)/c0) < (x + c1)/c0, false, c0 > 0 && c2 >= c1, "lt324") *)
Lemma lt324 : forall x y c0 c1 c2, c0 > 0 -> c2 >= c1 -> ~((max y ((x + c2)/c0)) < (x + c1) / c0).
Proof.
  intros.
  rewrite max_comm.
  apply lt320.
  assumption.
  assumption.
Qed.

(* lt326 *)
(* rewrite(max(y, x/c0) < (x + c1)/c0, false, c0 > 0 && 0 >= c1, "lt326") *)
Lemma lt326 : forall x y c0 c1, c0 > 0 -> 0 >= c1 -> ~((max y (x/c0)) < (x + c1)/c0).
Proof.
  intros.
  rewrite max_comm.
  apply lt322.
  assumption.
  assumption.
Qed.

(* lt330 *)
(* rewrite(max((x + c2)/c0, y) < x/c0 + c1, false, c0 > 0 && c2 >= c1 * c0, "lt330") *)
Lemma lt330 : forall x y c0 c1 c2, c0 > 0 -> c2 >= (c1 * c0) -> ~((max ((x + c2) / c0) y) < ((x / c0) + c1)).
Proof.
  intros.
  rewrite <- le_ngt.
  rewrite max_le_iff.
  cut (x / c0 + c1 <= (x + c2) / c0).
  auto.
  rewrite <- div_add.
  apply div_le_mono.
  assumption.
  apply add_le_mono_l.
  assumption.
  apply lt_neq_ooo.
  assumption.
Qed.

(* lt332 *)
(* rewrite(max(y, (x + c2)/c0) < x/c0 + c1, false, c0 > 0 && c2 >= c1 * c0, "lt332") *)
Lemma lt332 : forall x y c0 c1 c2, c0 > 0 -> c2 >= c1 * c0 -> ~((max y ((x + c2)/c0)) < x/c0 + c1).
Proof.
  intros.
  rewrite max_comm.
  apply lt330.
  assumption.
  assumption.
Qed.

(* lt336 *)
(* rewrite(x/c0 < min((x + c2)/c0, y), false, c0 > 0 && c2 < 0, "lt336") *)
Lemma lt336 : forall x y c0 c2, c0 > 0 -> c2 < 0 -> ~(x/c0 < (min ((x + c2)/c0) y)).
Proof.
  intros.
  rewrite <- le_ngt.
  rewrite <- add_0_r with (n := x) at 2.
  cut ((x + c2) / c0 <= (x + 0)/c0).
  intros.
  rewrite min_le_iff with (n := (x+c2)/c0) (m := y) (p := (x + 0)/c0).
  auto.
  rewrite add_0_r.
  rewrite le_ngt.
  apply lt295.
  assumption.
  apply lt_le_incl.
  assumption.
Qed.

(* lt338 *)
(* rewrite(x/c0 < min(y, (x + c2)/c0), false, c0 > 0 && c2 < 0, "lt338") *)
Lemma lt338 : forall x y c0 c2, c0 > 0 -> c2 < 0 -> ~(x/c0 < (min y ((x + c2)/c0))).
Proof.
  intros.
  rewrite min_comm.
  apply lt336.
  assumption.
  assumption.
Qed.

(* lt340 *)
(* rewrite(max((x + c2)/c0, y) < x/c0, false, c0 > 0 && c2 >= 0, "lt340") *)
Lemma lt340 : forall x y c0 c2, c0 > 0 -> c2 >= 0 -> ~((max ((x + c2) / c0) y) < (x / c0)).
Proof.
  intros.
  rewrite <- le_ngt.
  rewrite max_le_iff.
  cut (x/c0 <= (x + c2)/c0).
  intros.
  auto.
  rewrite le_ngt.
  apply lt298.
  assumption.
  assumption.
Qed.

(* lt342 *)
(* rewrite(max(y, (x + c2)/c0) < x/c0, false, c0 > 0 && c2 >= 0, "lt342") *)
Lemma lt342 : forall x y c0 c2, c0 > 0 -> c2 >= 0 -> ~((max y ((x + c2)/c0)) < x/c0).
Proof.
  intros.
  rewrite max_comm.
  apply lt340.
  assumption.
  assumption.
Qed.

(* lt352 *)
(* rewrite(ramp(x * c3 + c2, c1) < broadcast(z * c0),
                      broadcast(x * fold(c3/c0) + fold(c2/c0) < z, lanes),
                      c0 > 0 && (c3 % c0 == 0) &&
                      (c2 % c0) + c1 * (lanes - 1) < c0 &&
                      (c2 % c0) + c1 * (lanes - 1) >= 0, "lt352") *)
Lemma lt352 : forall x z c0 c1 c2 c3 lanes, c0 > 0 -> c3 mod c0 == 0 -> (c2 mod c0) + c1 * lanes < c0 -> (c2 mod c0) + c1 * lanes >= 0 -> (x * c3 + c2 + c1 * lanes) < z * c0 -> x * (c3/c0) + c2/c0 < z.
Proof.
  intros.
  cut (c0 ~= 0).
  cut (c2 == c0 * (c2/c0) + c2 mod c0).
  cut (x*(c3/c0) + c2/c0 ~= z).
  intros.
  apply lt_le_incl in H3.
  apply div_le_mono with (c := c0) in H3.
  rewrite div_mul in H3.
  intros.
  rewrite H5 in H3.
  rewrite add_assoc in H3.
  rewrite add_comm with (n := x * c3) in H3.
  rewrite mul_comm with (n := c0) in H3.
  rewrite <- !add_assoc in H3.
  rewrite div_add_l in H3.
  cut (x * c3 == c0 * ((x * c3)/c0)).
  intros.
  rewrite H7 in H3.
  rewrite mul_comm with (n := c0) (m := (x * c3)/c0) in H3.
  rewrite div_add_l in H3.
  rewrite add_comm in H3.
  cut ((c2 mod c0 + c1 * lanes)/c0 == 0).
  intros.
  rewrite H8 in H3.
  rewrite add_0_r in H3.
  rewrite div_mul_mod_z in H3.
  rewrite le_neq.
  auto.
  assumption.
  assumption.
  apply div_small.
  auto.
  assumption.
  rewrite div_exact.
  rewrite <- mul_mod_idemp_r.
  rewrite H0.
  rewrite mul_0_r.
  rewrite mod_0_l.
  reflexivity.
  assumption.
  assumption.
  assumption.
  assumption.
  assumption.
  assumption.
  unfold not.
  intros.
  rewrite <- H4 in H3.
  cut (c2 == c0 * (c2 / c0) + c2 mod c0).
  intros.
  rewrite H5 in H3 at 1.
  rewrite mul_add_distr_r in H3.
  rewrite <- mul_assoc in H3.
  rewrite mul_comm with (n := c3/c0) in H3.
  rewrite <- div_exact in H0.
  rewrite <- H0 in H3.
  rewrite mul_comm with (n := c2/c0) in H3.
  rewrite <- !add_assoc in H3.
  rewrite 2lt_add_lt_sub_l in H3.
  rewrite add_comm with (n := x * c3) in H3.
  rewrite <- add_sub_assoc in H3.
  rewrite sub_diag in H3.
  rewrite add_0_r in H3.
  rewrite sub_diag in H3.
  apply le_lt_trans with (n := 0) in H3.
  apply lt_neq_ooo in H3.
  unfold not in H3.
  contradiction H3.
  reflexivity.
  assumption.
  apply lt_neq_ooo.
  assumption.
  rewrite add_comm.
  rewrite <- sub_move_r.
  apply eq_sym.
  apply mod_eq.
  apply lt_neq_ooo.
  assumption.
  rewrite add_comm.
  rewrite <- sub_move_r.
  apply eq_sym.
  apply mod_eq.
  apply lt_neq_ooo.
  assumption.
  apply lt_neq_ooo.
  assumption.
Qed.

(* lt358 *)
(* rewrite(ramp(x * c3, c1) < broadcast(z * c0),
                      broadcast(x * fold(c3/c0) < z, lanes),
                      c0 > 0 && (c3 % c0 == 0) &&
                      c1 * (lanes - 1) < c0 &&
                      c1 * (lanes - 1) >= 0, "lt358") *)
Lemma lt358 : forall x z c0 c1 c3 lanes, c0 > 0 -> c3 mod c0 == 0 -> c1 * lanes < c0 -> c1 * lanes >= 0 -> x*c3 + c1 * lanes < z * c0 -> x * (c3 / c0) < z.
Proof.
  intros.
  cut (c0 ~= 0).
  intros H000.
  cut (x*(c3/c0) ~= z).
  intros H00.
  rewrite le_neq in H3.
  destruct H3.
  apply div_le_mono with (c := c0) in H3.
  rewrite div_mul in H3.
  cut (x*c3 == c0 * ((x*c3)/c0)).
  intros.
  rewrite H5 in H3.
  rewrite mul_comm with (n := c0) in H3.
  rewrite div_add_l in H3.
  cut ((c1 * lanes)/c0 == 0).
  intros.
  rewrite H6 in H3.
  rewrite add_0_r in H3.
  rewrite div_mul_mod_z in H3.
  rewrite le_neq.
  auto.
  assumption.
  assumption.
  rewrite div_small.
  reflexivity.
  auto.
  assumption.
  rewrite div_exact.
  rewrite <- mul_mod_idemp_r.
  rewrite H0.
  rewrite mul_0_r.
  rewrite mod_0_l.
  reflexivity.
  assumption.
  assumption.
  assumption.
  assumption.
  assumption.
  unfold not.
  intros.
  rewrite <- H4 in H3.
  rewrite <- mul_assoc in H3.
  rewrite mul_comm with (n := c3/c0) in H3.
  cut (c3 == c0 * (c3 / c0)).
  intros.
  rewrite <- H5 in H3.
  rewrite lt_add_lt_sub_l in H3.
  rewrite sub_diag in H3.
  apply le_lt_trans with (n := 0) in H3.
  apply lt_neq_ooo in H3.
  unfold not in H3.
  contradiction H3.
  reflexivity.
  assumption.
  rewrite div_exact.
  assumption.
  assumption.
  apply lt_neq_ooo.
  assumption.
Qed.


(* max193 *)
(* rewrite(max(x / c0, y / c0), max(x, y) / c0, c0 > 0, "max193") *)
Lemma max193 : forall x y c0, c0 > 0 -> (max (x/c0) (y/c0)) == (max x y)/c0.
Proof.
  intros.
  cut (y <= x \/ x <= y).
  intros.
  destruct H0.
  rewrite max_l with (x := x) (y := y).
  cut (y/c0 <= x/c0).
  intros.
  rewrite max_l.
  reflexivity.
  assumption.
  apply div_le_mono.
  assumption.
  assumption.
  assumption.
  rewrite max_comm with (n := x) (m := y).
  rewrite max_l with (x := y) (y := x).
  cut (x/c0 <= y/c0).
  intros.
  rewrite max_comm.
  rewrite max_l.
  reflexivity.
  assumption.
  apply div_le_mono.
  assumption.
  assumption.
  assumption.
  apply le_ge_cases.
Qed.

(* max194 *)
(* rewrite(min(y, (x + c2)/c0) < x/c0, true, c0 > 0 && c2 + c0 <= 0, "lt343") *)
Lemma max194 : forall x y c0, c0 < 0 -> (max (x/c0) (y/c0)) == (min x y)/c0.
Proof.
  intros.
  cut (x <= y \/ y <= x).
  intros.
  destruct H0.
  cut (x/c0 >= y/c0).
  intros.
  rewrite max_l.
  rewrite min_l.
  reflexivity.
  assumption.
  apply neg_div_antimonotone.
  assumption.
  assumption.
  apply neg_div_antimonotone.
  assumption.
  assumption.
  cut (y/c0 >= x/c0).
  intros.
  rewrite max_r.
  rewrite min_r.
  reflexivity.
  assumption.
  assumption.
  apply neg_div_antimonotone.
  assumption.
  assumption.
  apply le_ge_cases.
Qed.

(* max201 *)
(* rewrite(max(x / c0, y / c0 + c1), max(x, y + fold(c1 * c0)) / c0, c0 > 0 && !overflows(c1 * c0), "max201") *)
Lemma max201 : forall x y c0 c1, c0 > 0 -> (max (x/c0) ((y/c0) + c1)) == ((max x (y + c1 * c0)) / c0).
Proof.
  intros.
  cut (y/c0 + c1 == (y + c1 * c0)/c0).
  intros.
  cut ((max (x / c0) (y / c0 + c1)) == (max (x/c0) ((y + c1 * c0) / c0) )).
  intros.
  rewrite H1.
  apply max193 with (x := x) (y := (y + c1 * c0)) (c0 := c0).
  assumption.
  rewrite max_comm.
  cut ((max (y / c0 + c1) (x / c0)) == (max ((y + c1 * c0)/c0) (x / c0))).
  intros.
  rewrite H1.
  rewrite max_comm.
  reflexivity.
  apply max_proper.
  assumption.
  apply eq_sym.
  apply div_add.
  apply lt_neq_ooo.
  assumption.
Qed.

(* max202 *)
(* rewrite(max(x / c0, y / c0 + c1), min(x, y + fold(c1 * c0)) / c0, c0 < 0 && !overflows(c1 * c0), "max202") *)
Lemma max202 : forall x y c0 c1, c0 < 0 -> (max (x / c0) (y / c0 + c1)) == (min x (y + c1 * c0)) / c0.
Proof.
  intros.
  cut (y/c0 + c1 == (y + c1 * c0)/c0).
  intros.
  rewrite max_comm.
  cut ((max (y/c0 + c1) (x/c0)) == (max ((y + c1 * c0)/c0) (x/c0))).
  intros.
  rewrite H1.
  rewrite max_comm.
  apply max194.
  assumption.
  apply max_proper.
  assumption.
  apply eq_sym.
  apply div_add.
  apply neq_sym.
  apply lt_neq_ooo.
  assumption.
Qed.

(* min196 *)
(* rewrite(min(x / c0, y / c0), min(x, y) / c0, c0 > 0, "min196") *)
Lemma min196 : forall x y c0, c0 > 0 -> (min (x / c0) (y / c0)) == (min x y) / c0.
Proof.
  intros.
  cut (y <= x \/ x <= y).
  intros.
  destruct H0.
  cut (y/c0 <= x/c0).
  intros.
  rewrite min_r.
  rewrite min_r.
  reflexivity.
  assumption.
  assumption.
  apply div_le_mono.
  assumption.
  assumption.
  cut (x/c0 <= y/c0).
  intros.
  rewrite min_l.
  rewrite min_l.
  reflexivity.
  assumption.
  assumption.
  apply div_le_mono.
  assumption.
  assumption.
  apply le_ge_cases.
Qed.

(* min197 *)
(* rewrite(min(x / c0, y / c0), max(x, y) / c0, c0 < 0, "min197") *)
Lemma min197 : forall x y c0, c0 < 0 -> (min (x / c0) (y / c0)) == (max x y) / c0.
Proof.
  intros.
  cut (x <= y \/ y <= x).
  intros.
  destruct H0.
  cut (x/c0 >= y/c0).
  intros.
  rewrite max_r.
  rewrite min_r.
  reflexivity.
  assumption.
  assumption.
  apply neg_div_antimonotone.
  assumption.
  assumption.
  cut (y/c0 >= x/c0).
  intros.
  rewrite max_l.
  rewrite min_l.
  reflexivity.
  assumption.
  assumption.
  apply neg_div_antimonotone.
  assumption.
  assumption.
  apply le_ge_cases.
Qed.

(* min204 *)
(* rewrite(min(x / c0, y / c0 + c1), min(x, y + fold(c1 * c0)) / c0, c0 > 0 && !overflows(c1 * c0), "min204") *)
Lemma min204 : forall x y c0 c1, c0 > 0 -> (min (x / c0) (y / c0 + c1)) == (min x (y + c1 * c0)) / c0.
Proof.
  intros.
  cut (y/c0 + c1 == (y + c1 * c0)/c0).
  intros.
  cut ((min (x/c0) (y/c0 + c1)) == (min (x/c0) ((y + c1 * c0)/c0))).
  intros.
  rewrite H1.
  apply min196.
  assumption.
  rewrite min_comm.
  cut ((min (y/c0 + c1) (x/c0)) == (min ((y + c1 * c0) / c0) (x/c0))).
  intros.
  rewrite H1.
  rewrite min_comm.
  reflexivity.
  apply min_proper.
  assumption.
  apply eq_sym.
  apply div_add.
  apply lt_neq_ooo.
  assumption.
Qed.

(* min205 *)
(* rewrite(min(x / c0, y / c0 + c1), max(x, y + fold(c1 * c0)) / c0, c0 < 0 && !overflows(c1 * c0), "min205") *)
Lemma min205 : forall x y c0 c1, c0 < 0 -> (min (x / c0) (y / c0 + c1)) == (max x (y + c1 * c0)) / c0.
Proof.
  intros.
  cut (y/c0 + c1 == (y + c1 * c0)/c0).
  intros.
  rewrite max_comm.
  cut ((min (x/c0) (y/c0 + c1)) == (min ((y + c1 * c0)/c0) (x/c0))).
  intros.
  rewrite H1.
  rewrite max_comm.
  rewrite min_comm.
  apply min197.
  assumption.
  rewrite min_comm.
  apply min_proper.
  assumption.
  apply eq_sym.
  apply div_add.
  apply neq_sym.
  apply lt_neq_ooo.
  assumption.
Qed.

(* mod81 *)
(* rewrite((x * c0) % c1, (x * fold(c0 % c1)) % c1, c1 > 0 && (c0 >= c1 || c0 < 0), "mod81") *)
Lemma mod81 : forall x c0 c1,(x * c0) mod c1 == (x * (c0 mod c1)) mod c1.
Proof.
  intros.
  cut (c1 == 0 \/ c1 ~= 0).
  intros H0.
  destruct H0.
  rewrite H.
  rewrite !mod_by_zero.
  reflexivity.
  rewrite <- mul_mod_idemp_r.
  reflexivity.
  assumption.
  apply eq_decidable.
Qed.

(* mod82 *)
(* rewrite((x + c0) % c1, (x + fold(c0 % c1)) % c1, c1 > 0 && (c0 >= c1 || c0 < 0), "mod82") *)
Lemma mod82 : forall x c0 c1, (x + c0) mod c1 == (x + (c0 mod c1)) mod c1.
Proof.
  intros.
  cut (c1 == 0 \/ c1 ~= 0).
  intros H0.
  destruct H0.
  rewrite H.
  rewrite !mod_by_zero.
  reflexivity.
  rewrite <- add_mod_idemp_r.
  reflexivity.
  assumption.
  apply eq_decidable.
Qed.

(* mod83 *)
(* rewrite((x * c0) % c1, (x % fold(c1/c0)) * c0, c0 > 0 && c1 % c0 == 0, "mod83") *)
Lemma mod83 : forall x c0 c1, c0 > 0 -> c1 mod c0 == 0 -> (x * c0) mod c1 == (x mod (c1/c0))*c0.
Proof.
  intros.
  cut (c1 == 0 \/ c1 ~= 0).
  intros H1.
  destruct H1.
  rewrite H1.
  rewrite div_0_l.
  rewrite !mod_by_zero.
  rewrite mul_0_l.
  reflexivity.
  apply lt_neq_ooo.
  assumption.
  rewrite <- mul_mod_distr_r.
  rewrite mul_comm with (n := c1/c0).
  rewrite <- div_exact in H0.
  rewrite <- H0.
  reflexivity.
  apply lt_neq_ooo.
  assumption.
  rewrite <- div_exact in H0.
  rewrite <- mul_cancel_l with (p := c0).
  rewrite <- H0.
  rewrite mul_0_r.
  assumption.
  apply lt_neq_ooo.
  assumption.
  apply lt_neq_ooo.
  assumption.
  assumption.
  apply eq_decidable.
Qed.

(* mod84 *)
(* rewrite((x * c0 + y) % c1, y % c1, c0 % c1 == 0, "mod84") *)
Lemma mod84 : forall x y c0 c1, c0 mod c1 == 0 -> (x * c0 + y) mod c1 == y mod c1.
Proof.
  intros.
  cut (c1 == 0 \/ c1 ~= 0).
  intros.
  destruct H0.
  rewrite H0.
  rewrite !mod_by_zero.
  reflexivity.
  rewrite <- add_mod_idemp_l.
  rewrite mul_mod.
  rewrite H.
  rewrite mul_0_r.
  rewrite mod_0_l.
  rewrite add_0_l.
  reflexivity.
  assumption.
  assumption.
  assumption.
  apply eq_decidable.
Qed.

(* mod85 *)
(* rewrite((y + x * c0) % c1, y % c1, c0 % c1 == 0, "mod85") *)
Lemma mod85 : forall x y c0 c1, c0 mod c1 == 0 -> (y + x * c0) mod c1 == y mod c1.
Proof.
  intros.
  rewrite add_comm.
  apply mod84.
  assumption.
Qed.

(* mod86 *)
(* rewrite((x * c0 - y) % c1, (-y) % c1, c0 % c1 == 0, "mod86") *)
Lemma mod86 : forall x y c0 c1, c0 mod c1 == 0 -> (x * c0 - y) mod c1 == (- y) mod c1.
Proof.
  intros.
  rewrite <- add_opp_r.
  apply mod84.
  assumption.
Qed.

(* mod87 *)
(* rewrite((y - x * c0) % c1, y % c1, c0 % c1 == 0, "mod87") *)
Lemma mod87 : forall x y c0 c1, c0 mod c1 == 0 -> (y - x * c0) mod c1 == y mod c1.
Proof.
  intros.
  rewrite <- add_opp_r.
  rewrite <- mul_opp_l.
  apply mod85.
  assumption.
Qed.

(* mod90 *)
(* rewrite(ramp(x, c0) % broadcast(c1), broadcast(x, lanes) % c1, c0 % c1 == 0, "mod90") *)
Lemma mod90 : forall x c0 c1 lanes, c0 mod c1 == 0 -> (x + c0 * lanes) mod c1 == x mod c1.
Proof.
  intros.
  rewrite mul_comm.
  apply mod85.
  assumption.
Qed.

(* mod94 *)
(* rewrite(ramp(x * c0, c2) % broadcast(c1), (ramp(x * fold(c0 % c1), fold(c2 % c1), lanes) % c1), c1 > 0 && (c0 >= c1 || c0 < 0), "mod94") *)
Lemma mod94 : forall x c0 c1 c2 lanes, (x * c0 + c2 * lanes) mod c1 == (x * (c0 mod c1) + (c2 mod c1) * lanes) mod c1.
Proof.
  intros.
  cut (c1 == 0 \/ c1 ~= 0).
  intros H.
  destruct H.
  rewrite H.
  rewrite !mod_by_zero.
  reflexivity.
  rewrite add_mod.
  rewrite <- mul_mod_idemp_r with (a := x) (b := c0) (n := c1).
  rewrite <- mul_mod_idemp_l with (a := c2) (b := lanes) (n := c1).
  rewrite <- add_mod.
  reflexivity.
  assumption.
  assumption.
  assumption.
  assumption.
  apply eq_decidable.
Qed.
  

(* mod95 *)
(* rewrite(ramp(x + c0, c2) % broadcast(c1), (ramp(x + fold(c0 % c1), fold(c2 % c1), lanes) % c1), c1 > 0 && (c0 >= c1 || c0 < 0), "mod95") *)
Lemma mod95 : forall x c0 c1 c2 lanes, (x + c0 + c2 * lanes) mod c1 == (x + (c0 mod c1) + (c2 mod c1)*lanes) mod c1.
Proof.
  intros.
  cut (c1 == 0 \/ c1 ~= 0).
  intro H.
  destruct H.
  rewrite H.
  rewrite !mod_by_zero.
  reflexivity.
  rewrite <- add_mod_idemp_r.
  rewrite <- mul_mod_idemp_l.
  rewrite <- add_mod_idemp_l.
  rewrite <- add_mod_idemp_r with (a := x) (b := c0).
  rewrite add_mod_idemp_l.
  rewrite add_mod_idemp_r.
  reflexivity.
  assumption.
  assumption.
  assumption.
  assumption.
  assumption.
  assumption.
  apply eq_decidable.
Qed.

(* mod96 *)
(* rewrite(ramp(x * c0 + y, c2) % broadcast(c1), ramp(y, fold(c2 % c1), lanes) % c1, c0 % c1 == 0, "mod96") *)
Lemma mod96 : forall x y c0 c1 c2 lanes, c0 mod c1 == 0 -> ((x * c0 + y) + c2 * lanes) mod c1 == (y + c2 mod c1 * lanes) mod c1.
Proof.
  intros.
  cut (c1 == 0 \/ c1 ~= 0).
  intros H0.
  destruct H0.
  rewrite H0.
  rewrite !mod_by_zero.
  reflexivity.
  rewrite <- add_assoc.
  rewrite mod84.
  rewrite add_mod.
  rewrite <- mul_mod_idemp_l.
  rewrite <- add_mod.
  reflexivity.
  assumption.
  assumption.
  assumption.
  assumption.
  apply eq_decidable.
Qed.

(* mod97 *)
(* rewrite(ramp(y + x * c0, c2) % broadcast(c1), ramp(y, fold(c2 % c1), lanes) % c1, c0 % c1 == 0, "mod97") *)
Lemma mod97 : forall x y c0 c1 c2 lanes, c0 mod c1 == 0 -> (y + x * c0 + c2 * lanes) mod c1 == (y + (c2 mod c1) * lanes) mod c1.
Proof.
  intros.
  rewrite add_comm with (n := y) (m := x * c0).
  apply mod96.
  assumption.
Qed.

(* sub245 *)
(* rewrite(c0 - (c1 - x)/c2, (fold(c0*c2 - c1 + c2 - 1) + x)/c2, c2 > 0, "sub245") *)
Lemma sub245 : forall x c0 c1 c2, c2 > 0 -> c0 - (c1 - x)/c2 == (c0*c2 - c1 + c2 - 1 + x)/c2.
Proof.
  intros.
  cut (c2 ~= 0).
  intros H1.
  rewrite <- !add_opp_r.
  rewrite <- !add_assoc.
  rewrite div_add_l with (a := c0) (b := c2).
  rewrite add_assoc with (n := c2).
  rewrite add_comm with (m := x).
  rewrite add_assoc.
  rewrite add_opp_r with (n := c2) (m := 1).
  rewrite <- opp_sub_distr.
  cut ((c1 - x) mod c2 == 0 \/ (c1 - x) mod c2 ~= 0).
  intros.
  destruct H0.
  rewrite add_sub_assoc.
  rewrite <- pos_div_round_down_mod_z.
  rewrite <- div_opp_l_z.
  rewrite add_opp_r.
  reflexivity.
  assumption.
  rewrite add_opp_r.
  assumption.
  assumption.
  apply mod_opp_l_z.
  assumption.
  assumption.
  rewrite add_sub_assoc.
  rewrite pos_div_round_down_mod_nz.
  rewrite div_opp_l_nz.
  rewrite sgn_pos.
  rewrite <- add_opp_r with (m := 1).
  rewrite <- !add_assoc.
  rewrite add_opp_diag_l.
  rewrite add_0_r.
  rewrite add_opp_r with (n := c1) (m := x).
  reflexivity.
  assumption.
  assumption.
  assumption.
  assumption.
  rewrite mod_opp_l_nz.
  rewrite abs_eq.
  cut (c2 - (c1 - x) mod c2 > 0).
  intros H2.
  apply lt_neq_ooo.
  assumption.
  rewrite lt_0_sub.
  apply mod_lt_denominator.
  assumption.
  apply lt_le_incl.
  assumption.
  assumption.
  assumption.
  apply eq_decidable.
  assumption.
  apply lt_neq_ooo.
  assumption.
Qed.

(* sub246 *)
(* rewrite(c0 - (x + c1)/c2, (fold(c0*c2 - c1 + c2 - 1) - x)/c2, c2 > 0, "sub246") *)
Lemma sub246 : forall x c0 c1 c2, c2 > 0 -> c0 - (x + c1)/c2 == (c0*c2 - c1 + c2 - 1 - x)/c2.
Proof.  
  intros.
  rewrite add_comm with (n := x) (m := c1).
  rewrite <- opp_involutive with (n := x).
  rewrite add_opp_r.
  rewrite sub245.
  rewrite opp_involutive.
  rewrite add_opp_r.
  reflexivity.
  assumption.
Qed.

(* sub247 *)
(* rewrite(x - (x + y)/c0, (x*fold(c0 - 1) - y + fold(c0 - 1))/c0, c0 > 0, "sub247") *)
Lemma sub247 : forall x y c0, c0 > 0 -> x - (x + y)/c0 == (x *(c0 - 1) - y + (c0 - 1))/c0.
Proof.
  intros.
  rewrite sub246.
  rewrite <- add_opp_r with (m := x).
  rewrite add_comm with (m := - x).
  rewrite <- !add_opp_r.
  rewrite !add_assoc.
  rewrite add_comm with (n := -x).
  rewrite <- mul_1_r with (n := -x).
  rewrite mul_opp_comm.
  rewrite <- mul_add_distr_l.
  reflexivity.
  assumption.
Qed.

(* sub248 *)
(* rewrite(x - (x - y)/c0, (x*fold(c0 - 1) + y + fold(c0 - 1))/c0, c0 > 0, "sub248") *)
Lemma sub248 : forall x y c0, c0 > 0 -> x - (x - y)/c0 == (x * (c0 - 1) + y + (c0 - 1))/c0.
Proof.
  intros.
  rewrite <- add_opp_r with (n := x) (m := y).
  rewrite <- opp_involutive with (n := y) at 2.
  rewrite add_opp_r with (m := (- y)).
  apply sub247.
  assumption.
Qed.

(* sub249 *)
(* rewrite(x - (y + x)/c0, (x*fold(c0 - 1) - y + fold(c0 - 1))/c0, c0 > 0, "sub249") *)
Lemma sub249 : forall x y c0, c0 > 0 -> x - (y + x)/c0 == (x * (c0 - 1) - y + (c0 - 1))/c0.
Proof.
  intros.
  rewrite add_comm.
  apply sub247.
  assumption.
Qed.

(* sub250 *)
(* rewrite(x - (y - x)/c0, (x*fold(c0 + 1) - y + fold(c0 - 1))/c0, c0 > 0, "sub250") *)
Lemma sub250 : forall x y c0, c0 > 0 -> x - (y - x)/c0 == (x * (c0 + 1) - y + (c0 - 1))/c0.
Proof.
  intros.
  rewrite sub245.
  rewrite add_comm with (m := x).
  rewrite <- !add_opp_r.
  rewrite !add_assoc.
  rewrite add_comm with (n := x) (m := x * c0).
  rewrite <- mul_1_r with (n := x) at 2.
  rewrite mul_add_distr_l.
  reflexivity.
  assumption.
Qed.

(* sub251 *)
(* rewrite((x + y)/c0 - x, (x*fold(1 - c0) + y)/c0, "sub251") *)
Lemma sub251 : forall x y c0, c0 ~= 0 -> (x + y)/c0 - x == (x*(1 - c0) + y)/c0.
Proof.
  intros.
  rewrite <- add_opp_r.
  rewrite <- div_add with (b := (- x)).
  rewrite <- add_assoc.
  rewrite add_comm with (n := y).
  rewrite add_assoc.
  rewrite <- mul_1_r with (n := x) at 1.
  rewrite mul_opp_comm.
  rewrite <- mul_add_distr_l.
  rewrite add_opp_r.
  reflexivity.
  assumption.
Qed.

(* sub252 *)
(* rewrite((y + x)/c0 - x, (y + x*fold(1 - c0))/c0, "sub252") *)
Lemma sub252 : forall x y c0, c0 ~= 0 -> (y + x)/c0 - x == (y + x*(1 - c0))/c0.
Proof.
  intros.
  rewrite add_comm.
  rewrite add_comm with (n := y) (m := x * (1 - c0)).
  apply sub251.
  assumption.
Qed.

(* sub253 *)
(* rewrite((x - y)/c0 - x, (x*fold(1 - c0) - y)/c0, "sub253") *)
Lemma sub253 : forall x y c0, c0 ~= 0 -> (x - y)/c0 - x == (x*(1 - c0) - y)/c0.
Proof.
  intros.
  rewrite <- add_opp_r with (n := x) (m := y).
  rewrite <- add_opp_r with (m := y).
  apply sub251.
  assumption.
Qed.

(* sub254 *)
(* rewrite((y - x)/c0 - x, (y - x*fold(1 + c0))/c0, "sub254") *)
Lemma sub254: forall x y c0, c0 ~= 0 -> (y - x)/c0 - x == (y - x*(1 + c0))/c0.
Proof.
  intros.
  rewrite <- !add_opp_r.
  rewrite <- div_add with (b := (- x)).
  rewrite <- mul_1_r with (n := x) at 1.
  rewrite <- mul_opp_l.
  rewrite <- add_assoc.
  rewrite <- mul_add_distr_l.
  rewrite mul_opp_l.
  reflexivity.
  assumption.
Qed.

(* sub258 *)
(* rewrite(((x + c0)/c1)*c1 - x, (-x) % c1, c1 > 0 && c0 + 1 == c1, "sub258") *)
Lemma sub258 : forall x c0 c1, c1 > 0 -> c0 + 1 == c1 -> ((x + c0)/c1)*c1 - x == (- x) mod c1.
Proof.
  intros.
  rewrite add_move_r in H0.
  rewrite H0.
  cut (c1 ~= 0).
  cut (x mod c1 == 0 \/ x mod c1 ~= 0).
  intros H1 H2.
  destruct H1.
  rewrite mod_opp_l_z.
  rewrite add_sub_assoc.
  rewrite <- pos_div_round_down_mod_z.
  rewrite <- div_exact in H1.
  rewrite H1 at 2.
  rewrite mul_comm.
  rewrite sub_diag.
  reflexivity.
  assumption.
  assumption.
  assumption.
  assumption.
  assumption.
  rewrite mod_opp_l_nz.
  rewrite abs_eq.
  rewrite add_sub_assoc.
  rewrite pos_div_round_down_mod_nz. 
  rewrite mul_comm.
  rewrite mul_add_distr_l.
  rewrite mul_1_r.
  rewrite add_comm.
  rewrite <- add_opp_r with (n := c1).
  rewrite <- add_sub_assoc.
  rewrite add_cancel_l.
  rewrite mod_eq.
  rewrite opp_sub_distr.
  rewrite add_comm.
  rewrite add_opp_r.
  reflexivity.
  assumption.
  assumption.
  assumption.
  apply lt_le_incl.
  assumption.
  assumption.
  assumption.
  apply eq_decidable.
  apply lt_neq_ooo.
  assumption.
Qed.

Lemma sub263 : forall x c0 c1 q r, 0<=r<abs c1 -> x == (c1 * q) + r -> c1 > 0 -> c0 + 1 == c1 -> ((x + c0)/c1) * c1 - x == (- x) mod c1.
Proof.
  intros.
  cut (x mod c1 == 0 \/ x mod c1 ~= 0).
  intros.
  destruct H3.
  rewrite mod_opp_l_z.
  rewrite add_move_r in H2.
  rewrite H2.
  rewrite H0.
  cut (r == (x mod c1)).
  intros.
  rewrite H4.
  rewrite H3.
  rewrite add_0_r.
  rewrite mul_comm with (n := c1) (m := q).
  rewrite div_add_l.
  cut ((c1 - 1)/c1 == 0).
  intros.
  rewrite H5.
  rewrite add_0_r.
  rewrite sub_diag.
  reflexivity.
  cut (0 <= c1 - 1).
  cut (c1 - 1 < c1).
  intros.
  rewrite div_small.
  reflexivity.
  auto.
  rewrite sub_1_r.
  apply lt_pred_l.
  rewrite sub_1_r.
  apply lt_le_pred.
  assumption.
  apply lt_neq_ooo.
  assumption.
  apply mod_unique in H0.
  assumption.
  assumption.
  apply lt_neq_ooo.
  assumption.
  assumption.
(* case in which x mod c1 ~= 0 *)
  rewrite mod_opp_l_nz.
  cut (abs c1 == c1).
  intros.
  rewrite H4.
  rewrite mod_eq.
  rewrite <- add_opp_r with (n := c1).
  rewrite opp_sub_distr with (n := x) (m := c1 * (x/c1)).
  rewrite add_comm with (n := -x).
  rewrite add_assoc.
  rewrite <- add_opp_r.
  rewrite add_cancel_r.
  rewrite <- mul_1_r with (n := c1) at 3.
  rewrite <- mul_add_distr_l.
  rewrite mul_comm.
  rewrite mul_cancel_l.
  apply eq_sym in H2.
  rewrite <- sub_move_r in H2.
  rewrite <- H2.
  rewrite <- add_opp_r.
  rewrite add_comm with (n := c1) (m := -1).
  rewrite add_assoc.
  rewrite <- mul_1_l with (n := c1) at 1.
  rewrite div_add.
  rewrite add_comm.
  rewrite add_cancel_l.
  rewrite H0.
  rewrite <- add_assoc.
  rewrite mul_comm.
  rewrite div_add_l.
  rewrite div_add_l.
  rewrite add_cancel_l.
  cut (r < c1).
  intros.
  rewrite div_small.
  rewrite div_small.
  reflexivity.
  rewrite abs_eq in H.
  assumption.
  apply mod_unique in H0.
  rewrite <- H0 in H3.
  destruct H.
  apply lt_le_incl.
  assumption.
  assumption.
  destruct H.
  apply mod_unique in H0.
  rewrite <- H0 in H3.
  cut (0 < r).
  intros.
  auto.
  rewrite lt_le_pred in H7.
  cut (P r == r + -1).
  intros.
  rewrite H8 in H7.
  cut (P r < c1).
  intros.
  rewrite H8 in H9.
  auto.
  apply lt_lt_pred.
  assumption.
  apply succ_inj_wd.
  rewrite succ_pred.
  rewrite <- add_succ_r.
  rewrite succ_m1.
  rewrite add_0_r.
  reflexivity.
  apply le_neq.
  cut (0 ~= r).
  intros.
  auto.
  apply neq_sym.
  assumption.
  auto.
  apply lt_le_incl in H1.
  rewrite <- abs_eq with (n := c1).
  destruct H.
  assumption.
  assumption.
  apply lt_neq_ooo.
  assumption.
  apply lt_neq_ooo.
  assumption.
  apply lt_neq_ooo.
  assumption.
  apply lt_neq_ooo.
  assumption.
  apply lt_neq_ooo.
  assumption.
  apply lt_le_incl in H1.
  apply abs_eq.
  assumption.
  apply lt_neq_ooo.
  assumption.
  assumption.
  auto.
  apply eq_decidable.
Qed.

(* sub268 *)
(* rewrite((x + y)/c0 - (x + c1)/c0, (((x + fold(c1 % c0)) % c0) + (y - c1))/c0, c0 > 0, "sub268") *)
Lemma sub268 : forall x y c0 c1, c0 ~= 0 -> (x + y)/c0 - (x + c1)/c0 == (((x + (c1 mod c0)) mod c0) + y - c1)/c0.
Proof.
  intros.
  rewrite mod_eq with (a := c1) (b := c0).
  rewrite mod_eq.
  rewrite add_sub_assoc with (n := x) (m := c1) (p := c0 * (c1/c0)).
  rewrite <- add_opp_r with (m := c1).
  rewrite <- add_opp_r with (m := c0 * ((x + c1 - c0 * (c1 / c0)) / c0)).
  rewrite <- add_assoc.
  rewrite add_shuffle0.
  rewrite <- mul_opp_r.
  rewrite <- add_opp_r with (m := c0 * (c1 / c0)).
  rewrite <- mul_opp_r.
  rewrite mul_comm.
  rewrite div_add.
  rewrite mul_comm with (m := - ((x + c1) / c0 + - (c1 / c0))).
  rewrite div_add.
  rewrite add_shuffle0.
  rewrite div_add.
  rewrite add_shuffle1.
  rewrite add_opp_r with (n := c1) (m := c1).
  rewrite sub_diag.
  rewrite add_0_r.
  rewrite opp_add_distr.
  rewrite opp_involutive.
  rewrite add_shuffle1.
  rewrite add_comm with (n := -(c1/c0)).
  rewrite add_opp_r.
  rewrite add_opp_r.
  rewrite sub_diag.
  rewrite add_0_r.
  reflexivity.
  assumption.
  assumption.
  assumption.
  assumption.
  assumption.
Qed.

(* sub269 *)
(* rewrite((x + c1)/c0 - (x + y)/c0, ((fold(c0 + c1 - 1) - y) - ((x + fold(c1 % c0)) % c0))/c0, c0 > 0, "sub269") *)
Lemma sub269 : forall x y c0 c1, c0 > 0 -> (x + c1)/c0 - (x + y)/c0 == (c0 + c1 - 1 - y - ((x + (c1 mod c0)) mod c0))/c0.
Proof.
  intros.
  cut (c0 ~= 0).
  intros H00.
  rewrite add_mod_idemp_r.
  rewrite mod_eq.
  rewrite <- add_opp_r with (m := (x + c1 - c0 * ((x + c1) / c0))).
  rewrite opp_sub_distr with (n := x + c1).
  rewrite mul_comm with (n := c0).
  rewrite <- !add_opp_r.
  rewrite !add_assoc.
  rewrite div_add.
  rewrite add_comm with (m := (x + c1)/c0).
  rewrite <- !add_assoc.
  rewrite add_comm with (n := c0).
  rewrite !add_assoc.
  rewrite add_comm with (m := -1).
  rewrite <- !add_assoc.
  rewrite add_comm with (n := -1).
  rewrite opp_add_distr.
  rewrite add_assoc.
  rewrite add_comm with (n := c1).
  rewrite !add_assoc.
  rewrite add_comm with (m := -x).
  rewrite !add_assoc.
  rewrite <- opp_add_distr.
  rewrite <- add_assoc.
  rewrite <- add_assoc with (n := - (x + y)).
  rewrite add_opp_diag_r.
  rewrite add_0_r.
  rewrite add_assoc.
  rewrite add_opp_r with (m := 1).
  cut ((x + y) mod c0 == 0 \/ (x + y) mod c0 ~= 0).
  intros H0.
  destruct H0.
  cut (-(x + y) mod c0 == 0).
  intros.
  rewrite <- pos_div_round_down_mod_z.
  rewrite div_opp_l_z.
  reflexivity.
  assumption.
  assumption.
  assumption.
  assumption.
  apply mod_opp_l_z.
  assumption.
  assumption.
  cut (-(x + y) mod c0 ~= 0).
  intros.
  rewrite pos_div_round_down_mod_nz.
  rewrite div_opp_l_nz.
  rewrite sgn_pos.
  rewrite <- add_opp_r.
  rewrite <- !add_assoc.
  rewrite add_opp_diag_l.
  rewrite add_0_r.
  reflexivity.
  assumption.
  assumption.
  assumption.
  assumption.
  assumption.
  apply neg_mod_nz.
  assumption.
  assumption.
  apply eq_decidable.
  assumption.
  assumption.
  assumption.
  apply lt_neq_ooo.
  assumption.
Qed.

(* sub270 *)
(* rewrite((x - y)/c0 - (x + c1)/c0, (((x + fold(c1 % c0)) % c0) - y - c1)/c0, c0 > 0, "sub270") *)
Lemma sub270 : forall x y c0 c1, c0 ~= 0 -> (x - y)/c0 - (x + c1)/c0 == (((x + (c1 mod c0)) mod c0) - y - c1)/c0.
Proof.
  intros.
  rewrite <- add_opp_r with (n := x) (m := y).
  rewrite <- add_opp_r with (m := y).
  apply sub268.
  assumption.
Qed.

(* sub271 *)
(* rewrite((x + c1)/c0 - (x - y)/c0, ((y + fold(c0 + c1 - 1)) - ((x + fold(c1 % c0)) % c0))/c0, c0 > 0, "sub271") *)
Lemma sub271 : forall x y c0 c1, c0 > 0 -> (x + c1)/c0 - (x - y)/c0 == (y + (c0 + c1 - 1) - ((x + (c1 mod c0)) mod c0))/c0.
Proof.
  intros.
  rewrite <- add_opp_r with (n := x) (m := y).
  rewrite sub269.
  rewrite <- add_opp_r with (m := - y).
  rewrite add_comm with (m := - - y).
  rewrite opp_involutive.
  reflexivity.
  assumption.
Qed.


(* sub272 *)
(* rewrite(x/c0 - (x + y)/c0, ((fold(c0 - 1) - y) - (x % c0))/c0, c0 > 0, "sub272") *)
Lemma sub272 : forall x y c0, c0 > 0 -> x/c0 - (x + y)/c0 == ((c0 - 1) - y - (x mod c0))/c0.
Proof.
  intros.
  cut (c0 ~= 0).
  intros H00.
  rewrite mod_eq.
  rewrite <- add_opp_r with (m := (x - c0 * (x/c0))).
  rewrite opp_sub_distr.
  rewrite !add_assoc.
  rewrite mul_comm.
  rewrite div_add.
  rewrite add_comm with (m := x / c0).
  rewrite <- add_opp_r with (m := y).
  rewrite add_comm with (m := - x).
  rewrite add_comm with (m := - y).
  rewrite add_assoc.
  rewrite <- opp_add_distr.
  cut ((x + y) mod c0 == 0 \/ (x + y) mod c0 ~= 0).
  intros H0.
  destruct H0.
  rewrite add_sub_assoc.
  cut (-(x + y) mod c0 == 0).
  intros.
  rewrite <- pos_div_round_down_mod_z.
  rewrite div_opp_l_z.
  rewrite add_opp_r.
  reflexivity.
  assumption.
  assumption.
  assumption.
  assumption.
  apply mod_opp_l_z.
  assumption.
  assumption.
  cut (-(x + y) mod c0 ~= 0).
  intros.
  rewrite add_sub_assoc.
  rewrite pos_div_round_down_mod_nz.
  rewrite div_opp_l_nz.
  rewrite sgn_pos.
  rewrite sub_simpl_r.
  rewrite add_opp_r.
  reflexivity.
  assumption.
  assumption.
  assumption.
  assumption.
  assumption.
  apply neg_mod_nz.
  assumption.
  assumption.
  apply eq_decidable.
  assumption.
  assumption.
  apply lt_neq_ooo.
  assumption.
Qed.


(* sub273 *)
(* rewrite((x + y)/c0 - x/c0, ((x % c0) + y)/c0, c0 > 0, "sub273") *)
Lemma sub273 : forall x y c0, c0 > 0 -> (x + y)/c0 - x/c0 == ((x mod c0) + y)/c0.
Proof.
  intros.
  rewrite mod_eq.
  cut (x - c0*(x/c0) == x + -(c0*(x/c0))).
  intros.
  rewrite H0.
  rewrite <- mul_opp_r.
  rewrite add_shuffle0.
  rewrite mul_comm.
  rewrite div_add.
  rewrite add_opp_r.
  reflexivity.
  apply lt_neq_ooo.
  assumption.
  rewrite add_opp_r.
  reflexivity.
  apply lt_neq_ooo.
  assumption.
Qed.

(* sub274 *)
(* rewrite(x/c0 - (x - y)/c0, ((y + fold(c0 - 1)) - (x % c0))/c0, c0 > 0, "sub274") *)
Lemma sub274 : forall x y c0, c0 > 0 -> x/c0 - (x - y)/c0 == (y + (c0 - 1) - (x mod c0))/c0.
Proof.
  intros.
  rewrite <- add_opp_r with (m := y).
  rewrite sub272.
  rewrite <- add_opp_r with (m := - y).
  rewrite add_comm with (n := c0 - 1).
  rewrite opp_involutive with (n := y).
  reflexivity.
  assumption.
Qed.

(* sub275 *)
(* rewrite((x - y)/c0 - x/c0, ((x % c0) - y)/c0, c0 > 0, "sub275") *)
Lemma sub275 : forall x y c0, c0 > 0 -> (x - y)/c0 - x/c0 == ((x mod c0) - y)/c0.
Proof.
  intros.
  rewrite <- add_opp_r.
  rewrite <- add_opp_r.
  rewrite <- add_opp_r.
  rewrite add_opp_r with (m := x/c0).
  apply sub273.
  assumption.
Qed.

End ZEuclidProp.