@echo off
pushd %~dp0\..\
call vendor\premake\bin\premake5.exe --file=Editor\premake5.lua vs2022
call vendor\premake\bin\premake5.exe --file=Builder\premake5.lua vs2022
popd
PAUSE