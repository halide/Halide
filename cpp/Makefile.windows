HEADERS = FImage.h Compiler.h X64.h IRNode.h CImg.h
OBJECTS = tests.obj Compiler.obj FImage.obj IRNode.obj

tests.exe: $(OBJECTS) 
	cl.exe $(OBJECTS) /link winmm.lib ws2_32.lib user32.lib gdi32.lib shell32.lib lib/libjpeg-x64-static-mt.lib lib/libpng-x64-static-mt.lib lib/libtiff-x64-static-mt.lib lib/zlib-x64-static-mt.lib

%.obj: %.cpp $(HEADERS)
	cl.exe /Ox /fp:fast /we4267 /we4244 /favor:INTEL64 /EHsc /c $< /I include

clean:
	rm $(OBJECTS) tests.exe