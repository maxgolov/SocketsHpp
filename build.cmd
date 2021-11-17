set "PATH=C:\Program Files\CMake\bin;C:\ProgramData\chocolatey\bin;%PATH%"
cmake -GNinja . --preset windows-default
pushd out\build\windows-default
ninja
popd
