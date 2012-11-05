viz: viz.cpp
	g++ -O3 -Wall viz.cpp -o viz -lglut -lpng

clean:
	rm viz

%.mp4:
	avconv -i $*.txt_movie_%05d.png -vcodec libx264 -crf 10 $@
