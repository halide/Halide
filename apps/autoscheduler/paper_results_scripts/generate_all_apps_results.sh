autoschedulers="greedy beam mcts"

echo > progress

for autoscheduler in $autoschedulers; do
    echo $autoscheduler >> progress
    ./generate_apps_results.sh $autoscheduler
done

cd plots
./extract > results.csv

