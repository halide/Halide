bin/libauto_schedule.so: AutoSchedule.cpp
	mkdir -p bin
	$(CXX) -fPIC -shared AutoSchedule.cpp -rdynamic -I ../../distrib/include -L ../../distrib/bin -lHalide -o bin/libauto_schedule.so

clean:
	rm -rf bin
