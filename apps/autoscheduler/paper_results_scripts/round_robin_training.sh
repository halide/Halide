# This script looks for all samples under the current directory, and generates a set of weights per app, with just that app excluded from the training set

RETRAIN=$(dirname $0)/../bin/retrain_cost_model

APPS="bgu bilateral_grid camera_pipe conv_layer cuda_mat_mul depthwise_separable_conv harris hist iir_blur interpolate lens_blur local_laplacian max_filter mobilenet0 mobilenet1 mobilenet2 mobilenet3 mobilenet4 mobilenet5 mobilenet6 mobilenet7 nl_means stencil_chain unsharp"

EPOCHS_PER_TURN=100

for app in $APPS; do
    echo "Generating initial weights for" $app
    (find . -name *.sample | grep -v ${app/[0-9]*} | HL_NUM_THREADS=2 $RETRAIN  --epochs=${EPOCHS_PER_TURN} --rates=0.0001 --randomize_weights=1 --num_cores=32 --weights_out=${app}_${i}.weights --best_benchmark=${app}.best_benchmark.txt --best_schedule=${app}.best_schedule.h --predictions_file= --verbose=0 --partition_schedules=1 >${app}_stdout.txt 2>${app}_stderr.txt; cp ${app}_${i}.weights ${app}.weights) &
done
wait
    
for ((i=0;i<1000;i++)); do 
    echo Round $i
    for app in $APPS; do
        (find . -name *.sample | grep -v ${app/[0-9]*} | HL_NUM_THREADS=2 $RETRAIN  --epochs=${EPOCHS_PER_TURN} --rates=0.0001 --initial_weights=${app}.weights --num_cores=32 --weights_out=${app}_${i}.weights --best_benchmark=${app}.best_benchmark.txt --best_schedule=${app}.best_schedule.h --predictions_file= --verbose=0 --partition_schedules=1 >${app}_${i}_stdout.txt 2>${app}_${i}_stderr.txt; cp ${app}_${i}.weights ${app}.weights) &
    done
    wait
    for app in $APPS; do
        echo 
        echo $app $i
        tail -n4 ${app}_${i}_stdout.txt | head -n1
    done
done
