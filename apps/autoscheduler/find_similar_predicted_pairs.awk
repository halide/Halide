#!/bin/awk -f

function abs(x) {return x < 0 ?  -x : x;}
function abs_avg(x, y) {return abs((x + y)/2.0);}

BEGIN{}

{
  pred_diff = abs($2 - prev_prediction) / abs_avg($2, prev_prediction);
  actual_diff = abs($3 - prev_actual) / abs_avg($3, prev_actual);
  printf("%s %s %f %f %f %f %f %f %f\n", $1, prev_filename, $2, prev_prediction, $3, prev_actual, pred_diff, actual_diff, actual_diff / pred_diff);
}

{ prev_filename = $1 }
{ prev_prediction = $2 }
{ prev_actual = $3 }

END {}
