%QTDIR%\bin\qmake -tp vc -spec win32-msvc2010 -o libcgt_qd3d11.vcxproj libcgt_qd3d11.pro
fsi --exec ..\..\vcxproj_win32tox64.fsx libcgt_qd3d11.vcxproj
