echo "Starting watchdog"
while [ 1 ]; do
    sleep 10
    ps -a | grep '[^0]:.. random_pipeline' | cut -dp -f1 | xargs kill 2>/dev/null
done

