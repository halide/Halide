viz: viz.cpp
	g++ -O3 -Wall viz.cpp -o viz -lglut -lpng

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
	llc blur_0.bc -O3 -o blur_0.o -filetype=obj
	llc blur_1.bc -O3 -o blur_1.o -filetype=obj
	llc blur_2.bc -O3 -o blur_2.o -filetype=obj
	llc blur_3.bc -O3 -o blur_3.o -filetype=obj
	llc blur_4.bc -O3 -o blur_4.o -filetype=obj
	llc blur_5.bc -O3 -o blur_5.o -filetype=obj
	g++ blur_*.o blur/test.cpp -I. -lpthread -o blur_example


blur_logs: blur_example
	./blur_example 0 2> tmp
	./last_fifth.sh tmp > blur_log_0
	./blur_example 1 2> tmp
	./last_fifth.sh tmp > blur_log_1
	./blur_example 2 2> tmp
	./last_fifth.sh tmp > blur_log_2
	./blur_example 3 2> tmp
	./last_fifth.sh tmp > blur_log_3
	./blur_example 4 2> tmp
	./last_fifth.sh tmp > blur_log_4
	./blur_example 5 2> tmp
	./last_fifth.sh tmp > blur_log_5

blur_viz_%: 
	./viz 1280 480 blur_log_$* .m0 input 32 50 16 blur_y_$*.blur_x_$* "blurred in x" 448 50 16 blur_y_$* output 864 50 16