viz: viz.cpp
	g++ -O3 -Wall viz.cpp -o viz -lglut -lpng -I /opt/local/include -L /opt/local/lib -lgl 

clean:
	rm viz

%.mp4:
	avconv -i $*.txt_movie_%05d.png -vcodec libx264 -crf 10 $@

blur_example: blur/blur.cpp blur/test.cpp
	g++ -Wall blur/blur.cpp -I ../../cpp_bindings/ -L ../../cpp_bindings/ -lHalide -ldl -lpthread -o gen_blur
	HL_DISABLE_BOUNDS_CHECKING=1 HL_TRACE=2 ./gen_blur 0
	HL_DISABLE_BOUNDS_CHECKING=1 HL_TRACE=2 ./gen_blur 1
	HL_DISABLE_BOUNDS_CHECKING=1 HL_TRACE=2 ./gen_blur 2
	HL_DISABLE_BOUNDS_CHECKING=1 HL_TRACE=2 ./gen_blur 3
	HL_DISABLE_BOUNDS_CHECKING=1 HL_TRACE=2 ./gen_blur 4
	HL_DISABLE_BOUNDS_CHECKING=1 HL_TRACE=2 ./gen_blur 5
	HL_DISABLE_BOUNDS_CHECKING=1 HL_TRACE=2 ./gen_blur 6
	HL_DISABLE_BOUNDS_CHECKING=1 HL_TRACE=2 ./gen_blur 7
	HL_DISABLE_BOUNDS_CHECKING=1 HL_TRACE=2 ./gen_blur 8
	HL_DISABLE_BOUNDS_CHECKING=1 HL_TRACE=2 ./gen_blur 9
	HL_DISABLE_BOUNDS_CHECKING=1 HL_TRACE=2 ./gen_blur 10
	llc blur_0.bc -O3 -o blur_0.o -filetype=obj
	llc blur_1.bc -O3 -o blur_1.o -filetype=obj
	llc blur_2.bc -O3 -o blur_2.o -filetype=obj
	llc blur_3.bc -O3 -o blur_3.o -filetype=obj
	llc blur_4.bc -O3 -o blur_4.o -filetype=obj
	llc blur_5.bc -O3 -o blur_5.o -filetype=obj
	llc blur_6.bc -O3 -o blur_6.o -filetype=obj
	llc blur_7.bc -O3 -o blur_7.o -filetype=obj
	llc blur_8.bc -O3 -o blur_8.o -filetype=obj
	llc blur_9.bc -O3 -o blur_9.o -filetype=obj
	llc blur_10.bc -O3 -o blur_10.o -filetype=obj
	g++ blur_*.o blur/test.cpp -I. -lpthread -o blur_example


blur_logs: blur_example
	./blur_example 0 2> blur_log_0 
	./blur_example 1 2> blur_log_1 
	./blur_example 2 2> blur_log_2 
	./blur_example 3 2> blur_log_3 
	./blur_example 4 2> blur_log_4 
	./blur_example 5 2> blur_log_5 
	./blur_example 6 2> blur_log_6 
	./blur_example 7 2> blur_log_7 
	./blur_example 8 2> blur_log_8 
	./blur_example 9 2> blur_log_9 
	./blur_example 10 2> blur_log_10

blur_viz_%: 
	./viz 1280 480 blur_log_$* .m0 input 32 60 24 blur_y_$*.blur_x_$* "blurred in x" 448 60 24 blur_y_$* output 864 60 24

blur_output_only_%:
	./viz 480 480 blur_log_$* blur_y_$* "" 40 40 24