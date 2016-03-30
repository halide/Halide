all: libsim_qurt.a

sim_qurt.o: sim_qurt.cpp
	$(CXX) $(CXXFLAGS) -O3 -c sim_qurt.cpp -o sim_qurt.o

libsim_qurt.a: sim_qurt.o
	ar rcs libsim_qurt.a sim_qurt.o

clean:
	rm -rf sim_qurt.o libsim_qurt.a
