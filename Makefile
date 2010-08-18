test: generated.txt test.txt
	diff generated.txt test.txt

generated.txt: generated.obj
	dumpbin /disasm:bytes generated.obj | grep "^  " | cut -d: -f2 > generated.txt

test.txt: test.obj
	dumpbin /disasm:bytes test.obj | grep "^  " | cut -d: -f2 > test.txt

generated.obj: testX64
	./testX64

test.obj: generated.obj testX64
	ml64.exe /c test.s

testX64: testX64.cpp X64.h
	g++ -Wall testX64.cpp -o testX64