HEADERS = FImage.h Compiler.h X64.h IRNode.h CImg.h
OBJECTS = tests.obj Compiler.obj FImage.obj

tests.exe: $(OBJECTS) 
	cl.exe $(OBJECTS) /Ox /link /LTCG winmm.lib ws2_32.lib user32.lib gdi32.lib shell32.lib lib/libjpeg-x64-static-mt.lib lib/libpng-x64-static-mt.lib lib/libtiff-x64-static-mt.lib

%.obj: %.cpp $(HEADERS)
	cl.exe /EHsc /Ox /c $< /I include

clean:
	rm $(OBJECTS) tests.exe