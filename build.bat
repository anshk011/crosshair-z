@echo off
echo Building RecoilCrosshair...
g++ -O2 -mwindows -o RecoilCrosshair.exe crosshair.cpp -lgdi32 -lwinmm -lshell32 -luser32
if %errorlevel% == 0 (
    echo.
    echo Build successful! Run RecoilCrosshair.exe
) else (
    echo.
    echo Build failed. Make sure MinGW is installed and in your PATH.
    echo Download: https://winlibs.com
)
pause
