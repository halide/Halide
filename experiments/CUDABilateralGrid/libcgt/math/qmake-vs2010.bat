%QTDIR%\bin\qmake -tp vc -spec win32-msvc2010 -o libcgt_math.vcxproj libcgt_math.pro
fsi --exec ..\vcxproj_win32tox64.fsx libcgt_math.vcxproj
