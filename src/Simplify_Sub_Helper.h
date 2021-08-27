#ifndef HALIDE_SIMPLIFY_SUB_HELPER_H
#define HALIDE_SIMPLIFY_SUB_HELPER_H

#include "Expr.h"
#include "Simplify_Internal.h"
#include "Simplify_Helper_Internal.h"

namespace Halide {
namespace Internal {

Expr simplify_sub(const Expr &expr, Simplify *simplifier) {
  if (const Sub *a0 = expr.as<Sub>()) {
    if (is_const(a0->a)) {
      if (is_const(a0->b)) {
        return fold(a0->a - a0->b, simplifier);
      }
      if (const Select *a102 = a0->b.as<Select>()) {
        if (is_const(a102->true_value)) {
          if (is_const(a102->false_value)) {
            return select(a102->condition, fold(a0->a - a102->true_value, simplifier), fold(a0->a - a102->false_value, simplifier));
          }
        }
      }
      if (const Div *a922 = a0->b.as<Div>()) {
        if (const Sub *a923 = a922->a.as<Sub>()) {
          if (is_const(a923->a)) {
            if (is_const(a922->b)) {
              if (evaluate_predicate(fold((a922->b > 0), simplifier))) {
                return ((fold((((a0->a*a922->b) - a923->a) + a922->b) - 1, simplifier) + a923->b)/a922->b);
              }
            }
          }
        }
        if (const Add *a926 = a922->a.as<Add>()) {
          if (is_const(a926->b)) {
            if (is_const(a922->b)) {
              if (evaluate_predicate(fold((a922->b > 0), simplifier))) {
                return ((fold((((a0->a*a922->b) - a926->b) + a922->b) - 1, simplifier) - a926->a)/a922->b);
              }
            }
          }
        }
      }
    }
    if (equal(a0->a, a0->b)) {
      return 0;
    }
    if (const Ramp *a3 = a0->a.as<Ramp>()) {
      if (is_const(a3->lanes)) {
        if (const Ramp *a4 = a0->b.as<Ramp>()) {
          if (equal(a3->lanes, a4->lanes)) {
            return ramp(a3->base - a4->base, a3->stride - a4->stride, a3->lanes);
          }
        }
        if (const Broadcast *a7 = a0->b.as<Broadcast>()) {
          if (equal(a3->lanes, a7->lanes)) {
            return ramp(a3->base - a7->value, a3->stride, a3->lanes);
          }
        }
      }
      if (const Broadcast *a30 = a3->base.as<Broadcast>()) {
        if (is_const(a30->lanes)) {
          if (is_const(a3->lanes)) {
            if (const Broadcast *a31 = a0->b.as<Broadcast>()) {
              if (is_const(a31->lanes)) {
                if (evaluate_predicate(fold((a31->lanes == (a30->lanes*a3->lanes)), simplifier))) {
                  return ramp(broadcast(a30->value - a31->value, a30->lanes), a3->stride, a3->lanes);
                }
              }
            }
          }
        }
      }
      if (const Ramp *a34 = a3->base.as<Ramp>()) {
        if (is_const(a34->lanes)) {
          if (is_const(a3->lanes)) {
            if (const Broadcast *a35 = a0->b.as<Broadcast>()) {
              if (is_const(a35->lanes)) {
                if (evaluate_predicate(fold((a35->lanes == (a34->lanes*a3->lanes)), simplifier))) {
                  return ramp(ramp(a34->base - a35->value, a34->stride, a34->lanes), a3->stride, a3->lanes);
                }
              }
            }
          }
        }
      }
    }
    if (const Broadcast *a9 = a0->a.as<Broadcast>()) {
      if (is_const(a9->lanes)) {
        if (const Ramp *a10 = a0->b.as<Ramp>()) {
          if (equal(a9->lanes, a10->lanes)) {
            return ramp(a9->value - a10->base, 0 - a10->stride, a9->lanes);
          }
        }
        if (const Broadcast *a13 = a0->b.as<Broadcast>()) {
          if (equal(a9->lanes, a13->lanes)) {
            return broadcast(a9->value - a13->value, a9->lanes);
          }
          if (is_const(a13->lanes)) {
            if (evaluate_predicate(fold(((a13->lanes % a9->lanes) == 0), simplifier))) {
              return broadcast(a9->value - broadcast(a13->value, fold(a13->lanes/a9->lanes, simplifier)), a9->lanes);
            }
            if (evaluate_predicate(fold(((a9->lanes % a13->lanes) == 0), simplifier))) {
              return broadcast(broadcast(a9->value, fold(a9->lanes/a13->lanes, simplifier)) - a13->value, a13->lanes);
            }
          }
        }
      }
    }
    if (const Sub *a21 = a0->a.as<Sub>()) {
      if (const Broadcast *a22 = a21->b.as<Broadcast>()) {
        if (is_const(a22->lanes)) {
          if (const Broadcast *a23 = a0->b.as<Broadcast>()) {
            if (equal(a22->lanes, a23->lanes)) {
              return (a21->a - broadcast(a22->value + a23->value, a22->lanes));
            }
          }
        }
      }
      if (equal(a21->a, a0->b)) {
        return (0 - a21->b);
      }
      if (const Select *a99 = a21->a.as<Select>()) {
        if (const Select *a100 = a0->b.as<Select>()) {
          if (equal(a99->condition, a100->condition)) {
            return (select(a99->condition, a99->true_value - a100->true_value, a99->false_value - a100->false_value) - a21->b);
          }
        }
      }
      if (is_const(a21->a)) {
        if (const Sub *a115 = a0->b.as<Sub>()) {
          if (is_const(a115->a)) {
            return ((a115->b - a21->b) + fold(a21->a - a115->a, simplifier));
          }
        }
        if (const Add *a118 = a0->b.as<Add>()) {
          if (is_const(a118->b)) {
            return (fold(a21->a - a118->b, simplifier) - (a21->b + a118->a));
          }
        }
        if (is_const(a0->b)) {
          return (fold(a21->a - a0->b, simplifier) - a21->b);
        }
      }
      if (const Mul *a157 = a21->b.as<Mul>()) {
        if (const Mul *a158 = a0->b.as<Mul>()) {
          if (equal(a157->b, a158->b)) {
            return (a21->a - ((a157->a + a158->a)*a157->b));
          }
          if (equal(a157->b, a158->a)) {
            return (a21->a - ((a157->a + a158->b)*a157->b));
          }
          if (equal(a157->a, a158->b)) {
            return (a21->a - (a157->a*(a157->b + a158->a)));
          }
          if (equal(a157->a, a158->a)) {
            return (a21->a - (a157->a*(a157->b + a158->b)));
          }
        }
      }
      if (const Mul *a189 = a21->a.as<Mul>()) {
        if (const Mul *a190 = a0->b.as<Mul>()) {
          if (equal(a189->b, a190->b)) {
            return (((a189->a - a190->a)*a189->b) - a21->b);
          }
          if (equal(a189->b, a190->a)) {
            return (((a189->a - a190->b)*a189->b) - a21->b);
          }
          if (equal(a189->a, a190->b)) {
            return ((a189->a*(a189->b - a190->a)) - a21->b);
          }
          if (equal(a189->a, a190->a)) {
            return ((a189->a*(a189->b - a190->b)) - a21->b);
          }
        }
      }
      if (const Add *a349 = a0->b.as<Add>()) {
        if (equal(a21->a, a349->a)) {
          return ((0 - a21->b) - a349->b);
        }
        if (equal(a21->a, a349->b)) {
          return ((0 - a21->b) - a349->a);
        }
      }
      if (const Add *a355 = a21->a.as<Add>()) {
        if (equal(a355->a, a0->b)) {
          return (a355->b - a21->b);
        }
        if (equal(a355->b, a0->b)) {
          return (a355->a - a21->b);
        }
      }
      if (const Sub *a389 = a21->a.as<Sub>()) {
        if (equal(a389->a, a0->b)) {
          return (0 - (a389->b + a21->b));
        }
      }
    }
    if (const Add *a25 = a0->a.as<Add>()) {
      if (const Broadcast *a26 = a25->b.as<Broadcast>()) {
        if (is_const(a26->lanes)) {
          if (const Broadcast *a27 = a0->b.as<Broadcast>()) {
            if (equal(a26->lanes, a27->lanes)) {
              return (a25->a + broadcast(a26->value - a27->value, a26->lanes));
            }
          }
        }
      }
      if (equal(a25->a, a0->b)) {
        return a25->b;
      }
      if (equal(a25->b, a0->b)) {
        return a25->a;
      }
      if (const Select *a83 = a25->a.as<Select>()) {
        if (const Select *a84 = a0->b.as<Select>()) {
          if (equal(a83->condition, a84->condition)) {
            return (select(a83->condition, a83->true_value - a84->true_value, a83->false_value - a84->false_value) + a25->b);
          }
        }
      }
      if (const Select *a87 = a25->b.as<Select>()) {
        if (const Select *a88 = a0->b.as<Select>()) {
          if (equal(a87->condition, a88->condition)) {
            return (select(a87->condition, a87->true_value - a88->true_value, a87->false_value - a88->false_value) + a25->a);
          }
        }
      }
      if (is_const(a25->b)) {
        if (is_const(a0->b)) {
          return (a25->a + fold(a25->b - a0->b, simplifier));
        }
        if (const Sub *a107 = a0->b.as<Sub>()) {
          if (is_const(a107->a)) {
            return ((a25->a + a107->b) + fold(a25->b - a107->a, simplifier));
          }
        }
        if (const Add *a110 = a0->b.as<Add>()) {
          if (is_const(a110->b)) {
            return ((a25->a - a110->a) + fold(a25->b - a110->b, simplifier));
          }
        }
        return ((a25->a - a0->b) + a25->b);
      }
      if (const Mul *a141 = a25->b.as<Mul>()) {
        if (const Mul *a142 = a0->b.as<Mul>()) {
          if (equal(a141->b, a142->b)) {
            return (a25->a + ((a141->a - a142->a)*a141->b));
          }
          if (equal(a141->b, a142->a)) {
            return (a25->a + ((a141->a - a142->b)*a141->b));
          }
          if (equal(a141->a, a142->b)) {
            return (a25->a + (a141->a*(a141->b - a142->a)));
          }
          if (equal(a141->a, a142->a)) {
            return (a25->a + (a141->a*(a141->b - a142->b)));
          }
        }
      }
      if (const Mul *a173 = a25->a.as<Mul>()) {
        if (const Mul *a174 = a0->b.as<Mul>()) {
          if (equal(a173->b, a174->b)) {
            return (a25->b + ((a173->a - a174->a)*a173->b));
          }
          if (equal(a173->b, a174->a)) {
            return (a25->b + ((a173->a - a174->b)*a173->b));
          }
          if (equal(a173->a, a174->b)) {
            return (a25->b + (a173->a*(a173->b - a174->a)));
          }
          if (equal(a173->a, a174->a)) {
            return (a25->b + (a173->a*(a173->b - a174->b)));
          }
        }
      }
      if (const Add *a269 = a0->b.as<Add>()) {
        if (equal(a25->a, a269->a)) {
          return (a25->b - a269->b);
        }
        if (equal(a25->a, a269->b)) {
          return (a25->b - a269->a);
        }
        if (equal(a25->b, a269->a)) {
          return (a25->a - a269->b);
        }
        if (equal(a25->b, a269->b)) {
          return (a25->a - a269->a);
        }
        if (const Add *a318 = a269->b.as<Add>()) {
          if (equal(a25->a, a318->b)) {
            return (a25->b - (a269->a + a318->a));
          }
          if (equal(a25->b, a318->b)) {
            return (a25->a - (a269->a + a318->a));
          }
          if (equal(a25->a, a318->a)) {
            return (a25->b - (a269->a + a318->b));
          }
          if (equal(a25->b, a318->a)) {
            return (a25->a - (a269->a + a318->b));
          }
        }
        if (const Add *a334 = a269->a.as<Add>()) {
          if (equal(a25->a, a334->a)) {
            return (a25->b - (a334->b + a269->b));
          }
          if (equal(a25->b, a334->a)) {
            return (a25->a - (a334->b + a269->b));
          }
          if (equal(a25->a, a334->b)) {
            return (a25->b - (a334->a + a269->b));
          }
          if (equal(a25->b, a334->b)) {
            return (a25->a - (a334->a + a269->b));
          }
        }
      }
      if (const Add *a281 = a25->a.as<Add>()) {
        if (equal(a281->a, a0->b)) {
          return (a281->b + a25->b);
        }
        if (equal(a281->b, a0->b)) {
          return (a281->a + a25->b);
        }
      }
      if (const Add *a287 = a25->b.as<Add>()) {
        if (equal(a287->a, a0->b)) {
          return (a25->a + a287->b);
        }
        if (equal(a287->b, a0->b)) {
          return (a25->a + a287->a);
        }
      }
      if (const Sub *a299 = a25->b.as<Sub>()) {
        if (equal(a299->a, a0->b)) {
          return (a25->a - a299->b);
        }
      }
      if (const Sub *a302 = a25->a.as<Sub>()) {
        if (equal(a302->a, a0->b)) {
          return (a25->b - a302->b);
        }
      }
      if (const Min *a369 = a0->b.as<Min>()) {
        if (equal(a25->a, a369->a)) {
          if (equal(a25->b, a369->b)) {
            return max(a25->b, a25->a);
          }
        }
        if (equal(a25->b, a369->a)) {
          if (equal(a25->a, a369->b)) {
            return max(a25->b, a25->a);
          }
        }
      }
      if (const Max *a375 = a0->b.as<Max>()) {
        if (equal(a25->a, a375->a)) {
          if (equal(a25->b, a375->b)) {
            return min(a25->b, a25->a);
          }
        }
        if (equal(a25->b, a375->a)) {
          if (equal(a25->a, a375->b)) {
            return min(a25->a, a25->b);
          }
        }
      }
      if (const Min *a1021 = a25->a.as<Min>()) {
        if (const Add *a1022 = a1021->a.as<Add>()) {
          if (equal(a1022->a, a0->b)) {
            return (min(a1021->b - a1022->a, a1022->b) + a25->b);
          }
        }
      }
    }
    if (const Select *a37 = a0->a.as<Select>()) {
      if (const Select *a38 = a0->b.as<Select>()) {
        if (equal(a37->condition, a38->condition)) {
          return select(a37->condition, a37->true_value - a38->true_value, a37->false_value - a38->false_value);
        }
      }
      if (equal(a37->true_value, a0->b)) {
        return select(a37->condition, 0, a37->false_value - a37->true_value);
      }
      if (equal(a37->false_value, a0->b)) {
        return select(a37->condition, a37->true_value - a37->false_value, 0);
      }
      if (const Add *a49 = a37->true_value.as<Add>()) {
        if (equal(a49->a, a0->b)) {
          return select(a37->condition, a49->b, a37->false_value - a49->a);
        }
        if (equal(a49->b, a0->b)) {
          return select(a37->condition, a49->a, a37->false_value - a49->b);
        }
      }
      if (const Add *a55 = a37->false_value.as<Add>()) {
        if (equal(a55->a, a0->b)) {
          return select(a37->condition, a37->true_value - a55->a, a55->b);
        }
        if (equal(a55->b, a0->b)) {
          return select(a37->condition, a37->true_value - a55->b, a55->a);
        }
      }
      if (const Add *a91 = a0->b.as<Add>()) {
        if (const Select *a92 = a91->a.as<Select>()) {
          if (equal(a37->condition, a92->condition)) {
            return (select(a37->condition, a37->true_value - a92->true_value, a37->false_value - a92->false_value) - a91->b);
          }
        }
        if (const Select *a96 = a91->b.as<Select>()) {
          if (equal(a37->condition, a96->condition)) {
            return (select(a37->condition, a37->true_value - a96->true_value, a37->false_value - a96->false_value) - a91->a);
          }
        }
      }
    }
    if (const Select *a44 = a0->b.as<Select>()) {
      if (equal(a0->a, a44->true_value)) {
        return select(a44->condition, 0, a0->a - a44->false_value);
      }
      if (equal(a0->a, a44->false_value)) {
        return select(a44->condition, a0->a - a44->true_value, 0);
      }
      if (const Add *a61 = a44->true_value.as<Add>()) {
        if (equal(a0->a, a61->a)) {
          return (0 - select(a44->condition, a61->b, a44->false_value - a0->a));
        }
        if (equal(a0->a, a61->b)) {
          return (0 - select(a44->condition, a61->a, a44->false_value - a0->a));
        }
      }
      if (const Add *a67 = a44->false_value.as<Add>()) {
        if (equal(a0->a, a67->a)) {
          return (0 - select(a44->condition, a44->true_value - a0->a, a67->b));
        }
        if (equal(a0->a, a67->b)) {
          return (0 - select(a44->condition, a44->true_value - a0->a, a67->a));
        }
      }
    }
    if (const Add *a76 = a0->b.as<Add>()) {
      if (equal(a0->a, a76->a)) {
        return (0 - a76->b);
      }
      if (equal(a0->a, a76->b)) {
        return (0 - a76->a);
      }
      if (is_const(a76->b)) {
        return ((a0->a - a76->a) - a76->b);
      }
      if (const Sub *a293 = a76->b.as<Sub>()) {
        if (equal(a0->a, a293->a)) {
          return (a293->b - a76->a);
        }
      }
      if (const Sub *a296 = a76->a.as<Sub>()) {
        if (equal(a0->a, a296->a)) {
          return (a296->b - a76->b);
        }
      }
      if (const Add *a305 = a76->b.as<Add>()) {
        if (equal(a0->a, a305->a)) {
          return (0 - (a76->a + a305->b));
        }
        if (equal(a0->a, a305->b)) {
          return (0 - (a76->a + a305->a));
        }
      }
      if (const Add *a311 = a76->a.as<Add>()) {
        if (equal(a0->a, a311->a)) {
          return (0 - (a311->b + a76->b));
        }
        if (equal(a0->a, a311->b)) {
          return (0 - (a311->a + a76->b));
        }
      }
    }
    if (const Sub *a120 = a0->b.as<Sub>()) {
      return (a0->a + (a120->b - a120->a));
    }
    if (const Mul *a122 = a0->b.as<Mul>()) {
      if (is_const(a122->b)) {
        if (evaluate_predicate(fold(((a122->b < 0) && ((0 - a122->b) > 0)), simplifier))) {
          return (a0->a + (a122->a*fold(0 - a122->b, simplifier)));
        }
      }
      if (const Div *a394 = a122->a.as<Div>()) {
        if (const Add *a395 = a394->a.as<Add>()) {
          if (equal(a0->a, a395->a)) {
            if (is_const(a395->b)) {
              if (is_const(a394->b)) {
                if (equal(a394->b, a122->b)) {
                  if (evaluate_predicate(fold((a394->b > 0), simplifier))) {
                    return (((a0->a + a395->b) % a394->b) - a395->b);
                  }
                  if (evaluate_predicate(fold(((a394->b > 0) && ((a395->b + 1) == a394->b)), simplifier))) {
                    return (((a0->a + a395->b) % a394->b) + fold(0 - a395->b, simplifier));
                  }
                }
              }
            }
          }
        }
        if (equal(a0->a, a394->a)) {
          if (is_const(a394->b)) {
            if (equal(a394->b, a122->b)) {
              if (evaluate_predicate(fold((a394->b > 0), simplifier))) {
                return (a0->a % a394->b);
              }
            }
          }
        }
      }
      if (equal(a0->a, a122->a)) {
        return (a0->a*(1 - a122->b));
      }
      if (equal(a0->a, a122->b)) {
        return ((1 - a122->a)*a0->a);
      }
    }
    if (const Mul *a128 = a0->a.as<Mul>()) {
      if (const Mul *a129 = a0->b.as<Mul>()) {
        if (equal(a128->b, a129->b)) {
          return ((a128->a - a129->a)*a128->b);
        }
        if (equal(a128->b, a129->a)) {
          return ((a128->a - a129->b)*a128->b);
        }
        if (equal(a128->a, a129->b)) {
          return (a128->a*(a128->b - a129->a));
        }
        if (equal(a128->a, a129->a)) {
          return (a128->a*(a128->b - a129->b));
        }
      }
      if (const Add *a205 = a0->b.as<Add>()) {
        if (const Mul *a206 = a205->b.as<Mul>()) {
          if (equal(a128->b, a206->b)) {
            return (((a128->a - a206->a)*a128->b) - a205->a);
          }
          if (equal(a128->b, a206->a)) {
            return (((a128->a - a206->b)*a128->b) - a205->a);
          }
          if (equal(a128->a, a206->b)) {
            return ((a128->a*(a128->b - a206->a)) - a205->a);
          }
          if (equal(a128->a, a206->a)) {
            return ((a128->a*(a128->b - a206->b)) - a205->a);
          }
        }
        if (const Mul *a238 = a205->a.as<Mul>()) {
          if (equal(a128->b, a238->b)) {
            return (((a128->a - a238->a)*a128->b) - a205->b);
          }
          if (equal(a128->b, a238->a)) {
            return (((a128->a - a238->b)*a128->b) - a205->b);
          }
          if (equal(a128->a, a238->b)) {
            return ((a128->a*(a128->b - a238->a)) - a205->b);
          }
          if (equal(a128->a, a238->a)) {
            return ((a128->a*(a128->b - a238->b)) - a205->b);
          }
        }
      }
      if (const Sub *a221 = a0->b.as<Sub>()) {
        if (const Mul *a222 = a221->b.as<Mul>()) {
          if (equal(a128->b, a222->b)) {
            return (((a128->a + a222->a)*a128->b) - a221->a);
          }
          if (equal(a128->b, a222->a)) {
            return (((a128->a + a222->b)*a128->b) - a221->a);
          }
          if (equal(a128->a, a222->b)) {
            return ((a128->a*(a128->b + a222->a)) - a221->a);
          }
          if (equal(a128->a, a222->a)) {
            return ((a128->a*(a128->b + a222->b)) - a221->a);
          }
        }
        if (const Mul *a254 = a221->a.as<Mul>()) {
          if (equal(a128->b, a254->b)) {
            return (((a128->a - a254->a)*a128->b) + a221->b);
          }
          if (equal(a128->b, a254->a)) {
            return (((a128->a - a254->b)*a128->b) + a221->b);
          }
          if (equal(a128->a, a254->b)) {
            return ((a128->a*(a128->b - a254->a)) + a221->b);
          }
          if (equal(a128->a, a254->a)) {
            return ((a128->a*(a128->b - a254->b)) + a221->b);
          }
        }
      }
      if (equal(a128->a, a0->b)) {
        return (a128->a*(a128->b - 1));
      }
      if (equal(a128->b, a0->b)) {
        return ((a128->a - 1)*a128->b);
      }
      if (const Div *a953 = a128->a.as<Div>()) {
        if (is_const(a953->b)) {
          if (equal(a953->b, a128->b)) {
            if (equal(a953->a, a0->b)) {
              if (evaluate_predicate(fold((a953->b > 0), simplifier))) {
                return (0 - (a953->a % a953->b));
              }
            }
          }
        }
        if (const Add *a960 = a953->a.as<Add>()) {
          if (is_const(a960->b)) {
            if (is_const(a953->b)) {
              if (equal(a953->b, a128->b)) {
                if (equal(a960->a, a0->b)) {
                  if (evaluate_predicate(fold(((a953->b > 0) && ((a960->b + 1) == a953->b)), simplifier))) {
                    return ((0 - a960->a) % a953->b);
                  }
                }
              }
            }
          }
        }
      }
      if (is_const(a128->b)) {
        if (const Mul *a967 = a0->b.as<Mul>()) {
          if (is_const(a967->b)) {
            if (evaluate_predicate(fold(((a128->b % a967->b) == 0), simplifier))) {
              return (((a128->a*fold(a128->b/a967->b, simplifier)) - a967->a)*a967->b);
            }
            if (evaluate_predicate(fold(((a967->b % a128->b) == 0), simplifier))) {
              return ((a128->a - (a967->a*fold(a967->b/a128->b, simplifier)))*a128->b);
            }
          }
        }
      }
    }
    if (const Min *a360 = a0->b.as<Min>()) {
      if (const Sub *a361 = a360->a.as<Sub>()) {
        if (equal(a0->a, a361->a)) {
          if (const IntImm *a362 = a360->b.as<IntImm>()) {
            if (a362->value == 0) {
              return max(a0->a, a361->b);
            }
          }
          return max(a361->b, a0->a - a360->b);
        }
        if (is_const(a360->b)) {
          return (a0->a + max(a361->b - a361->a, fold(0 - a360->b, simplifier)));
        }
      }
      if (equal(a0->a, a360->a)) {
        if (evaluate_predicate(fold(!is_const(a0->a), simplifier))) {
          return max(a0->a - a360->b, 0);
        }
      }
      if (equal(a0->a, a360->b)) {
        if (evaluate_predicate(fold(!is_const(a0->a), simplifier))) {
          return max(a0->a - a360->a, 0);
        }
      }
      if (const Sub *a414 = a360->b.as<Sub>()) {
        if (equal(a0->a, a414->a)) {
          return max(a0->a - a360->a, a414->b);
        }
      }
      if (const Max *a456 = a360->a.as<Max>()) {
        if (const Sub *a457 = a456->a.as<Sub>()) {
          if (is_const(a456->b)) {
            if (is_const(a360->b)) {
              return (a0->a + max(min(a457->b - a457->a, fold(0 - a456->b, simplifier)), fold(0 - a360->b, simplifier)));
            }
          }
        }
      }
      if (const Add *a468 = a360->b.as<Add>()) {
        if (equal(a0->a, a468->a)) {
          if (evaluate_predicate(fold(!is_const(a0->a), simplifier))) {
            return (0 - min(a360->a - a0->a, a468->b));
          }
        }
        if (equal(a0->a, a468->b)) {
          if (evaluate_predicate(fold(!is_const(a0->a), simplifier))) {
            return (0 - min(a360->a - a0->a, a468->a));
          }
        }
        if (const Add *a481 = a468->b.as<Add>()) {
          if (equal(a0->a, a481->a)) {
            if (evaluate_predicate(fold(!is_const(a0->a), simplifier))) {
              return (0 - min(a360->a - a0->a, a468->a + a481->b));
            }
          }
          if (equal(a0->a, a481->b)) {
            if (evaluate_predicate(fold(!is_const(a0->a), simplifier))) {
              return (0 - min(a360->a - a0->a, a481->a + a468->a));
            }
          }
        }
        if (const Add *a489 = a468->a.as<Add>()) {
          if (equal(a0->a, a489->a)) {
            if (evaluate_predicate(fold(!is_const(a0->a), simplifier))) {
              return (0 - min(a360->a - a0->a, a489->b + a468->b));
            }
          }
          if (equal(a0->a, a489->b)) {
            if (evaluate_predicate(fold(!is_const(a0->a), simplifier))) {
              return (0 - min(a360->a - a0->a, a489->a + a468->b));
            }
          }
        }
      }
      if (const Add *a474 = a360->a.as<Add>()) {
        if (equal(a0->a, a474->a)) {
          if (evaluate_predicate(fold(!is_const(a0->a), simplifier))) {
            return (0 - min(a360->b - a0->a, a474->b));
          }
        }
        if (equal(a0->a, a474->b)) {
          if (evaluate_predicate(fold(!is_const(a0->a), simplifier))) {
            return (0 - min(a360->b - a0->a, a474->a));
          }
        }
        if (const Add *a497 = a474->b.as<Add>()) {
          if (equal(a0->a, a497->a)) {
            if (evaluate_predicate(fold(!is_const(a0->a), simplifier))) {
              return (0 - min(a360->b - a0->a, a474->a + a497->b));
            }
          }
          if (equal(a0->a, a497->b)) {
            if (evaluate_predicate(fold(!is_const(a0->a), simplifier))) {
              return (0 - min(a360->b - a0->a, a497->a + a474->a));
            }
          }
        }
        if (const Add *a505 = a474->a.as<Add>()) {
          if (equal(a0->a, a505->a)) {
            if (evaluate_predicate(fold(!is_const(a0->a), simplifier))) {
              return (0 - min(a360->b - a0->a, a474->b + a505->b));
            }
          }
          if (equal(a0->a, a505->b)) {
            if (evaluate_predicate(fold(!is_const(a0->a), simplifier))) {
              return (0 - min(a360->b - a0->a, a474->b + a505->a));
            }
          }
        }
      }
    }
    if (const Max *a364 = a0->b.as<Max>()) {
      if (const Sub *a365 = a364->a.as<Sub>()) {
        if (equal(a0->a, a365->a)) {
          if (const IntImm *a366 = a364->b.as<IntImm>()) {
            if (a366->value == 0) {
              return min(a0->a, a365->b);
            }
          }
          return min(a365->b, a0->a - a364->b);
        }
        if (is_const(a364->b)) {
          return (a0->a + min(a365->b - a365->a, fold(0 - a364->b, simplifier)));
        }
      }
      if (equal(a0->a, a364->a)) {
        if (evaluate_predicate(fold(!is_const(a0->a), simplifier))) {
          return min(a0->a - a364->b, 0);
        }
      }
      if (equal(a0->a, a364->b)) {
        if (evaluate_predicate(fold(!is_const(a0->a), simplifier))) {
          return min(a0->a - a364->a, 0);
        }
      }
      if (const Sub *a420 = a364->b.as<Sub>()) {
        if (equal(a0->a, a420->a)) {
          return min(a0->a - a364->a, a420->b);
        }
      }
      if (const Min *a452 = a364->a.as<Min>()) {
        if (const Sub *a453 = a452->a.as<Sub>()) {
          if (is_const(a452->b)) {
            if (is_const(a364->b)) {
              return (a0->a + min(max(a453->b - a453->a, fold(0 - a452->b, simplifier)), fold(0 - a364->b, simplifier)));
            }
          }
        }
      }
      if (const Add *a570 = a364->b.as<Add>()) {
        if (equal(a0->a, a570->a)) {
          if (evaluate_predicate(fold(!is_const(a0->a), simplifier))) {
            return (0 - max(a364->a - a0->a, a570->b));
          }
        }
        if (equal(a0->a, a570->b)) {
          if (evaluate_predicate(fold(!is_const(a0->a), simplifier))) {
            return (0 - max(a364->a - a0->a, a570->a));
          }
        }
        if (const Add *a583 = a570->b.as<Add>()) {
          if (equal(a0->a, a583->a)) {
            if (evaluate_predicate(fold(!is_const(a0->a), simplifier))) {
              return (0 - max(a364->a - a0->a, a570->a + a583->b));
            }
          }
          if (equal(a0->a, a583->b)) {
            if (evaluate_predicate(fold(!is_const(a0->a), simplifier))) {
              return (0 - max(a364->a - a0->a, a583->a + a570->a));
            }
          }
        }
        if (const Add *a591 = a570->a.as<Add>()) {
          if (equal(a0->a, a591->a)) {
            if (evaluate_predicate(fold(!is_const(a0->a), simplifier))) {
              return (0 - max(a364->a - a0->a, a591->b + a570->b));
            }
          }
          if (equal(a0->a, a591->b)) {
            if (evaluate_predicate(fold(!is_const(a0->a), simplifier))) {
              return (0 - max(a364->a - a0->a, a591->a + a570->b));
            }
          }
        }
      }
      if (const Add *a576 = a364->a.as<Add>()) {
        if (equal(a0->a, a576->a)) {
          if (evaluate_predicate(fold(!is_const(a0->a), simplifier))) {
            return (0 - max(a364->b - a0->a, a576->b));
          }
        }
        if (equal(a0->a, a576->b)) {
          if (evaluate_predicate(fold(!is_const(a0->a), simplifier))) {
            return (0 - max(a364->b - a0->a, a576->a));
          }
        }
        if (const Add *a599 = a576->b.as<Add>()) {
          if (equal(a0->a, a599->a)) {
            if (evaluate_predicate(fold(!is_const(a0->a), simplifier))) {
              return (0 - max(a364->b - a0->a, a576->a + a599->b));
            }
          }
          if (equal(a0->a, a599->b)) {
            if (evaluate_predicate(fold(!is_const(a0->a), simplifier))) {
              return (0 - max(a364->b - a0->a, a599->a + a576->a));
            }
          }
        }
        if (const Add *a607 = a576->a.as<Add>()) {
          if (equal(a0->a, a607->a)) {
            if (evaluate_predicate(fold(!is_const(a0->a), simplifier))) {
              return (0 - max(a364->b - a0->a, a576->b + a607->b));
            }
          }
          if (equal(a0->a, a607->b)) {
            if (evaluate_predicate(fold(!is_const(a0->a), simplifier))) {
              return (0 - max(a364->b - a0->a, a576->b + a607->a));
            }
          }
        }
      }
    }
    if (const IntImm *a380 = a0->a.as<IntImm>()) {
      if (a380->value == 0) {
        if (const Add *a381 = a0->b.as<Add>()) {
          if (const Sub *a382 = a381->b.as<Sub>()) {
            return (a382->b - (a381->a + a382->a));
          }
          if (const Sub *a386 = a381->a.as<Sub>()) {
            return (a386->b - (a386->a + a381->b));
          }
        }
      }
    }
    if (const Mod *a391 = a0->b.as<Mod>()) {
      if (equal(a0->a, a391->a)) {
        if (is_const(a391->b)) {
          return ((a0->a/a391->b)*a391->b);
        }
      }
    }
    if (const Max *a397 = a0->a.as<Max>()) {
      if (equal(a397->a, a0->b)) {
        return max(a397->b - a397->a, 0);
      }
      if (equal(a397->b, a0->b)) {
        return max(a397->a - a397->b, 0);
      }
      if (const Sub *a430 = a397->a.as<Sub>()) {
        if (const IntImm *a431 = a397->b.as<IntImm>()) {
          if (a431->value == 0) {
            if (equal(a430->a, a0->b)) {
              return (0 - min(a430->a, a430->b));
            }
          }
        }
      }
      if (const Add *a440 = a0->b.as<Add>()) {
        if (equal(a397->a, a440->a)) {
          if (equal(a397->b, a440->b)) {
            return (0 - min(a397->a, a397->b));
          }
        }
        if (equal(a397->b, a440->a)) {
          if (equal(a397->a, a440->b)) {
            return (0 - min(a397->b, a397->a));
          }
        }
      }
      if (const Add *a614 = a397->a.as<Add>()) {
        if (equal(a614->a, a0->b)) {
          return max(a397->b - a614->a, a614->b);
        }
        if (equal(a614->b, a0->b)) {
          return max(a397->b - a614->b, a614->a);
        }
        if (const Add *a643 = a614->b.as<Add>()) {
          if (equal(a643->b, a0->b)) {
            return max(a397->b - a643->b, a614->a + a643->a);
          }
          if (equal(a643->a, a0->b)) {
            return max(a397->b - a643->a, a614->a + a643->b);
          }
        }
        if (const Add *a651 = a614->a.as<Add>()) {
          if (equal(a651->b, a0->b)) {
            return max(a397->b - a651->b, a651->a + a614->b);
          }
          if (equal(a651->a, a0->b)) {
            return max(a397->b - a651->a, a651->b + a614->b);
          }
        }
        if (is_const(a614->b)) {
          if (const Max *a802 = a0->b.as<Max>()) {
            if (equal(a614->a, a802->a)) {
              if (evaluate_predicate(fold(_can_prove(simplifier, a397->b >= (a802->b + a614->b)), simplifier))) {
                return max(a397->b - max(a614->a, a802->b), a614->b);
              }
              if (evaluate_predicate(fold(_can_prove(simplifier, a397->b <= (a802->b + a614->b)), simplifier))) {
                return min(max(a614->a + a614->b, a397->b) - a802->b, a614->b);
              }
            }
            if (const Add *a819 = a802->a.as<Add>()) {
              if (equal(a614->a, a819->a)) {
                if (is_const(a819->b)) {
                  if (evaluate_predicate(fold(_can_prove(simplifier, (a397->b + a819->b) >= (a802->b + a614->b)), simplifier))) {
                    return max(a397->b - max(a614->a + a819->b, a802->b), fold(a614->b - a819->b, simplifier));
                  }
                  if (evaluate_predicate(fold(_can_prove(simplifier, (a397->b + a819->b) <= (a802->b + a614->b)), simplifier))) {
                    return min(max(a614->a + a614->b, a397->b) - a802->b, fold(a614->b - a819->b, simplifier));
                  }
                }
              }
            }
            if (equal(a614->a, a802->b)) {
              if (evaluate_predicate(fold(_can_prove(simplifier, a397->b >= (a802->a + a614->b)), simplifier))) {
                return max(a397->b - max(a614->a, a802->a), a614->b);
              }
              if (evaluate_predicate(fold(_can_prove(simplifier, a397->b <= (a802->a + a614->b)), simplifier))) {
                return min(max(a614->a + a614->b, a397->b) - a802->a, a614->b);
              }
            }
            if (const Add *a883 = a802->b.as<Add>()) {
              if (equal(a614->a, a883->a)) {
                if (is_const(a883->b)) {
                  if (evaluate_predicate(fold(_can_prove(simplifier, (a397->b + a883->b) >= (a802->a + a614->b)), simplifier))) {
                    return max(a397->b - max(a614->a + a883->b, a802->a), fold(a614->b - a883->b, simplifier));
                  }
                  if (evaluate_predicate(fold(_can_prove(simplifier, (a397->b + a883->b) <= (a802->a + a614->b)), simplifier))) {
                    return min(max(a614->a + a614->b, a397->b) - a802->a, fold(a614->b - a883->b, simplifier));
                  }
                }
              }
            }
          }
        }
      }
      if (const Add *a620 = a397->b.as<Add>()) {
        if (equal(a620->a, a0->b)) {
          return max(a397->a - a620->a, a620->b);
        }
        if (equal(a620->b, a0->b)) {
          return max(a397->a - a620->b, a620->a);
        }
        if (const Add *a627 = a620->b.as<Add>()) {
          if (equal(a627->b, a0->b)) {
            return max(a397->a - a627->b, a620->a + a627->a);
          }
          if (equal(a627->a, a0->b)) {
            return max(a397->a - a627->a, a620->a + a627->b);
          }
        }
        if (const Add *a635 = a620->a.as<Add>()) {
          if (equal(a635->b, a0->b)) {
            return max(a397->a - a635->b, a635->a + a620->b);
          }
          if (equal(a635->a, a0->b)) {
            return max(a397->a - a635->a, a635->b + a620->b);
          }
        }
        if (is_const(a620->b)) {
          if (const Max *a834 = a0->b.as<Max>()) {
            if (equal(a620->a, a834->b)) {
              if (evaluate_predicate(fold(_can_prove(simplifier, a397->a >= (a834->a + a620->b)), simplifier))) {
                return max(a397->a - max(a620->a, a834->a), a620->b);
              }
              if (evaluate_predicate(fold(_can_prove(simplifier, a397->a <= (a834->a + a620->b)), simplifier))) {
                return min(max(a620->a + a620->b, a397->a) - a834->a, a620->b);
              }
            }
            if (const Add *a851 = a834->b.as<Add>()) {
              if (equal(a620->a, a851->a)) {
                if (is_const(a851->b)) {
                  if (evaluate_predicate(fold(_can_prove(simplifier, (a397->a + a851->b) >= (a834->a + a620->b)), simplifier))) {
                    return max(a397->a - max(a620->a + a851->b, a834->a), fold(a620->b - a851->b, simplifier));
                  }
                  if (evaluate_predicate(fold(_can_prove(simplifier, (a397->a + a851->b) <= (a834->a + a620->b)), simplifier))) {
                    return min(max(a620->a + a620->b, a397->a) - a834->a, fold(a620->b - a851->b, simplifier));
                  }
                }
              }
            }
            if (equal(a620->a, a834->a)) {
              if (evaluate_predicate(fold(_can_prove(simplifier, a397->a >= (a834->b + a620->b)), simplifier))) {
                return max(a397->a - max(a620->a, a834->b), a620->b);
              }
              if (evaluate_predicate(fold(_can_prove(simplifier, a397->a <= (a834->b + a620->b)), simplifier))) {
                return min(max(a620->a + a620->b, a397->a) - a834->b, a620->b);
              }
            }
            if (const Add *a915 = a834->a.as<Add>()) {
              if (equal(a620->a, a915->a)) {
                if (is_const(a915->b)) {
                  if (evaluate_predicate(fold(_can_prove(simplifier, (a397->a + a915->b) >= (a834->b + a620->b)), simplifier))) {
                    return max(a397->a - max(a620->a + a915->b, a834->b), fold(a620->b - a915->b, simplifier));
                  }
                  if (evaluate_predicate(fold(_can_prove(simplifier, (a397->a + a915->b) <= (a834->b + a620->b)), simplifier))) {
                    return min(max(a620->a + a620->b, a397->a) - a834->b, fold(a620->b - a915->b, simplifier));
                  }
                }
              }
            }
          }
        }
      }
      if (const Max *a658 = a0->b.as<Max>()) {
        if (equal(a397->b, a658->a)) {
          if (equal(a397->a, a658->b)) {
            return 0;
          }
          if (evaluate_predicate(fold(_can_prove(simplifier, a397->a >= a658->b), simplifier))) {
            return max(a397->a - max(a397->b, a658->b), 0);
          }
          if (evaluate_predicate(fold(_can_prove(simplifier, a397->a <= a658->b), simplifier))) {
            return min(max(a397->b, a397->a) - a658->b, 0);
          }
        }
        if (evaluate_predicate(fold(_can_prove(simplifier, (a397->a - a397->b) == (a658->a - a658->b)), simplifier))) {
          return (a397->b - a658->b);
        }
        if (evaluate_predicate(fold(_can_prove(simplifier, (a397->a - a397->b) == (a658->b - a658->a)), simplifier))) {
          return (a397->b - a658->a);
        }
        if (equal(a397->a, a658->a)) {
          if (evaluate_predicate(fold(_can_prove(simplifier, a397->b >= a658->b), simplifier))) {
            return max(a397->b - max(a397->a, a658->b), 0);
          }
          if (evaluate_predicate(fold(_can_prove(simplifier, a397->b <= a658->b), simplifier))) {
            return min(max(a397->a, a397->b) - a658->b, 0);
          }
        }
        if (const Add *a810 = a658->a.as<Add>()) {
          if (equal(a397->a, a810->a)) {
            if (is_const(a810->b)) {
              if (evaluate_predicate(fold(_can_prove(simplifier, (a397->b + a810->b) >= a658->b), simplifier))) {
                return max(a397->b - max(a397->a + a810->b, a658->b), fold(0 - a810->b, simplifier));
              }
              if (evaluate_predicate(fold(_can_prove(simplifier, (a397->b + a810->b) <= a658->b), simplifier))) {
                return min(max(a397->a, a397->b) - a658->b, fold(0 - a810->b, simplifier));
              }
            }
          }
          if (equal(a397->b, a810->a)) {
            if (is_const(a810->b)) {
              if (evaluate_predicate(fold(_can_prove(simplifier, (a397->a + a810->b) >= a658->b), simplifier))) {
                return max(a397->a - max(a397->b + a810->b, a658->b), fold(0 - a810->b, simplifier));
              }
              if (evaluate_predicate(fold(_can_prove(simplifier, (a397->a + a810->b) <= a658->b), simplifier))) {
                return min(max(a397->b, a397->a) - a658->b, fold(0 - a810->b, simplifier));
              }
            }
          }
        }
        if (equal(a397->b, a658->b)) {
          if (evaluate_predicate(fold(_can_prove(simplifier, a397->a >= a658->a), simplifier))) {
            return max(a397->a - max(a397->b, a658->a), 0);
          }
          if (evaluate_predicate(fold(_can_prove(simplifier, a397->a <= a658->a), simplifier))) {
            return min(max(a397->b, a397->a) - a658->a, 0);
          }
        }
        if (const Add *a842 = a658->b.as<Add>()) {
          if (equal(a397->b, a842->a)) {
            if (is_const(a842->b)) {
              if (evaluate_predicate(fold(_can_prove(simplifier, (a397->a + a842->b) >= a658->a), simplifier))) {
                return max(a397->a - max(a397->b + a842->b, a658->a), fold(0 - a842->b, simplifier));
              }
              if (evaluate_predicate(fold(_can_prove(simplifier, (a397->a + a842->b) <= a658->a), simplifier))) {
                return min(max(a397->b, a397->a) - a658->a, fold(0 - a842->b, simplifier));
              }
            }
          }
          if (equal(a397->a, a842->a)) {
            if (is_const(a842->b)) {
              if (evaluate_predicate(fold(_can_prove(simplifier, (a397->b + a842->b) >= a658->a), simplifier))) {
                return max(a397->b - max(a397->a + a842->b, a658->a), fold(0 - a842->b, simplifier));
              }
              if (evaluate_predicate(fold(_can_prove(simplifier, (a397->b + a842->b) <= a658->a), simplifier))) {
                return min(max(a397->a, a397->b) - a658->a, fold(0 - a842->b, simplifier));
              }
            }
          }
        }
        if (equal(a397->a, a658->b)) {
          if (evaluate_predicate(fold(_can_prove(simplifier, a397->b >= a658->a), simplifier))) {
            return max(a397->b - max(a397->a, a658->a), 0);
          }
          if (evaluate_predicate(fold(_can_prove(simplifier, a397->b <= a658->a), simplifier))) {
            return min(max(a397->a, a397->b) - a658->a, 0);
          }
        }
      }
    }
    if (const Min *a399 = a0->a.as<Min>()) {
      if (equal(a399->a, a0->b)) {
        return min(a399->b - a399->a, 0);
      }
      if (equal(a399->b, a0->b)) {
        return min(a399->a - a399->b, 0);
      }
      if (const Sub *a426 = a399->a.as<Sub>()) {
        if (const IntImm *a427 = a399->b.as<IntImm>()) {
          if (a427->value == 0) {
            if (equal(a426->a, a0->b)) {
              return (0 - max(a426->a, a426->b));
            }
          }
        }
      }
      if (const Add *a434 = a0->b.as<Add>()) {
        if (equal(a399->a, a434->a)) {
          if (equal(a399->b, a434->b)) {
            return (0 - max(a399->b, a399->a));
          }
        }
        if (equal(a399->b, a434->a)) {
          if (equal(a399->a, a434->b)) {
            return (0 - max(a399->a, a399->b));
          }
        }
      }
      if (const Add *a512 = a399->a.as<Add>()) {
        if (equal(a512->a, a0->b)) {
          return min(a399->b - a512->a, a512->b);
        }
        if (equal(a512->b, a0->b)) {
          return min(a399->b - a512->b, a512->a);
        }
        if (const Add *a541 = a512->b.as<Add>()) {
          if (equal(a541->b, a0->b)) {
            return min(a399->b - a541->b, a512->a + a541->a);
          }
          if (equal(a541->a, a0->b)) {
            return min(a399->b - a541->a, a512->a + a541->b);
          }
        }
        if (const Add *a549 = a512->a.as<Add>()) {
          if (equal(a549->b, a0->b)) {
            return min(a399->b - a549->b, a549->a + a512->b);
          }
          if (equal(a549->a, a0->b)) {
            return min(a399->b - a549->a, a549->b + a512->b);
          }
        }
        if (is_const(a512->b)) {
          if (const Min *a674 = a0->b.as<Min>()) {
            if (equal(a512->a, a674->a)) {
              if (evaluate_predicate(fold(_can_prove(simplifier, a399->b <= (a674->b + a512->b)), simplifier))) {
                return min(a399->b - min(a512->a, a674->b), a512->b);
              }
              if (evaluate_predicate(fold(_can_prove(simplifier, a399->b >= (a674->b + a512->b)), simplifier))) {
                return max(min(a512->a + a512->b, a399->b) - a674->b, a512->b);
              }
            }
            if (const Add *a691 = a674->a.as<Add>()) {
              if (equal(a512->a, a691->a)) {
                if (is_const(a691->b)) {
                  if (evaluate_predicate(fold(_can_prove(simplifier, (a399->b + a691->b) <= (a674->b + a512->b)), simplifier))) {
                    return min(a399->b - min(a512->a + a691->b, a674->b), fold(a512->b - a691->b, simplifier));
                  }
                  if (evaluate_predicate(fold(_can_prove(simplifier, (a399->b + a691->b) >= (a674->b + a512->b)), simplifier))) {
                    return max(min(a512->a + a512->b, a399->b) - a674->b, fold(a512->b - a691->b, simplifier));
                  }
                }
              }
            }
            if (equal(a512->a, a674->b)) {
              if (evaluate_predicate(fold(_can_prove(simplifier, a399->b <= (a674->a + a512->b)), simplifier))) {
                return min(a399->b - min(a512->a, a674->a), a512->b);
              }
              if (evaluate_predicate(fold(_can_prove(simplifier, a399->b >= (a674->a + a512->b)), simplifier))) {
                return max(min(a512->a + a512->b, a399->b) - a674->a, a512->b);
              }
            }
            if (const Add *a755 = a674->b.as<Add>()) {
              if (equal(a512->a, a755->a)) {
                if (is_const(a755->b)) {
                  if (evaluate_predicate(fold(_can_prove(simplifier, (a399->b + a755->b) <= (a674->a + a512->b)), simplifier))) {
                    return min(a399->b - min(a512->a + a755->b, a674->a), fold(a512->b - a755->b, simplifier));
                  }
                  if (evaluate_predicate(fold(_can_prove(simplifier, (a399->b + a755->b) >= (a674->a + a512->b)), simplifier))) {
                    return max(min(a512->a + a512->b, a399->b) - a674->a, fold(a512->b - a755->b, simplifier));
                  }
                }
              }
            }
          }
        }
        if (const Mul *a1038 = a512->a.as<Mul>()) {
          if (const Add *a1039 = a1038->a.as<Add>()) {
            if (const Mul *a1040 = a0->b.as<Mul>()) {
              if (equal(a1039->a, a1040->a)) {
                if (equal(a1038->b, a1040->b)) {
                  return min(a399->b - (a1039->a*a1038->b), (a1039->b*a1038->b) + a512->b);
                }
              }
              if (equal(a1039->b, a1040->a)) {
                if (equal(a1038->b, a1040->b)) {
                  return min(a399->b - (a1039->b*a1038->b), (a1039->a*a1038->b) + a512->b);
                }
              }
            }
          }
        }
      }
      if (const Add *a518 = a399->b.as<Add>()) {
        if (equal(a518->a, a0->b)) {
          return min(a399->a - a518->a, a518->b);
        }
        if (equal(a518->b, a0->b)) {
          return min(a399->a - a518->b, a518->a);
        }
        if (const Add *a525 = a518->b.as<Add>()) {
          if (equal(a525->b, a0->b)) {
            return min(a399->a - a525->b, a518->a + a525->a);
          }
          if (equal(a525->a, a0->b)) {
            return min(a399->a - a525->a, a518->a + a525->b);
          }
        }
        if (const Add *a533 = a518->a.as<Add>()) {
          if (equal(a533->b, a0->b)) {
            return min(a399->a - a533->b, a533->a + a518->b);
          }
          if (equal(a533->a, a0->b)) {
            return min(a399->a - a533->a, a533->b + a518->b);
          }
        }
        if (is_const(a518->b)) {
          if (const Min *a706 = a0->b.as<Min>()) {
            if (equal(a518->a, a706->b)) {
              if (evaluate_predicate(fold(_can_prove(simplifier, a399->a <= (a706->a + a518->b)), simplifier))) {
                return min(a399->a - min(a518->a, a706->a), a518->b);
              }
              if (evaluate_predicate(fold(_can_prove(simplifier, a399->a >= (a706->a + a518->b)), simplifier))) {
                return max(min(a518->a + a518->b, a399->a) - a706->a, a518->b);
              }
            }
            if (const Add *a723 = a706->b.as<Add>()) {
              if (equal(a518->a, a723->a)) {
                if (is_const(a723->b)) {
                  if (evaluate_predicate(fold(_can_prove(simplifier, (a399->a + a723->b) <= (a706->a + a518->b)), simplifier))) {
                    return min(a399->a - min(a518->a + a723->b, a706->a), fold(a518->b - a723->b, simplifier));
                  }
                  if (evaluate_predicate(fold(_can_prove(simplifier, (a399->a + a723->b) >= (a706->a + a518->b)), simplifier))) {
                    return max(min(a518->a + a518->b, a399->a) - a706->a, fold(a518->b - a723->b, simplifier));
                  }
                }
              }
            }
            if (equal(a518->a, a706->a)) {
              if (evaluate_predicate(fold(_can_prove(simplifier, a399->a <= (a706->b + a518->b)), simplifier))) {
                return min(a399->a - min(a518->a, a706->b), a518->b);
              }
              if (evaluate_predicate(fold(_can_prove(simplifier, a399->a >= (a706->b + a518->b)), simplifier))) {
                return max(min(a518->a + a518->b, a399->a) - a706->b, a518->b);
              }
            }
            if (const Add *a787 = a706->a.as<Add>()) {
              if (equal(a518->a, a787->a)) {
                if (is_const(a787->b)) {
                  if (evaluate_predicate(fold(_can_prove(simplifier, (a399->a + a787->b) <= (a706->b + a518->b)), simplifier))) {
                    return min(a399->a - min(a518->a + a787->b, a706->b), fold(a518->b - a787->b, simplifier));
                  }
                  if (evaluate_predicate(fold(_can_prove(simplifier, (a399->a + a787->b) >= (a706->b + a518->b)), simplifier))) {
                    return max(min(a518->a + a518->b, a399->a) - a706->b, fold(a518->b - a787->b, simplifier));
                  }
                }
              }
            }
          }
        }
      }
      if (const Min *a556 = a0->b.as<Min>()) {
        if (equal(a399->b, a556->a)) {
          if (equal(a399->a, a556->b)) {
            return 0;
          }
          if (evaluate_predicate(fold(_can_prove(simplifier, a399->a <= a556->b), simplifier))) {
            return min(a399->a - min(a399->b, a556->b), 0);
          }
          if (evaluate_predicate(fold(_can_prove(simplifier, a399->a >= a556->b), simplifier))) {
            return max(min(a399->b, a399->a) - a556->b, 0);
          }
        }
        if (evaluate_predicate(fold(_can_prove(simplifier, (a399->a - a399->b) == (a556->a - a556->b)), simplifier))) {
          return (a399->b - a556->b);
        }
        if (evaluate_predicate(fold(_can_prove(simplifier, (a399->a - a399->b) == (a556->b - a556->a)), simplifier))) {
          return (a399->b - a556->a);
        }
        if (equal(a399->a, a556->a)) {
          if (evaluate_predicate(fold(_can_prove(simplifier, a399->b <= a556->b), simplifier))) {
            return min(a399->b - min(a399->a, a556->b), 0);
          }
          if (evaluate_predicate(fold(_can_prove(simplifier, a399->b >= a556->b), simplifier))) {
            return max(min(a399->a, a399->b) - a556->b, 0);
          }
        }
        if (const Add *a682 = a556->a.as<Add>()) {
          if (equal(a399->a, a682->a)) {
            if (is_const(a682->b)) {
              if (evaluate_predicate(fold(_can_prove(simplifier, (a399->b + a682->b) <= a556->b), simplifier))) {
                return min(a399->b - min(a399->a + a682->b, a556->b), fold(0 - a682->b, simplifier));
              }
              if (evaluate_predicate(fold(_can_prove(simplifier, (a399->b + a682->b) >= a556->b), simplifier))) {
                return max(min(a399->a, a399->b) - a556->b, fold(0 - a682->b, simplifier));
              }
            }
          }
          if (equal(a399->b, a682->a)) {
            if (is_const(a682->b)) {
              if (evaluate_predicate(fold(_can_prove(simplifier, (a399->a + a682->b) <= a556->b), simplifier))) {
                return min(a399->a - min(a399->b + a682->b, a556->b), fold(0 - a682->b, simplifier));
              }
              if (evaluate_predicate(fold(_can_prove(simplifier, (a399->a + a682->b) >= a556->b), simplifier))) {
                return max(min(a399->b, a399->a) - a556->b, fold(0 - a682->b, simplifier));
              }
            }
          }
        }
        if (equal(a399->b, a556->b)) {
          if (evaluate_predicate(fold(_can_prove(simplifier, a399->a <= a556->a), simplifier))) {
            return min(a399->a - min(a399->b, a556->a), 0);
          }
          if (evaluate_predicate(fold(_can_prove(simplifier, a399->a >= a556->a), simplifier))) {
            return max(min(a399->b, a399->a) - a556->a, 0);
          }
        }
        if (const Add *a714 = a556->b.as<Add>()) {
          if (equal(a399->b, a714->a)) {
            if (is_const(a714->b)) {
              if (evaluate_predicate(fold(_can_prove(simplifier, (a399->a + a714->b) <= a556->a), simplifier))) {
                return min(a399->a - min(a399->b + a714->b, a556->a), fold(0 - a714->b, simplifier));
              }
              if (evaluate_predicate(fold(_can_prove(simplifier, (a399->a + a714->b) >= a556->a), simplifier))) {
                return max(min(a399->b, a399->a) - a556->a, fold(0 - a714->b, simplifier));
              }
            }
          }
          if (equal(a399->a, a714->a)) {
            if (is_const(a714->b)) {
              if (evaluate_predicate(fold(_can_prove(simplifier, (a399->b + a714->b) <= a556->a), simplifier))) {
                return min(a399->b - min(a399->a + a714->b, a556->a), fold(0 - a714->b, simplifier));
              }
              if (evaluate_predicate(fold(_can_prove(simplifier, (a399->b + a714->b) >= a556->a), simplifier))) {
                return max(min(a399->a, a399->b) - a556->a, fold(0 - a714->b, simplifier));
              }
            }
          }
        }
        if (equal(a399->a, a556->b)) {
          if (evaluate_predicate(fold(_can_prove(simplifier, a399->b <= a556->a), simplifier))) {
            return min(a399->b - min(a399->a, a556->a), 0);
          }
          if (evaluate_predicate(fold(_can_prove(simplifier, a399->b >= a556->a), simplifier))) {
            return max(min(a399->a, a399->b) - a556->a, 0);
          }
        }
      }
      if (const Mul *a565 = a399->a.as<Mul>()) {
        if (is_const(a565->b)) {
          if (is_const(a399->b)) {
            if (const Mul *a566 = a0->b.as<Mul>()) {
              if (const Min *a567 = a566->a.as<Min>()) {
                if (equal(a565->a, a567->a)) {
                  if (is_const(a567->b)) {
                    if (equal(a565->b, a566->b)) {
                      if (evaluate_predicate(fold(((a565->b > 0) && (a399->b <= (a567->b*a565->b))), simplifier))) {
                        return min(a399->b - (min(a565->a, a567->b)*a565->b), 0);
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
      if (const Min *a1029 = a399->a.as<Min>()) {
        if (const Add *a1030 = a1029->a.as<Add>()) {
          if (equal(a1030->a, a0->b)) {
            return min(min(a1029->b, a399->b) - a1030->a, a1030->b);
          }
        }
        if (const Add *a1034 = a1029->b.as<Add>()) {
          if (equal(a1034->a, a0->b)) {
            return min(min(a1029->a, a399->b) - a1034->a, a1034->b);
          }
        }
      }
    }
    if (const Div *a928 = a0->b.as<Div>()) {
      if (const Add *a929 = a928->a.as<Add>()) {
        if (equal(a0->a, a929->a)) {
          if (is_const(a928->b)) {
            if (evaluate_predicate(fold((a928->b > 0), simplifier))) {
              return ((((a0->a*fold(a928->b - 1, simplifier)) - a929->b) + fold(a928->b - 1, simplifier))/a928->b);
            }
          }
        }
        if (equal(a0->a, a929->b)) {
          if (is_const(a928->b)) {
            if (evaluate_predicate(fold((a928->b > 0), simplifier))) {
              return ((((a0->a*fold(a928->b - 1, simplifier)) - a929->a) + fold(a928->b - 1, simplifier))/a928->b);
            }
          }
        }
      }
      if (const Sub *a932 = a928->a.as<Sub>()) {
        if (equal(a0->a, a932->a)) {
          if (is_const(a928->b)) {
            if (evaluate_predicate(fold((a928->b > 0), simplifier))) {
              return ((((a0->a*fold(a928->b - 1, simplifier)) + a932->b) + fold(a928->b - 1, simplifier))/a928->b);
            }
          }
        }
        if (equal(a0->a, a932->b)) {
          if (is_const(a928->b)) {
            if (evaluate_predicate(fold((a928->b > 0), simplifier))) {
              return ((((a0->a*fold(a928->b + 1, simplifier)) - a932->a) + fold(a928->b - 1, simplifier))/a928->b);
            }
          }
        }
      }
    }
    if (const Div *a940 = a0->a.as<Div>()) {
      if (const Add *a941 = a940->a.as<Add>()) {
        if (is_const(a940->b)) {
          if (equal(a941->a, a0->b)) {
            return (((a941->a*fold(1 - a940->b, simplifier)) + a941->b)/a940->b);
          }
          if (equal(a941->b, a0->b)) {
            return ((a941->a + (a941->b*fold(1 - a940->b, simplifier)))/a940->b);
          }
          if (const Div *a981 = a0->b.as<Div>()) {
            if (const Add *a982 = a981->a.as<Add>()) {
              if (equal(a941->b, a982->a)) {
                if (equal(a941->a, a982->b)) {
                  if (equal(a940->b, a981->b)) {
                    if (evaluate_predicate(fold((a940->b != 0), simplifier))) {
                      return 0;
                    }
                  }
                }
              }
              if (equal(a941->a, a982->a)) {
                if (is_const(a982->b)) {
                  if (equal(a940->b, a981->b)) {
                    if (evaluate_predicate(fold((a940->b > 0), simplifier))) {
                      return ((((a941->a + fold(a982->b % a940->b, simplifier)) % a940->b) + (a941->b - a982->b))/a940->b);
                    }
                  }
                }
              }
            }
            if (equal(a941->a, a981->a)) {
              if (equal(a940->b, a981->b)) {
                if (evaluate_predicate(fold((a940->b > 0), simplifier))) {
                  return (((a941->a % a940->b) + a941->b)/a940->b);
                }
              }
            }
          }
        }
        if (const Add *a974 = a941->a.as<Add>()) {
          if (is_const(a940->b)) {
            if (const Div *a975 = a0->b.as<Div>()) {
              if (const Add *a976 = a975->a.as<Add>()) {
                if (const Add *a977 = a976->a.as<Add>()) {
                  if (equal(a974->b, a977->a)) {
                    if (equal(a974->a, a977->b)) {
                      if (equal(a940->b, a975->b)) {
                        if (evaluate_predicate(fold((a940->b > 0), simplifier))) {
                          return ((((a974->a + a974->b) + a941->b)/a940->b) - (((a974->a + a974->b) + a976->b)/a940->b));
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
        if (is_const(a941->b)) {
          if (is_const(a940->b)) {
            if (const Div *a991 = a0->b.as<Div>()) {
              if (const Add *a992 = a991->a.as<Add>()) {
                if (equal(a941->a, a992->a)) {
                  if (equal(a940->b, a991->b)) {
                    if (evaluate_predicate(fold((a940->b > 0), simplifier))) {
                      return (((fold((a940->b + a941->b) - 1, simplifier) - a992->b) - ((a941->a + fold(a941->b % a940->b, simplifier)) % a940->b))/a940->b);
                    }
                  }
                }
              }
              if (const Sub *a1002 = a991->a.as<Sub>()) {
                if (equal(a941->a, a1002->a)) {
                  if (equal(a940->b, a991->b)) {
                    if (evaluate_predicate(fold((a940->b > 0), simplifier))) {
                      return (((a1002->b + fold((a940->b + a941->b) - 1, simplifier)) - ((a941->a + fold(a941->b % a940->b, simplifier)) % a940->b))/a940->b);
                    }
                  }
                }
              }
            }
          }
        }
        if (const Min *a1062 = a941->a.as<Min>()) {
          if (const Add *a1063 = a1062->a.as<Add>()) {
            if (const Mul *a1064 = a1063->a.as<Mul>()) {
              if (is_const(a1064->b)) {
                if (is_const(a940->b)) {
                  if (const Mul *a1065 = a0->b.as<Mul>()) {
                    if (equal(a1064->a, a1065->a)) {
                      if (is_const(a1065->b)) {
                        if (evaluate_predicate(fold((a1064->b == (a940->b*a1065->b)), simplifier))) {
                          return ((min(a1063->b, a1062->b - (a1064->a*a1064->b)) + a941->b)/a940->b);
                        }
                      }
                    }
                  }
                }
              }
            }
          }
          if (const Add *a1070 = a1062->b.as<Add>()) {
            if (const Mul *a1071 = a1070->a.as<Mul>()) {
              if (is_const(a1071->b)) {
                if (is_const(a940->b)) {
                  if (const Mul *a1072 = a0->b.as<Mul>()) {
                    if (equal(a1071->a, a1072->a)) {
                      if (is_const(a1072->b)) {
                        if (evaluate_predicate(fold((a1071->b == (a940->b*a1072->b)), simplifier))) {
                          return ((min(a1062->a - (a1071->a*a1071->b), a1070->b) + a941->b)/a940->b);
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
      if (const Sub *a947 = a940->a.as<Sub>()) {
        if (is_const(a940->b)) {
          if (equal(a947->a, a0->b)) {
            return (((a947->a*fold(1 - a940->b, simplifier)) - a947->b)/a940->b);
          }
          if (equal(a947->b, a0->b)) {
            return ((a947->a - (a947->b*fold(1 + a940->b, simplifier)))/a940->b);
          }
          if (const Div *a996 = a0->b.as<Div>()) {
            if (const Add *a997 = a996->a.as<Add>()) {
              if (equal(a947->a, a997->a)) {
                if (is_const(a997->b)) {
                  if (equal(a940->b, a996->b)) {
                    if (evaluate_predicate(fold((a940->b > 0), simplifier))) {
                      return (((((a947->a + fold(a997->b % a940->b, simplifier)) % a940->b) - a947->b) - a997->b)/a940->b);
                    }
                  }
                }
              }
            }
            if (equal(a947->a, a996->a)) {
              if (equal(a940->b, a996->b)) {
                if (evaluate_predicate(fold((a940->b > 0), simplifier))) {
                  return (((a947->a % a940->b) - a947->b)/a940->b);
                }
              }
            }
          }
        }
      }
      if (is_const(a940->b)) {
        if (const Div *a1005 = a0->b.as<Div>()) {
          if (const Add *a1006 = a1005->a.as<Add>()) {
            if (equal(a940->a, a1006->a)) {
              if (equal(a940->b, a1005->b)) {
                if (evaluate_predicate(fold((a940->b > 0), simplifier))) {
                  return (((fold(a940->b - 1, simplifier) - a1006->b) - (a940->a % a940->b))/a940->b);
                }
              }
            }
          }
          if (const Sub *a1014 = a1005->a.as<Sub>()) {
            if (equal(a940->a, a1014->a)) {
              if (equal(a940->b, a1005->b)) {
                if (evaluate_predicate(fold((a940->b > 0), simplifier))) {
                  return (((a1014->b + fold(a940->b - 1, simplifier)) - (a940->a % a940->b))/a940->b);
                }
              }
            }
          }
        }
      }
      if (const Min *a1049 = a940->a.as<Min>()) {
        if (const Add *a1050 = a1049->a.as<Add>()) {
          if (const Mul *a1051 = a1050->a.as<Mul>()) {
            if (is_const(a1051->b)) {
              if (is_const(a940->b)) {
                if (const Mul *a1052 = a0->b.as<Mul>()) {
                  if (equal(a1051->a, a1052->a)) {
                    if (is_const(a1052->b)) {
                      if (evaluate_predicate(fold((a1051->b == (a940->b*a1052->b)), simplifier))) {
                        return (min(a1050->b, a1049->b - (a1051->a*a1051->b))/a940->b);
                      }
                    }
                  }
                }
              }
            }
          }
        }
        if (const Add *a1056 = a1049->b.as<Add>()) {
          if (const Mul *a1057 = a1056->a.as<Mul>()) {
            if (is_const(a1057->b)) {
              if (is_const(a940->b)) {
                if (const Mul *a1058 = a0->b.as<Mul>()) {
                  if (equal(a1057->a, a1058->a)) {
                    if (is_const(a1058->b)) {
                      if (evaluate_predicate(fold((a1057->b == (a940->b*a1058->b)), simplifier))) {
                        return (min(a1056->b, a1049->a - (a1057->a*a1057->b))/a940->b);
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  return expr;
}


} // namespace Internal
} // namespace Halide

#endif  // HALIDE_SIMPLIFY_SUB_HELPER_H
