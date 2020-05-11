autoschedulers="master greedy beam mcts"

if [ "$#" -lt 1 ] || [ "$1" != "--improved" ]; then
    improved=""
else
    improved="--improved"
fi

echo > autoallprogress

for autoscheduler in $autoschedulers; do
    echo $autoscheduler >> autoallprogress
    RETRAIN=false ./generate_autotune_results.sh $autoscheduler $improved

    if [ $? -ne 0 ]; then
        echo "Failed to get autotuning results for $autoscheduler"
        exit 1
    fi
done

cd plots
./extract > results.csv

