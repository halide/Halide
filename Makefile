HEADERS = FImage.h Compiler.h X64.h IRNode.h
OBJECTS = tests.obj Compiler.obj FImage.obj

tests.exe: $(OBJECTS) 
	cl.exe $(OBJECTS) /Ox /link ImageStack-x64-static-mt.lib /LTCG winmm.lib ws2_32.lib user32.lib

%.obj: %.cpp $(HEADERS)
	cl.exe /EHsc /Ox /c $< /DWIN32 /I ../../ImageStack/src
