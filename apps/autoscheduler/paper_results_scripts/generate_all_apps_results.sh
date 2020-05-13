autoschedulers="master greedy beam mcts"

if [ $# -lt 1 ]; then
    weights=""
elif  [ "$1" == "--improved" ]; then
    weights="--improved"
elif  [ "$1" == "--value_func" ]; then
    weights="--value_func"
else 
    echo "Invalid command line option!"
fi

echo > progress

for autoscheduler in $autoschedulers; do
    echo $autoscheduler >> progress
    ./generate_apps_results.sh $autoscheduler $weights

    if [ $? -ne 0 ]; then
        echo "Failed to get results for $autoscheduler"
        exit 1
    fi
done

cd plots
./extract > results.csv

