autoschedulers="master greedy beam mcts"

if [ "$#" -lt 1 ] || [ "$1" != "--improved" ]; then
    improved=""
else
    improved="--improved"
fi

echo > progress

for autoscheduler in $autoschedulers; do
    echo $autoscheduler >> progress
    ./generate_apps_results.sh $autoscheduler $improved

    if [ $? -ne 0 ]; then
        echo "Failed to get results for $autoscheduler"
        exit 1
    fi
done

cd plots
./extract > results.csv

