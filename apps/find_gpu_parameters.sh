# ./find_gpu_parameters.sh <folder>
# The results of execution will be in the file TUNING_RESULT
# Search for START_NEW_RUN to recognize the beginning of each run.
# Before running this script make sure to remove the lines that define
# $MACHINE_PARAMS_SIZE and $HL_SHARED_MEMORY_LIMIT from the Makefiles

# Explore different values for HL_MACHINE_PARAMS. The value explored are
# in $MACHINE_PARAMS_SIZE.
EXPLORE_MACHINE_PARAMS=1
MACHINE_PARAMS_SIZE=32 64 96 128 256 512

# Explore different values for HL_MACHINE_PARAMS. The value explored are
# in $MACHINE_PARAMS_SIZE.
EXPLORE_SHARED_MEMORY_SIZE=1
SHARED_MEM_SIZE=2 8 16 32 64 96 128

export BENCH=$1;

cd $BENCH;
echo "BEGIN" > TUNING_RESULT;

for i in ${MACHINE_PARAMS_SIZE}; do
	echo "START_NEW_RUN for HL_MACHINE_PARAMS=$i,1,1" >> TUNING_RESULT;
	HL_MACHINE_PARAMS=$i,1,1 make -B test &>> TUNING_RESULT;
done
