#!/bin/bash

PIPELINES=${1:-5}
SCHEDULES=${2:-32}
START_FROM=${3:-0}
HL_RANDOM_DROPOUT=${4:-1}
HL_BEAM_SIZE=${5:-1}
NUM_CORES=${6:-8}
ADAMS2019_DIR=@adams2019_BINARY_DIR@
INITIAL_WEIGHTS=${7:-$ADAMS2019_DIR/baseline.weights}
PROGRAM_NAME=`basename $0 .sh`
LOGFILEBASE=${8:-${PROGRAM_NAME}.log}
TIME_ME=${9:-true}
DATE=${10:-`date +'%F-%H-%M-%S'`}

LOG_DIR="./logs"
LOGFILE="$LOG_DIR/$LOGFILEBASE.$DATE"
TIME_TMP_LOG="$LOGFILEBASE.$DATE.tmp"

if [ "$TIME_ME" = true ]; then
  time /usr/bin/time -f "\nreal\t%E\nuser\t%U\nsys\t%S" -o $TIME_TMP_LOG \
    $0 $PIPELINES $SCHEDULES $START_FROM $HL_RANDOM_DROPOUT $HL_BEAM_SIZE \
       $NUM_CORES $INITIAL_WEIGHTS $LOGFILEBASE false $DATE
  cat $TIME_TMP_LOG >> $LOGFILE
  rm $TIME_TMP_LOG
  exit
fi

# Start a watchdog to kill any compilations that take too long
./watchdog.sh &
WATCHDOG_PID=$!

function finish {
  kill $WATCHDOG_PID
}
trap finish EXIT

printf "Running %s with the following parameters:\n\
        PIPELINES=%d\n\
        SCHEDULES=%d\n\
        START_FROM=%d\n\
        HL_RANDOM_DROPOUT=%d\n\
        HL_BEAM_SIZE=%d\n\
	NUM_CORES=%d\n\
        INITIAL_WEIGHTS=%s\n\
        LOGFILEBASE=%s\n " $PROGRAM_NAME $PIPELINES $SCHEDULES $START_FROM \
                           $HL_RANDOM_DROPOUT $HL_BEAM_SIZE $NUM_CORES $INITIAL_WEIGHTS \
                           $LOGFILEBASE | tee -a $LOGFILE

b=0
HL_TARGET=$($ADAMS2019_DIR/get_host_target)
WEIGHTS_OUT=./updated.weights
mkdir -p $LOG_DIR

if [ -d "./bin" ]; then
  # Don't clobber existing samples
  FIRST=$(ls ./bin | cut -d_ -f2 | sort -n | tail -n1)
  # Change initial weights to the updated.weights in FIRST directory
  P=$FIRST # ignore b*$PIPELINES term for now
  STAGES=$(((P % 30) + 10))
  INITIAL_WEIGHTS=`pwd`/bin/pipeline_${P}_${STAGES}/updated.weights
else
  mkdir -p bin
  FIRST=$((START_FROM-1))
fi

# Build lots of pipelines
for ((p=$((FIRST+1));p<$((FIRST+PIPELINES+1));p++)); do
  P=$((b * $PIPELINES + p))
  STAGES=$(((P % 30) + 10))
  mkdir -p bin/pipeline_${P}_${STAGES}

  # First, renerate and compile all the schedules
  for ((s=0;s<$SCHEDULES;s++)); do
    PIPELINE_DIR=./bin/pipeline_${P}_${STAGES}/schedule_${s}_${HL_RANDOM_DROPOUT}_${HL_BEAM_SIZE}_0
    echo "export HL_SEED=$s && \
    export HL_RANDOM_DROPOUT=$HL_RANDOM_DROPOUT && \
    export HL_BEAM_SIZE=$HL_BEAM_SIZE && \
    export HL_WEIGHTS_DIR=$INITIAL_WEIGHTS && \
    mkdir -p $PIPELINE_DIR && \
    ./random_pipeline.generator -n random_pipeline -d 0 -g random_pipeline -f random_pipeline \
       -e c_header,object,schedule,python_schedule,static_library,registration,featurization \
       -o $PIPELINE_DIR -p $ADAMS2019_DIR/libautoschedule_adams2019.so \
       target=${HL_TARGET}-no_runtime auto_schedule=true seed=$P max_stages=$STAGES && \
       @CMAKE_CXX_COMPILER@ -std=c++17 -O3 -DNDEBUG -I@Halide_BINARY_DIR@/include @Halide_SOURCE_DIR@/tools/RunGenMain.cpp \
         $PIPELINE_DIR/random_pipeline.registration.cpp \
         $PIPELINE_DIR/random_pipeline.a librandom_pipeline.runtime.a \
         -o $PIPELINE_DIR/bench -DHALIDE_NO_PNG -DHALIDE_NO_JPEG  -pthread -ldl" | tee -a $LOGFILE
  done | xargs -P16 -I{} bash -c "{}" | tee -a $LOGFILE

  echo Benchmarking schedules | tee -a $LOGFILE
  # Now let's benchmark them
  for ((s=0;s<$SCHEDULES;s++)); do
    PIPELINE_DIR=./bin/pipeline_${P}_${STAGES}/schedule_${s}_${HL_RANDOM_DROPOUT}_${HL_BEAM_SIZE}_0
    echo $PIPELINE_DIR | tee -a $LOGFILE
    benchmark_time=$($PIPELINE_DIR/bench --estimate_all --benchmarks=all)
    echo $benchmark_time | tee -a $LOGFILE
    t=$(echo $benchmark_time | head -1 | awk '{print $8}')
    id=$(printf "%04d%02d%02d%02d%02d" $P $STAGES $s $HL_RANDOM_DROPOUT $HL_BEAM_SIZE)
    echo Producing featurization for ID: $id | tee -a $LOGFILE
    $ADAMS2019_DIR/featurization_to_sample $PIPELINE_DIR/random_pipeline.featurization $t onnx $id $PIPELINE_DIR/random_pipeline.sample
  done

  echo Retraining weights | tee -a $LOGFILE
  cd bin/pipeline_${P}_${STAGES} 
  find . -name "*.sample" | $ADAMS2019_DIR/retrain_cost_model  --epochs=4 --rates=0.001 --num_cores=$NUM_CORES \
     --initial_weights=$INITIAL_WEIGHTS --weights_out=$WEIGHTS_OUT --best_benchmark=./best.onnx.benchmark.txt \
     --best_schedule=./best.onnx.schedule.h --best_python_schedule=./best_onnx_schedule.py | tee -a ../../$LOGFILE
  INITIAL_WEIGHTS=`pwd`/updated.weights
  cd ../..
done
