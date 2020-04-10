autoschedulers="greedy beam mcts"

for autoscheduler in $autoschedulers; do
    ./generate_apps_results.sh $autoscheduler
done

cd plots
./extract > results.csv

