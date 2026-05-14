@echo off
echo Building RecoilCrosshair with MSVC...
cl /O2 /EHsc crosshair.cpp /link gdi32.lib winmm.lib shell32.lib user32.lib /SUBSYSTEM:WINDOWS /OUT:RecoilCrosshair.exe
if %errorlevel% == 0 (
    echo.
    echo Build successful! Run RecoilCrosshair.exe
) else (
    echo.
    echo Build failed. Run this from a Visual Studio Developer Command Prompt.
)
pause
