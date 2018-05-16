echo "Starting watchdog"
while [ 1 ]; do
    sleep 10
    ps -a | grep '[^0]:.. random_pipeline' | cut -d' ' -f1 | xargs kill
done

