for ((i=1000;i<2000;i++)); do
    rm f5.bc
    ./a.out small.tmp output.tmp 10 1 1 $i
    llc -O3 f5.bc -filetype=obj -o f5_${i}.o
    g++-4.6 -O3 test.cpp f5_${i}.o -o test_${i}
done

for ((i=1000;i<2000;i++)); do
    ./test_${i} small.tmp output.tmp 8 1 1 | xargs echo $i
done > times.txt