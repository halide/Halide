%QTDIR%\bin\qmake -tp vc -spec win32-msvc2010 -o libcgt_core.vcxproj libcgt_core.pro
fsi --exec ..\vcxproj_win32tox64.fsx libcgt_core.vcxproj
