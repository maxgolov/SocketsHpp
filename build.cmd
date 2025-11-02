@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat" -arch=x64
set "PATH=C:\Program Files\CMake\bin;C:\ProgramData\chocolatey\bin;%PATH%"
cmake -GNinja . --preset windows-default
pushd out\build\windows-default
ninja
popd
