#!/bin/bash

APPS="harris local_laplacian unsharp bilateral_grid camera_pipe nl_means stencil_chain iir_blur interpolate max_filter lens_blur resnet_50 resize"

rm names.csv
touch names.csv

# Assumes that run_all_experiments.sh has run
for app in $APPS; do
    echo $app ...
    echo "ours,baseline,ratio" > ${app}_runtime.csv
    echo "ours,baseline,ratio" > ${app}_peak_memory.csv
    echo "ours,baseline,ratio" > ${app}_compile_time.csv
    echo "ours,baseline,ratio" > ${app}_proof_failures.csv
    echo "ours,baseline,ratio" > ${app}_non_monotonic.csv
    echo "ours,baseline,ratio" > ${app}_code_size.csv
    for ((i=0;i<256;i++)); do
        echo -n .
        A=$(grep BEST ../${app}/results/${i}/benchmark_stdout.txt | cut -d' ' -f5)
        B=$(grep BEST ../${app}/results_baseline/${i}/benchmark_stdout.txt | cut -d' ' -f5)        
        # If the baseline crashes we get a pass
        if [ -z $B ]; then A=0; B=0; fi
        R=$(echo "scale=3; ${A}00001/${B}00001" | bc)
        echo "$A,$B,$R" >> ${app}_runtime.csv
        
        A=$(grep memory ../${app}/results/${i}/memory_stdout.txt | cut -d' ' -f4)
        B=$(grep memory ../${app}/results_baseline/${i}/memory_stdout.txt | cut -d' ' -f4)        
        if [ -z $B ]; then A=0; B=0; fi
        R=$(echo "scale=3; ${A}.00001/${B}.00001" | bc)
        echo "${A}.0,${B}.0,${R}" >> ${app}_peak_memory.csv
        
        A1=$(grep Lower.cpp ../${app}/results/${i}/stderr.txt | cut -d' ' -f5)
        B1=$(grep Lower.cpp ../${app}/results_baseline/${i}/stderr.txt | cut -d' ' -f5)        
        
        A2=$(grep CodeGen_LLVM.cpp ../${app}/results/${i}/stderr.txt | cut -d' ' -f5)
        B2=$(grep CodeGen_LLVM.cpp ../${app}/results_baseline/${i}/stderr.txt | cut -d' ' -f5)        
        
        A3=$(grep LLVM_Output.cpp ../${app}/results/${i}/stderr.txt | cut -d' ' -f5 | head -n1)
        B3=$(grep LLVM_Output.cpp ../${app}/results_baseline/${i}/stderr.txt | cut -d' ' -f5 | head -n1)        
        A=$(echo "$A1 + $A2 + $A3" | bc)
        B=$(echo "$B1 + $B2 + $B3" | bc)        

        R=$(echo "scale=3; ${A}00001/${B}00001" | bc)
        echo "$A,$B,$R" >> ${app}_compile_time.csv
        
        A=$(grep 'Failed to prove' -A1 ../${app}/results/${i}/stderr.txt | grep '(' | sort | uniq | wc -l)
        B=$(grep 'Failed to prove' -A1 ../${app}/results_baseline/${i}/stderr.txt | grep '(' | sort | uniq | wc -l)        
        R=$(echo "scale=3; ${A}.00001/${B}.00001" | bc)
        echo "${A}.0,${B}.0,$R" >> ${app}_proof_failures.csv
        
        A=$(grep 'non-monotonic' ../${app}/results/${i}/stderr.txt | wc -l)
        B=$(grep 'non-monotonic' ../${app}/results_baseline/${i}/stderr.txt | wc -l)        
        R=$(echo "scale=3; ${A}.00001/${B}.00001" | bc)
        echo "${A}.0,${B}.0,$R" >> ${app}_non_monotonic.csv
        
        A=$(ls -l ../${app}/results/${i}/${app}.a | cut -d' ' -f5)
        B=$(ls -l ../${app}/results_baseline/${i}/${app}.a | cut -d' ' -f5)        
        R=$(echo "scale=3; ${A}.00001/${B}.00001" | bc)
        echo "${A}.0,${B}.0,$R" >> ${app}_code_size.csv

        echo ${app}_${i} >> names.csv
    done
    echo
done

echo $APPS | sed 's/ /,,,/g' > header.csv

cp header.csv results.csv

STATS="runtime peak_memory compile_time proof_failures non_monotonic code_size"

for sheet in $STATS; do
    echo ${sheet} > results_${sheet}.csv
    cat header.csv >> results_${sheet}.csv
    ARGS=$(for app in $APPS; do echo ${app}_${sheet}.csv; done)
    paste -d, $ARGS >> results_${sheet}.csv

    # Get the ratios alone in a standalone sheet
    for app in $APPS; do cut -d, -f3 ${app}_${sheet}.csv | grep -v ratio; done > ratios_${sheet}.csv
done



echo names $STATS | sed 's/ /,/g' > ratios.csv
paste -d, names.csv $(for sheet in $STATS; do echo ratios_${sheet}.csv; done) >> ratios.csv
