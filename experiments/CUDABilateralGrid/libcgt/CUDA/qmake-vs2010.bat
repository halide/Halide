%QTDIR%\bin\qmake -tp vc -spec win32-msvc2010 -o libcgt_cuda.vcxproj libcgt_cuda.pro
fsi --exec ..\vcxproj_win32tox64.fsx libcgt_cuda.vcxproj
