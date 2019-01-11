# ./explore_gpu_parameters.sh <folder>
# The results of execution will be in the file OUTPUT_FILE
# Search for START_NEW_RUN to recognize the beginning of each run.
# Before running this script make sure to remove the lines that define
# $MACHINE_PARAMS_SIZE and $HL_SHARED_MEMORY_LIMIT from the Makefiles

if [ "$#" -ne 1 ]; then
    echo "./explore_gpu_parameters.sh <folder>"
    exit
fi

OUTPUT_FILE=EXPLORAION_RESULT

# Explore different values for HL_MACHINE_PARAMS. The value explored are
# in $MACHINE_PARAMS_SIZE.
EXPLORE_MACHINE_PARAMS=1
MACHINE_PARAMS_SIZE="32 64 96 128 256 512"

# Explore different values for HL_MACHINE_PARAMS. The value explored are
# in $MACHINE_PARAMS_SIZE.
EXPLORE_SHARED_MEMORY_SIZE=0
SHARED_MEM_SIZES="2 8 16 32 48"

BENCH=$1;

cd $BENCH;
echo "BEGIN" > $OUTPUT_FILE;

if [ "${EXPLORE_MACHINE_PARAMS}" -ne 0 ]; then
	for i in ${MACHINE_PARAMS_SIZE}; do
		echo "	Exploring HL_MACHINE_PARAMS=$i,1,1 on $BENCH";
		echo "START_NEW_RUN for HL_MACHINE_PARAMS=$i,1,1" >> $OUTPUT_FILE;
		HL_MACHINE_PARAMS=$i,1,1 make -B test &>> $OUTPUT_FILE;	
		grep "START_NEW_RUN" $OUTPUT_FILE 
		grep "Manually-tuned time" $OUTPUT_FILE
		grep "Classic auto-scheduled time" $OUTPUT_FILE
		grep "Auto-scheduled" $OUTPUT_FILE
	done
fi

if [ "${EXPLORE_SHARED_MEMORY_SIZE}" -ne 0 ]; then
	for i in ${SHARED_MEM_SIZES}; do
		echo "	Exploring HL_SHARED_MEMORY_LIMIT=$i on $BENCH";
		echo "START_NEW_RUN for HL_SHARED_MEMORY_LIMIT=$i" >> $OUTPUT_FILE;
		HL_SHARED_MEMORY_LIMIT=$i make -B test &>> $OUTPUT_FILE;
		grep "START_NEW_RUN" $OUTPUT_FILE 
		grep "Manually-tuned time" $OUTPUT_FILE
		grep "Classic auto-scheduled time" $OUTPUT_FILE
		grep "Auto-scheduled" $OUTPUT_FILE
	done
fi

cd -
