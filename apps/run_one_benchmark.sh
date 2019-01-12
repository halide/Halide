# ./run_one_benchmark.sh <folder>
# The results of execution will be in the file $OUTPUT_FILE

if [ "$#" -ne 1 ]; then
    echo "./run_one_benchmark.sh <folder>"
    exit
fi

OUTPUT_FILE=ONE_RUN_RESULT

BENCH=$1;

cd $BENCH;

rm -rf $OUTPUT_FILE
make -B test &> $OUTPUT_FILE;
grep "Manually-tuned time" $OUTPUT_FILE
grep "Classic auto-scheduled time" $OUTPUT_FILE
grep "Auto-scheduled" $OUTPUT_FILE

cd -
