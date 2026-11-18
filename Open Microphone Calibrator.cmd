@echo off
setlocal
cd /d "%~dp0"
if exist "C:\Program Files\Python313\pythonw.exe" (
    start "" "C:\Program Files\Python313\pythonw.exe" "%~dp0tools\microphone_calibrator.py"
    exit /b 0
)
where pythonw.exe >nul 2>&1
if not errorlevel 1 (
    start "" pythonw.exe "%~dp0tools\microphone_calibrator.py"
    exit /b 0
)
echo Python with Tkinter and pyserial is required. Ask Codex to repair the calibrator launcher.
pause
